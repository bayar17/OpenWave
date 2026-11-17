#include "appdeploy/appdeploy.hpp"
#include "util.hpp"

#include <libdnf5/base/base.hpp>
#include <libdnf5/base/goal.hpp>
#include <libdnf5/base/transaction.hpp>
#include <libdnf5/base/transaction_package.hpp>
#include <libdnf5/conf/option_string_list.hpp>
#include <libdnf5/logger/logger.hpp>
#include <libdnf5/repo/repo_sack.hpp>
#include <libdnf5/rpm/nevra.hpp>
#include <libdnf5/rpm/package.hpp>
#include <libdnf5/rpm/package_query.hpp>
#include <libdnf5/rpm/reldep_list.hpp>
#include <libdnf5/rpm/transaction_callbacks.hpp>

#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

extern "C" {
#include <rpm/rpmdb.h>
#include <rpm/rpmfi.h>
#include <rpm/rpmlib.h>
#include <rpm/rpmmacro.h>
#include <rpm/rpmts.h>
}

namespace appdeploy {

namespace {

using libdnf5::transaction::TransactionItemAction;

std::string joined(const std::vector<std::string> &items, const char *separator)
{
    std::string out;
    for (const auto &item : items) out += (out.empty() ? "" : separator) + item;
    return out;
}

class SinkLogger : public libdnf5::Logger {
public:
    explicit SinkLogger(const ProgressSink &sink) : sink_(sink) {}
    void write(const std::chrono::time_point<std::chrono::system_clock> &, pid_t, Level level,
               const std::string &message) noexcept override
    {
        if (level > Level::WARNING || !sink_) return;
        try {
            sink_({level <= Level::ERROR ? Progress::Kind::Warning : Progress::Kind::Info, message, 0, 0});
        } catch (...) {}
    }

private:
    const ProgressSink &sink_;
};

class RpmProgress : public libdnf5::rpm::TransactionCallbacks {
public:
    explicit RpmProgress(const ProgressSink &sink) : sink_(sink) {}
    void install_start(const libdnf5::base::TransactionPackage &item, uint64_t total) override
    {
        emit(Progress::Kind::Install, "installing " + item.get_package().get_full_nevra(), 0, total);
    }
    void install_stop(const libdnf5::base::TransactionPackage &item, uint64_t amount, uint64_t total) override
    {
        emit(Progress::Kind::Install, "installed " + item.get_package().get_full_nevra(), amount, total);
    }
    void uninstall_start(const libdnf5::base::TransactionPackage &item, uint64_t total) override
    {
        emit(Progress::Kind::Remove, "removing " + item.get_package().get_full_nevra(), 0, total);
    }
    void uninstall_stop(const libdnf5::base::TransactionPackage &item, uint64_t amount, uint64_t total) override
    {
        emit(Progress::Kind::Remove, "removed " + item.get_package().get_full_nevra(), amount, total);
    }
    void verify_start(std::uint64_t total) override { emit(Progress::Kind::Verify, "verifying the transaction", 0, total); }
    void script_start(const libdnf5::base::TransactionPackage *item, libdnf5::rpm::Nevra, ScriptType) override
    {
        emit(Progress::Kind::Warning, "UNEXPECTED: an RPM scriptlet started for "
                                          + (item ? item->get_package().get_full_nevra() : std::string("?")), 0, 0);
    }
    void unpack_error(const libdnf5::base::TransactionPackage &item) override
    {
        emit(Progress::Kind::Warning, "cannot unpack " + item.get_package().get_full_nevra(), 0, 0);
    }
    void cpio_error(const libdnf5::base::TransactionPackage &item) override
    {
        emit(Progress::Kind::Warning, "payload error in " + item.get_package().get_full_nevra(), 0, 0);
    }

private:
    void emit(Progress::Kind kind, std::string text, std::uint64_t current, std::uint64_t total)
    {
        if (sink_) sink_({kind, std::move(text), current, total});
    }
    const ProgressSink &sink_;
};

class Lock {
public:
    explicit Lock(const std::string &path)
    {
        fd_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (fd_ < 0) throw Error("cannot open the transaction lock " + path + ": " + util::errno_text(errno));
        if (::flock(fd_, LOCK_EX | LOCK_NB)) {
            ::close(fd_);
            fd_ = -1;
            throw Error("another package transaction is in progress");
        }
    }
    ~Lock() { if (fd_ >= 0) ::close(fd_); }
    Lock(const Lock &) = delete;
    Lock &operator=(const Lock &) = delete;

private:
    int fd_ = -1;
};

struct Fd {
    int fd = -1;
    explicit Fd(int value = -1) : fd(value) {}
    ~Fd() { if (fd >= 0) ::close(fd); }
    Fd(const Fd &) = delete;
    Fd &operator=(const Fd &) = delete;
};

void make_directories(const std::string &path, mode_t mode)
{
    std::string current;
    for (const auto &part : util::split(path, '/')) {
        if (part.empty()) continue;
        current += "/" + part;
        if (::mkdir(current.c_str(), mode) && errno != EEXIST)
            throw Error("cannot create " + current + ": " + util::errno_text(errno));
    }
}

std::string provides_class_application(const std::string &name) { return "openwave-application(" + name + ")"; }

struct Known {
    std::string name, version, release, arch, nevra;
    std::optional<PackageClass> klass;
};

Known describe(const libdnf5::rpm::Package &package)
{
    Known known;
    known.name = package.get_name();
    known.version = package.get_version();
    known.release = package.get_release();
    known.arch = package.get_arch();
    known.nevra = package.get_full_nevra();
    bool application = false, system = false;
    const auto provides = package.get_provides();
    for (const auto &provide : provides) {
        const std::string name = provide.get_name();
        if (name == provides_class_application(known.name)) application = true;
        if (name == "openwave-system-package(" + known.name + ")") system = true;
    }
    if (application != system) known.klass = application ? PackageClass::Application : PackageClass::System;
    return known;
}

}

struct Deployer::Impl {
    Config config;
    ProgressSink sink;
    ConfirmFn confirm;
    KeyStore keys;
    Policy policy;
    Registry registry;
    std::map<std::string, std::string> pending_pins;
    Labeler labeler;

    Impl(Config c, ProgressSink s, ConfirmFn f) : config(std::move(c)), sink(std::move(s)), confirm(std::move(f)),
                                                  keys(config), policy(config), registry(config), labeler(config) {}

    void say(Progress::Kind kind, const std::string &text) const
    {
        if (sink) sink({kind, text, 0, 0});
    }

    std::string state(const std::string &leaf) const { return config.path(config.state_dir + "/" + leaf); }

    std::unique_ptr<libdnf5::Base> make_base()
    {
        std::vector<std::unique_ptr<libdnf5::Logger>> loggers;
        loggers.emplace_back(std::make_unique<SinkLogger>(sink));
        auto base = std::make_unique<libdnf5::Base>(std::move(loggers));
        auto &c = base->get_config();
        c.get_use_host_config_option().set(libdnf5::Option::Priority::RUNTIME, false);
        c.get_installroot_option().set(libdnf5::Option::Priority::RUNTIME, config.root.empty() ? "/" : config.root);
        c.get_reposdir_option().set(libdnf5::Option::Priority::RUNTIME, std::vector<std::string>{});
        c.get_varsdir_option().set(libdnf5::Option::Priority::RUNTIME, std::vector<std::string>{});
        c.get_cachedir_option().set(libdnf5::Option::Priority::RUNTIME, config.path(config.cache_dir));
        c.get_system_cachedir_option().set(libdnf5::Option::Priority::RUNTIME, config.path(config.cache_dir));
        c.get_persistdir_option().set(libdnf5::Option::Priority::RUNTIME, "/var/lib/appdeploy/persist");
        c.get_system_state_dir_option().set(libdnf5::Option::Priority::RUNTIME, "/var/lib/appdeploy/state");
        c.get_transaction_history_dir_option().set(libdnf5::Option::Priority::RUNTIME, "/var/lib/appdeploy/state");
        c.get_logdir_option().set(libdnf5::Option::Priority::RUNTIME, config.path(config.state_dir + "/log"));
        c.get_plugins_option().set(libdnf5::Option::Priority::RUNTIME, false);
        c.get_install_weak_deps_option().set(libdnf5::Option::Priority::RUNTIME, false);
        c.get_assumeyes_option().set(libdnf5::Option::Priority::RUNTIME, true);
        c.get_tsflags_option().set(libdnf5::Option::Priority::RUNTIME,
                                   std::vector<std::string>{"noscripts", "notriggers", "nocontexts"});
        base->setup();
        base->get_repo_sack()->load_repos(libdnf5::repo::Repo::Type::SYSTEM);
        return base;
    }

    struct Journal {
        std::string operation;
        std::vector<std::string> install, remove;
    };
    std::string journal_path() const { return state("state/journal"); }

    void write_journal(const Journal &journal) const
    {
        std::string text = "operation " + journal.operation + "\n";
        for (const auto &entry : journal.install) text += "install " + entry + "\n";
        for (const auto &entry : journal.remove) text += "remove " + entry + "\n";
        const int fd = util::open_directory_nofollow(state("state"));
        if (fd < 0) throw Error("the state directory is missing");
        try { util::write_file_atomic(fd, "journal", text, 0600); } catch (...) { ::close(fd); throw; }
        ::close(fd);
    }
    std::optional<Journal> read_journal() const
    {
        std::ifstream stream(journal_path());
        if (!stream) return std::nullopt;
        Journal journal;
        std::string line;
        while (std::getline(stream, line)) {
            const auto space = line.find(' ');
            if (space == std::string::npos) continue;
            const std::string key = line.substr(0, space), value = line.substr(space + 1);
            if (key == "operation") journal.operation = value;
            else if (key == "install") journal.install.push_back(value);
            else if (key == "remove") journal.remove.push_back(value);
        }
        return journal;
    }
    void clear_journal() const
    {
        const int fd = util::open_directory_nofollow(state("state"));
        if (fd >= 0) { ::unlinkat(fd, "journal", 0); ::fsync(fd); ::close(fd); }
    }

    void prepare_directories() const
    {
        make_directories(config.path(config.state_dir + "/rpmdb"), 0700);
        make_directories(config.path(config.state_dir + "/state"), 0700);
        make_directories(config.path(config.state_dir + "/staging"), 0700);
        make_directories(config.path(config.state_dir + "/log"), 0700);
        make_directories(config.path(config.cache_dir), 0700);
        make_directories(config.path(config.state_dir + "/persist"), 0700);
        if (::chdir(config.path(config.state_dir).c_str()) != 0) say(Progress::Kind::Warning, "cannot enter the state directory");
    }

    struct Candidate {
        libdnf5::rpm::Package package;
        TransactionItemAction action;
        PackageInfo info;
        KeyStore::Verdict verdict;
        std::string display_name;
        std::vector<std::string> permissions;
        std::string path;
    };

    std::vector<Violation> check_file(Candidate &candidate, const Options &options) const
    {
        std::vector<Violation> violations;
        const Fd fd(::open(candidate.path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
        if (fd.fd < 0) return {{"io", "cannot open " + candidate.path + ": " + util::errno_text(errno)}};
        struct stat st;
        if (::fstat(fd.fd, &st) || !S_ISREG(st.st_mode)) return {{"io", candidate.path + " is not a regular file"}};
        if (std::uint64_t(st.st_size) > config.max_package_bytes) return {{"limits", candidate.path + " is too large"}};
        candidate.info = read_package(fd.fd, candidate.path);
        PackageInfo &info = candidate.info;
        classify(info);
        std::string embedded_key;
        if (info.klass == PackageClass::Application) {
            try {
                embedded_key = read_payload_file(fd.fd, candidate.path,
                                                 config.applications + "/" + info.name + "/" + kPublisherKeyPath, 65536).value_or("");
            } catch (const Error &) {}
        }
        candidate.verdict = keys.verify_package(fd.fd, candidate.path, info.name, embedded_key);
        if (!candidate.verdict.intact)
            violations.push_back({"integrity", info.nevra() + ": " + candidate.verdict.reason});
        if (candidate.verdict.publisher_mismatch)
            violations.push_back({"publisher", info.nevra() + ": " + candidate.verdict.reason});
        const Known known = describe(candidate.package);
        if (info.name != known.name || info.version != known.version || info.release != known.release
            || info.arch != known.arch)
            violations.push_back({"metadata", "the file does not match the package the solver chose (" + known.nevra
                                              + " vs " + info.nevra() + ")"});
        for (auto &violation : policy.evaluate(info, candidate.verdict.trust)) violations.push_back(std::move(violation));
        if (info.klass == PackageClass::System && !options.allow_system)
            violations.push_back({"authorization", info.nevra() + " is a system package: installing it needs an explicit "
                                                   "administrator transaction (--system)"});
        candidate.display_name = info.name;
        if (info.klass == PackageClass::Application) {
            std::optional<std::string> toml;
            try { toml = read_payload_file(fd.fd, candidate.path, config.applications + "/" + info.name + "/Application.toml", 64 * 1024); }
            catch (const Error &error) { violations.push_back({"payload", error.what()}); }
            for (auto &violation : policy.declaration(info, toml)) violations.push_back(std::move(violation));
            if (violations.empty() && toml) {
                try {
                    const auto descriptor = ApplicationDescriptor::parse(*toml, info.name);
                    candidate.display_name = descriptor.name;
                    candidate.permissions = descriptor.permissions;
                } catch (const Error &) {}
            }
        }
        return violations;
    }

    std::vector<Violation> check_conflicts(libdnf5::Base &base, const Candidate &candidate) const
    {
        std::vector<Violation> violations;
        const std::string name = candidate.package.get_name();
        for (const auto &entry : candidate.info.files) {
            if (entry.directory()) continue;
            struct stat st;
            if (::lstat(config.path(entry.path).c_str(), &st)) continue;
            libdnf5::rpm::PackageQuery owners(base.get_weak_ptr());
            owners.filter_installed();
            owners.filter_file(entry.path);
            bool own = false;
            for (const auto &owner : owners)
                if (owner.get_name() == name) own = true;
            if (!own) {
                std::string who = "no package";
                for (const auto &owner : owners) { who = owner.get_name(); break; }
                violations.push_back({"file-conflict", entry.path + " already exists and belongs to " + who
                                                       + "; it would be overwritten"});
            }
        }
        return violations;
    }

    std::string installed_display_name(const Known &known) const
    {
        if (known.klass != PackageClass::Application) return known.name;
        const int fd = util::open_directory_nofollow(config.path(config.applications + "/" + known.name));
        if (fd < 0) return known.name;
        const std::string text = util::read_regular_file(fd, "Application.toml", 64 * 1024, config.owner_uid);
        ::close(fd);
        try { return ApplicationDescriptor::parse(text, known.name).name; } catch (const Error &) { return known.name; }
    }

    void purge_user_data(const std::string &id) const
    {
        const std::string users = config.path(config.users_dir);
        DIR *stream = ::opendir(users.c_str());
        if (!stream) return;
        std::vector<std::string> names;
        while (dirent *entry = ::readdir(stream))
            if (std::strcmp(entry->d_name, ".") && std::strcmp(entry->d_name, "..")) names.emplace_back(entry->d_name);
        ::closedir(stream);
        for (const auto &user : names) {
            const std::string parent = users + "/" + user + "/.DataLocation/Applications";
            const int fd = util::open_directory_nofollow(parent);
            if (fd < 0) continue;
            util::remove_tree(fd, id.c_str());
            ::close(fd);
            say(Progress::Kind::Info, "removed application data of " + id + " for " + user);
        }
    }

    TransactionResult run(const std::string &operation, const std::function<void(libdnf5::Goal &, libdnf5::Base &)> &fill,
                          const Options &options)
    {
        TransactionResult result;
        prepare_directories();
        Lock lock(state("lock"));
        if (read_journal()) {
            say(Progress::Kind::Warning, "an earlier transaction did not finish: reconciling first");
            reconcile_locked();
        }
        auto base = make_base();
        libdnf5::Goal goal(*base);
        try { fill(goal, *base); }
        catch (const Error &error) { result.error = error.what(); return result; }
        auto transaction = goal.resolve();
        if (transaction.get_problems() != libdnf5::GoalProblem::NO_PROBLEM) {
            result.error = "the request cannot be satisfied:";
            for (const auto &line : transaction.get_resolve_logs_as_strings()) result.error += "\n  " + line;
            return result;
        }
        if (transaction.empty()) {
            result.ok = true;
            result.notes.push_back("nothing to do");
            return result;
        }

        std::vector<Candidate> installs;
        std::vector<Known> removals;
        std::vector<Violation> all;
        for (const auto &item : transaction.get_transaction_packages()) {
            const auto package = item.get_package();
            switch (item.get_action()) {
            case TransactionItemAction::INSTALL:
            case TransactionItemAction::UPGRADE:
            case TransactionItemAction::DOWNGRADE:
            case TransactionItemAction::REINSTALL: {
                if (item.get_action() == TransactionItemAction::DOWNGRADE && !options.allow_downgrade)
                    all.push_back({"downgrade", package.get_full_nevra() + " would be a downgrade (use --allow-downgrade)"});
                Candidate candidate{package, item.get_action(), {}, {}, {}, {}, package.get_package_path()};
                for (auto &violation : check_file(candidate, options)) all.push_back(std::move(violation));
                for (auto &violation : check_conflicts(*base, candidate)) all.push_back(std::move(violation));
                installs.push_back(std::move(candidate));
                break;
            }
            case TransactionItemAction::REMOVE:
            case TransactionItemAction::REPLACED: {
                const Known known = describe(package);
                if (!known.klass) {
                    all.push_back({"unmanaged", known.nevra + " is not an OpenWave package and is not removed by appdeploy"});
                } else if (*known.klass == PackageClass::System && !options.allow_system) {
                    all.push_back({"authorization", known.nevra + " is a system package: removing it needs an explicit "
                                                    "administrator transaction (--system)"});
                }
                if (item.get_action() == TransactionItemAction::REMOVE) removals.push_back(known);
                break;
            }
            default:
                break;
            }
        }
        if (!all.empty()) {
            result.error = "the transaction was rejected by policy:";
            for (const auto &violation : all) result.error += "\n  [" + violation.rule + "] " + violation.message;
            return result;
        }
        for (const auto &candidate : installs)
            (candidate.action == TransactionItemAction::UPGRADE ? result.upgraded : result.installed)
                .push_back(candidate.package.get_full_nevra());
        for (const auto &known : removals) result.removed.push_back(known.nevra);
        {
            const auto test_rc = transaction.test();
            if (test_rc != libdnf5::base::Transaction::TransactionRunResult::SUCCESS) {
                result.error = "the transaction check failed:";
                for (const auto &problem : transaction.get_transaction_problems()) result.error += "\n  " + problem;
                if (transaction.get_transaction_problems().empty())
                    result.error += " " + libdnf5::base::Transaction::transaction_result_to_string(test_rc);
                return result;
            }
        }
        if (options.assume_no) {
            result.ok = true;
            result.notes.push_back("nothing was changed (plan only)");
            return result;
        }

        if (!options.assume_yes) {
            if (!confirm) {
                result.error = "no way to ask for confirmation here; an administrator can pass --yes";
                return result;
            }
            std::vector<Confirmation> questions;
            for (const auto &candidate : installs) {
                Confirmation question;
                question.name = candidate.display_name;
                question.package = candidate.info.name;
                question.version = candidate.info.version;
                question.klass = candidate.info.klass.value_or(PackageClass::Application);
                question.trust = candidate.verdict.trust;
                question.trust_reason = candidate.verdict.reason;
                question.permissions = candidate.permissions;
                question.action = Confirmation::Action::Install;
                libdnf5::rpm::PackageQuery old(base->get_weak_ptr());
                old.filter_installed();
                old.filter_name(candidate.package.get_name());
                for (const auto &previous : old) { question.action = Confirmation::Action::Update; question.old_version = previous.get_version(); }
                questions.push_back(std::move(question));
            }
            for (const auto &known : removals) {
                Confirmation question;
                question.action = Confirmation::Action::Remove;
                question.name = installed_display_name(known);
                question.package = known.name;
                question.version = known.version;
                question.klass = known.klass.value_or(PackageClass::Application);
                question.trust = Trust::Application;
                questions.push_back(std::move(question));
            }
            for (const auto &question : questions) {
                bool agreed = false;
                try { agreed = confirm(question); } catch (const std::exception &failure) {
                    result.error = std::string("the confirmation could not be shown: ") + failure.what()
                                   + "\nAn administrator can pass --yes.";
                    return result;
                }
                if (!agreed) {
                    result.error = "cancelled: nothing was changed";
                    return result;
                }
            }
        }

        Journal journal{operation, {}, {}};
        for (const auto &candidate : installs)
            journal.install.push_back(candidate.package.get_name() + " " + candidate.package.get_version() + " "
                                      + (candidate.info.klass ? class_name(*candidate.info.klass) : "unknown"));
        for (const auto &item : transaction.get_transaction_packages()) {
            const auto action = item.get_action();
            if (action == TransactionItemAction::REMOVE || action == TransactionItemAction::REPLACED) {
                const Known known = describe(item.get_package());
                journal.remove.push_back(known.name + " " + known.version + " " + (known.klass ? class_name(*known.klass) : "unknown"));
            }
        }
        pending_pins.clear();
        for (const auto &candidate : installs)
            if (candidate.verdict.trust == Trust::Publisher && !candidate.verdict.publisher_key.empty()
                && candidate.info.klass == PackageClass::Application)
                pending_pins[candidate.info.name] = candidate.verdict.publisher_key;
        write_journal(journal);
        for (const auto &entry : journal.remove) {
            const auto fields = util::split(entry, ' ');
            if (fields.size() == 3 && fields[2] == "application") registry.unregister_bundle(fields[0]);
        }
        transaction.set_callbacks(std::make_unique<RpmProgress>(sink));
        transaction.set_description("appdeploy " + operation);
        const auto rc = transaction.run();
        if (rc != libdnf5::base::Transaction::TransactionRunResult::SUCCESS) {
            result.error = "the transaction failed: " + libdnf5::base::Transaction::transaction_result_to_string(rc);
            for (const auto &problem : transaction.get_transaction_problems()) result.error += "\n  " + problem;
            if (rc == libdnf5::base::Transaction::TransactionRunResult::ERROR_RPM_RUN) {
                result.error += "\nThe package database may be partly changed; run `appdeploy reconcile`.";
                result.ok = false;
                return result;
            }
            clear_journal();
            reconcile_registry(*base);
            return result;
        }
        std::string finish_error;
        finish(journal, options, finish_error);
        if (!finish_error.empty()) {
            result.error = finish_error + "\nThe packages are installed but not registered; run `appdeploy reconcile`.";
            return result;
        }
        clear_journal();
        result.ok = true;
        return result;
    }

    void finish(const Journal &journal, const Options &options, std::string &error)
    {
        try {
            for (const auto &entry : journal.install) {
                const auto fields = util::split(entry, ' ');
                if (fields.size() != 3) continue;
                if (fields[2] == "application") {
                    labeler.label_tree(config.applications + "/" + fields[0]);
                    registry.register_bundle(fields[0], fields[1]);
                    const auto pin = pending_pins.find(fields[0]);
                    if (pin != pending_pins.end()) keys.pin_publisher(fields[0], pin->second);
                } else if (fields[2] == "system" && labeler.enabled()) {
                    labeler_system_files(fields[0]);
                }
            }
            std::set<std::string> replaced;
            for (const auto &entry : journal.install) {
                const auto fields = util::split(entry, ' ');
                if (!fields.empty()) replaced.insert(fields[0]);
            }
            for (const auto &entry : journal.remove) {
                const auto fields = util::split(entry, ' ');
                if (fields.size() != 3 || fields[2] != "application" || replaced.count(fields[0])) continue;
                registry.unregister_bundle(fields[0]);
                keys.unpin_publisher(fields[0]);
                if (options.purge_data) purge_user_data(fields[0]);
            }
        } catch (const std::exception &failure) {
            error = failure.what();
        }
    }

    void labeler_system_files(const std::string &name)
    {
        auto base = make_base();
        libdnf5::rpm::PackageQuery query(base->get_weak_ptr());
        query.filter_installed();
        query.filter_name(name);
        for (const auto &package : query)
            for (const auto &file : package.get_files()) {
                struct stat st;
                if (!::lstat(config.path(file).c_str(), &st)) labeler.label(file, st.st_mode);
            }
    }

    TransactionResult reconcile_registry(libdnf5::Base &base)
    {
        TransactionResult result;
        result.ok = true;
        std::set<std::string> installed_ids;
        libdnf5::rpm::PackageQuery query(base.get_weak_ptr());
        query.filter_installed();
        for (const auto &package : query) {
            const Known known = describe(package);
            if (known.klass != PackageClass::Application) continue;
            installed_ids.insert(known.name);
            std::string detail;
            const auto state = registry.state(known.name, known.version, &detail);
            if (state == Registry::State::Current) continue;
            try {
                labeler.label_tree(config.applications + "/" + known.name);
                registry.register_bundle(known.name, known.version);
                result.notes.push_back("registered " + known.name + (detail.empty() ? "" : " (" + detail + ")"));
            } catch (const std::exception &failure) {
                registry.unregister_bundle(known.name);
                result.ok = false;
                result.notes.push_back("cannot register " + known.name + ": " + failure.what());
            }
        }
        const std::string root = config.path(config.applications);
        if (DIR *stream = ::opendir(root.c_str())) {
            std::vector<std::string> names;
            while (dirent *entry = ::readdir(stream))
                if (entry->d_name[0] != '.') names.emplace_back(entry->d_name);
            ::closedir(stream);
            for (const auto &name : names) {
                if (installed_ids.count(name) || !valid_application_id(name)) continue;
                struct stat st;
                if (::lstat((root + "/" + name + "/Application.toml").c_str(), &st)) continue;
                if (registry.registered(name)) {
                    registry.unregister_bundle(name);
                    result.notes.push_back("unregistered " + name + " (its package is not installed)");
                }
            }
        }
        return result;
    }

    TransactionResult reconcile_locked()
    {
        auto base = make_base();
        TransactionResult result = reconcile_registry(*base);
        if (const auto journal = read_journal()) {
            result.notes.push_back("finished an interrupted " + journal->operation + " transaction");
            clear_journal();
        }
        return result;
    }

    PackageRow row(const libdnf5::rpm::Package &package) const
    {
        PackageRow out;
        out.name = package.get_name();
        out.version = package.get_version();
        out.release = package.get_release();
        out.arch = package.get_arch();
        out.summary = package.get_summary();
        const Known known = describe(package);
        out.klass = known.klass;
        if (known.klass == PackageClass::Application) out.application_id = known.name;
        return out;
    }
};

Deployer::Deployer(Config config, ProgressSink sink, ConfirmFn confirm)
    : p(new Impl(std::move(config), std::move(sink), std::move(confirm))) {}
Deployer::~Deployer() = default;

TransactionResult Deployer::install_files(const std::vector<File> &files, const Options &options)
{
    if (files.empty() || files.size() > 16) throw Error("give between one and sixteen package files");
    p->prepare_directories();
    struct Staged { std::string path, display; };
    std::vector<Staged> staged;
    struct Cleanup {
        std::vector<Staged> &items;
        ~Cleanup() { for (const auto &item : items) ::unlink(item.path.c_str()); }
    } cleanup{staged};
    const std::string directory = p->config.path(p->config.state_dir + "/staging");
    for (const auto &file : files) {
        struct stat st;
        if (::fstat(file.fd, &st) || !S_ISREG(st.st_mode)) throw Error(file.display_name + " is not a regular file");
        if (std::uint64_t(st.st_size) > p->config.max_package_bytes) throw Error(file.display_name + " is too large");
        std::string path = directory + "/package-XXXXXX.rpm";
        Fd out(::mkstemps(path.data(), 4));
        if (out.fd < 0) throw Error("cannot stage the package: " + util::errno_text(errno));
        staged.push_back({path, file.display_name});
        ::fchmod(out.fd, 0600);
        ::lseek(file.fd, 0, SEEK_SET);
        char buffer[65536];
        std::uint64_t total = 0;
        for (;;) {
            const auto count = ::read(file.fd, buffer, sizeof(buffer));
            if (count < 0) throw Error("cannot read " + file.display_name);
            if (count == 0) break;
            total += std::uint64_t(count);
            if (total > p->config.max_package_bytes) throw Error(file.display_name + " is too large");
            if (::write(out.fd, buffer, std::size_t(count)) != count) throw Error("cannot stage the package");
        }
        ::fsync(out.fd);
        try { read_package(out.fd, file.display_name); }
        catch (const Error &error) { throw Error(file.display_name + " is not a usable RPM package: " + error.what()); }
    }
    return p->run("install", [&](libdnf5::Goal &goal, libdnf5::Base &base) {
        std::vector<std::string> paths;
        for (const auto &item : staged) paths.push_back(item.path);
        auto packages = base.get_repo_sack()->add_cmdline_packages(paths);
        for (const auto &item : staged) {
            const auto found = packages.find(item.path);
            if (found == packages.end()) throw Error(item.display + " is not a usable RPM package");
            libdnf5::rpm::PackageQuery installed(base.get_weak_ptr());
            installed.filter_installed();
            installed.filter_name(found->second.get_name());
            if (installed.empty()) { goal.add_rpm_install(found->second); continue; }
            bool newer = false, same = false;
            for (const auto &previous : installed) {
                const int comparison = libdnf5::rpm::evrcmp(found->second, previous);
                newer = newer || comparison > 0;
                same = same || comparison == 0;
            }
            if (newer) goal.add_rpm_upgrade(found->second);
            else if (same) goal.add_rpm_reinstall(found->second);
            else if (options.allow_downgrade) goal.add_rpm_downgrade(found->second);
            else throw Error(item.display + " is older than the installed version (use --allow-downgrade)");
        }
    }, options);
}

TransactionResult Deployer::remove(const std::vector<std::string> &specs, const Options &options)
{
    for (const auto &spec : specs)
        if (!valid_package_spec(spec)) throw Error("invalid package name: " + spec);
    return p->run("remove", [&](libdnf5::Goal &goal, libdnf5::Base &base) {
        for (const auto &spec : specs) {
            libdnf5::rpm::PackageQuery installed(base.get_weak_ptr());
            installed.filter_installed();
            installed.filter_name(spec);
            if (installed.empty()) throw Error(spec + " is not installed");
            goal.add_remove(spec);
        }
    }, options);
}

std::vector<PackageRow> Deployer::list_installed()
{
    auto base = p->make_base();
    libdnf5::rpm::PackageQuery query(base->get_weak_ptr());
    query.filter_installed();
    std::vector<PackageRow> rows;
    for (const auto &package : query) {
        PackageRow row = p->row(package);
        if (row.klass) rows.push_back(std::move(row));
    }
    std::sort(rows.begin(), rows.end(), [](const PackageRow &a, const PackageRow &b) { return a.name < b.name; });
    return rows;
}

std::optional<PackageRow> Deployer::info(const std::string &spec, std::vector<std::string> *detail)
{
    if (!valid_package_spec(spec)) throw Error("invalid package name: " + spec);
    auto base = p->make_base();
    libdnf5::rpm::PackageQuery query(base->get_weak_ptr());
    query.filter_installed();
    query.filter_name(spec);
    for (const auto &package : query) {
        PackageRow row = p->row(package);
        if (detail) {
            detail->push_back("Name        : " + p->installed_display_name(describe(package)));
            detail->push_back("Package     : " + row.name);
            detail->push_back("Version     : " + row.version + "-" + row.release);
            detail->push_back("Arch        : " + row.arch);
            detail->push_back(std::string("Class       : ") + (row.klass ? class_name(*row.klass) : "unmanaged"));
            if (!row.application_id.empty()) detail->push_back("Application : " + row.application_id);
            detail->push_back("Summary     : " + row.summary);
            std::vector<std::string> requires_;
            for (const auto &dep : package.get_requires()) requires_.push_back(dep.to_string());
            if (!requires_.empty()) detail->push_back("Requires    : " + joined(requires_, ", "));
            detail->push_back("Size        : " + std::to_string(package.get_install_size()) + " bytes installed");
        }
        return row;
    }
    return std::nullopt;
}

namespace {
const char *verify_attribute(rpmVerifyAttrs attrs)
{
    if (attrs & RPMVERIFY_FILEDIGEST) return "content changed";
    if (attrs & RPMVERIFY_READLINKFAIL) return "link target changed";
    if (attrs & RPMVERIFY_LSTATFAIL) return "missing";
    if (attrs & RPMVERIFY_FILESIZE) return "size changed";
    if (attrs & RPMVERIFY_MODE) return "mode changed";
    if (attrs & RPMVERIFY_USER) return "owner changed";
    if (attrs & RPMVERIFY_GROUP) return "group changed";
    if (attrs & RPMVERIFY_LINKTO) return "link target changed";
    return "changed";
}
}

Deployer::VerifyReport Deployer::verify(const std::string &spec)
{
    if (!valid_package_spec(spec)) throw Error("invalid package name: " + spec);
    VerifyReport report;
    { auto base = p->make_base(); (void)base; }
    rpmts ts = rpmtsCreate();
    rpmtsSetRootDir(ts, p->config.root.empty() ? "/" : p->config.root.c_str());
    struct Free { rpmts ts; ~Free() { rpmtsFree(ts); } } guard{ts};
    rpmdbMatchIterator iterator = rpmtsInitIterator(ts, RPMDBI_NAME, spec.c_str(), 0);
    Header header = iterator ? rpmdbNextIterator(iterator) : nullptr;
    if (!header) {
        if (iterator) rpmdbFreeIterator(iterator);
        report.ok = false;
        report.problems.push_back(spec + " is not installed");
        return report;
    }
    PackageInfo info;
    {
        const char *version = headerGetString(header, RPMTAG_VERSION);
        info.name = spec;
        info.version = version ? version : "";
    }
    rpmfiles files = rpmfilesNew(nullptr, header, RPMTAG_BASENAMES, RPMFI_FLAGS_VERIFY);
    std::set<std::string> owned;
    if (files) {
        rpmfi fi = rpmfilesIter(files, RPMFI_ITER_FWD);
        while (rpmfiNext(fi) >= 0) {
            const int index = rpmfiFX(fi);
            const std::string path = rpmfiFN(fi);
            owned.insert(path);
            if (rpmfiFFlags(fi) & RPMFILE_GHOST) continue;
            if (p->labeler.enabled()) {
                const std::string want = p->labeler.expected(path, rpmfiFMode(fi)), have = p->labeler.current(path);
                if (!want.empty() && want != have) {
                    report.ok = false;
                    report.problems.push_back(path + ": SELinux label is " + (have.empty() ? "missing" : have) + ", expected " + want);
                }
            }
            const rpmVerifyAttrs attrs = rpmfilesVerify(files, index, RPMVERIFY_ALL);
            if (attrs) {
                report.ok = false;
                report.problems.push_back(path + ": " + verify_attribute(attrs));
            }
        }
        rpmfiFree(fi);
        rpmfilesFree(files);
    }
    const std::string name = headerGetString(header, RPMTAG_NAME) ? headerGetString(header, RPMTAG_NAME) : "";
    const std::string version = headerGetString(header, RPMTAG_VERSION) ? headerGetString(header, RPMTAG_VERSION) : "";
    rpmdbFreeIterator(iterator);
    const std::string bundle = p->config.applications + "/" + name;
    struct stat st;
    if (!::lstat(p->config.path(bundle + "/Application.toml").c_str(), &st)) {
        std::string detail;
        const auto state = p->registry.state(name, version, &detail);
        if (state != Registry::State::Current) {
            report.ok = false;
            report.problems.push_back("registry: " + detail);
        }
        std::vector<std::string> pending{bundle};
        while (!pending.empty()) {
            const std::string directory = pending.back();
            pending.pop_back();
            DIR *stream = ::opendir(p->config.path(directory).c_str());
            if (!stream) continue;
            while (dirent *entry = ::readdir(stream)) {
                if (!std::strcmp(entry->d_name, ".") || !std::strcmp(entry->d_name, "..")) continue;
                const std::string child = directory + "/" + entry->d_name;
                struct stat info;
                if (::lstat(p->config.path(child).c_str(), &info)) continue;
                if (child == bundle + "/manifest.json" || child == bundle + "/.manifest.appdeploy") continue;
                if (!owned.count(child)) {
                    report.ok = false;
                    report.problems.push_back(child + ": not part of the package");
                }
                if (S_ISDIR(info.st_mode)) pending.push_back(child);
            }
            ::closedir(stream);
        }
    }
    return report;
}

TransactionResult Deployer::reconcile()
{
    p->prepare_directories();
    Lock lock(p->state("lock"));
    return p->reconcile_locked();
}

}
