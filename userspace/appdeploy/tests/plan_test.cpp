#include "appdeploy/appdeploy.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace appdeploy;
namespace fs = std::filesystem;

static int failures = 0, checks = 0;
#define CHECK_MSG(condition, message) do { ++checks; if (!(condition)) { ++failures; std::fprintf(stderr, "FAIL %s:%d: %s -- %s\n", __FILE__, __LINE__, #condition, std::string(message).c_str()); } } while (0)

static bool has(const std::vector<std::string> &list, const std::string &needle)
{
    return std::any_of(list.begin(), list.end(), [&](const std::string &s) { return s.find(needle) != std::string::npos; });
}

static std::string packages_dir;

struct Opened {
    int fd = -1;
    std::string name;
    ~Opened() { if (fd >= 0) ::close(fd); }
};
static std::vector<std::unique_ptr<Opened>> keep;
static Deployer::File file(const std::string &stem)
{
    for (const auto &entry : fs::directory_iterator(packages_dir)) {
        const std::string name = entry.path().filename().string();
        const bool match = name.rfind(stem + "-", 0) == 0 && name.size() > stem.size() + 1
                           && std::isdigit(static_cast<unsigned char>(name[stem.size() + 1])) && name.find(".rpm") != std::string::npos;
        if (match) {
            auto opened = std::make_unique<Opened>();
            opened->fd = ::open(entry.path().c_str(), O_RDONLY | O_CLOEXEC);
            opened->name = name;
            Deployer::File out{opened->fd, name};
            keep.push_back(std::move(opened));
            return out;
        }
    }
    std::fprintf(stderr, "no test package %s\n", stem.c_str());
    std::exit(2);
}

int main(int argc, char **argv)
{
    if (argc != 3) { std::fprintf(stderr, "usage: plan_test KEYS PACKAGES\n"); return 2; }
    packages_dir = argv[2];
    char scratch[] = "/tmp/appdeploy-plan-XXXXXX";
    if (!::mkdtemp(scratch)) return 2;
    Config config;
    config.root = scratch;
    config.owner_uid = ::getuid();
    for (const char *klass : {"application", "system"}) {
        const fs::path target = fs::path(scratch) / "etc/appdeploy/keys" / klass;
        fs::create_directories(target);
        for (const auto &entry : fs::directory_iterator(fs::path(argv[1]) / klass)) fs::copy_file(entry.path(), target / entry.path().filename());
    }
    for (const auto &entry : fs::recursive_directory_iterator(fs::path(scratch) / "etc")) {
        fs::permissions(entry.path(), entry.is_directory() ? fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec | fs::perms::others_read | fs::perms::others_exec
                                                            : fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read | fs::perms::others_read);
    }

    std::vector<Confirmation> asked;
    int questions = 0;
    Deployer deployer(config, nullptr, [&](const Confirmation &c) { asked.push_back(c); ++questions; return false; });
    Deployer two_questions(config, nullptr, [&](const Confirmation &c) { asked.push_back(c); return asked.size() < 2; });
    Deployer silent(config, nullptr, nullptr);
    Options decline;
    Options plan;
    plan.assume_no = true;

    auto install = [&](std::initializer_list<const char *> stems, bool system = false, Options base = Options()) {
        std::vector<Deployer::File> files;
        for (const char *stem : stems) files.push_back(file(stem));
        base.allow_system = system;
        asked.clear();
        return deployer.install_files(files, base);
    };

    {
        const auto result = install({"org.openwave.test.hello-1.0.0"});
        CHECK_MSG(!result.ok && result.error.find("cancelled") != std::string::npos, "declining changes nothing: " + result.error);
        CHECK_MSG(asked.size() == 1, "one question for one application");
        if (!asked.empty()) {
            const auto &q = asked[0];
            CHECK_MSG(q.action == Confirmation::Action::Install, "the action is install");
            CHECK_MSG(q.name == "Hello", "the name is Application.toml's name, not the package name: " + q.name);
            CHECK_MSG(q.package == "org.openwave.test.hello" && q.version == "1.0.0", "package and version");
            CHECK_MSG(q.trust == Trust::Application, "signed by a trusted application key");
        }
    }
    {
        const auto result = install({"org.openwave.test.hello-2.0.0"});
        CHECK_MSG(asked.size() == 1 && has(asked.empty() ? std::vector<std::string>{} : asked[0].permissions, "files.read"),
                  "declared permissions are shown");
    }
    {
        const auto unsigned_result = install({"org.openwave.test.unsigned"});
        CHECK_MSG(unsigned_result.error.find("cancelled") != std::string::npos, "unsigned is not rejected: " + unsigned_result.error);
        CHECK_MSG(asked.size() == 1 && asked[0].trust == Trust::Untrusted && !asked[0].trust_reason.empty(),
                  "unsigned package is asked about as untrusted, with the reason");
        const auto untrusted_result = install({"org.openwave.test.untrusted"});
        CHECK_MSG(untrusted_result.error.find("cancelled") != std::string::npos, "an unknown signer is not rejected: " + untrusted_result.error);
        CHECK_MSG(asked.size() == 1 && asked[0].trust == Trust::Untrusted, "unknown signer is untrusted");
    }
    {
        const auto result = install({"org.openwave.test.tampered"});
        CHECK_MSG(!result.ok && result.error.find("[integrity]") != std::string::npos, "tampered package: " + result.error);
        CHECK_MSG(questions == 0 || asked.empty(), "a damaged package is not even asked about");
    }
    {
        std::vector<Deployer::File> files{file("org.openwave.test.hello-1.0.0")};
        const auto result = silent.install_files(files, decline);
        CHECK_MSG(!result.ok && result.error.find("--yes") != std::string::npos, "no way to ask: " + result.error);
    }
    {
        const auto alone = install({"org.openwave.test.viewer"}, false, plan);
        CHECK_MSG(!alone.ok, "the viewer alone cannot be installed: its library is nowhere");
        const auto both = install({"org.openwave.test.viewer", "org.openwave.test.lib"}, false, plan);
        CHECK_MSG(both.ok, both.error);
        CHECK_MSG(has(both.installed, "org.openwave.test.viewer") && has(both.installed, "org.openwave.test.lib"), "both are planned");
        asked.clear();
        std::vector<Deployer::File> pair{file("org.openwave.test.viewer"), file("org.openwave.test.lib")};
        const auto declined = two_questions.install_files(pair, decline);
        CHECK_MSG(!declined.ok && asked.size() == 2, "each package is asked about, until the person says no: " + std::to_string(asked.size()));
    }
    const std::pair<const char *, const char *> rejected[] = {
        {"org.openwave.test.scriptlet", "scriptlet"}, {"org.openwave.test.trigger", "scriptlet"}, {"org.openwave.test.filetrigger", "scriptlet"},
        {"org.openwave.test.symlink", "symlink"}, {"org.openwave.test.dotdot", "symlink"},
        {"org.openwave.test.outside", "location"}, {"org.openwave.test.suid", "mode"},
        {"org.openwave.test.manifest", "manifest"}, {"org.openwave.test.layout", "layout"}, {"org.openwave.test.noclass", "class"},
        {"org.openwave.test.badtoml", "toml"}, {"org.openwave.test.mismatch", "toml"},
        {"org.openwave.test.reserved", "toml"},
    };
    for (const auto &[name, rule] : rejected) {
        const std::string stem = name;
        const auto result = install({stem.c_str()}, false, plan);
        CHECK_MSG(!result.ok && result.error.find(std::string("[") + rule + "]") != std::string::npos,
                  stem + " should be rejected for " + rule + ": " + result.error);
        CHECK_MSG(asked.empty(), stem + " must be rejected before anyone is asked");
    }
    CHECK_MSG(!install({"org.openwave.test.scriptlet-sh"}, false, plan).ok, "shell scriptlet package");

    {
        const auto without = install({"openwave-test-sys-a"}, false, plan);
        CHECK_MSG(!without.ok && without.error.find("[authorization]") != std::string::npos, without.error);
        const auto with = install({"openwave-test-sys-a"}, true, plan);
        CHECK_MSG(with.ok, with.error);
        const auto bad_key = install({"openwave-test-sys-bad"}, true, plan);
        CHECK_MSG(!bad_key.ok && bad_key.error.find("[signature]") != std::string::npos, bad_key.error);
        const auto etc = install({"openwave-test-sys-etc"}, true, plan);
        CHECK_MSG(!etc.ok && etc.error.find("[protected-path]") != std::string::npos, etc.error);
        const auto both = install({"openwave-test-sys-a", "openwave-test-sys-b"}, true, plan);
        CHECK_MSG(!both.ok, "two packages with the same file cannot be planned together");
        install({"openwave-test-sys-a"}, true);
        CHECK_MSG(asked.size() == 1 && asked[0].klass == PackageClass::System && asked[0].trust == Trust::System, "system package question");
    }

    {
        char path[] = "/tmp/appdeploy-notrpm-XXXXXX";
        const int fd = ::mkstemp(path);
        const char junk[] = "this is not an rpm";
        (void)!::write(fd, junk, sizeof(junk));
        std::vector<Deployer::File> files{{fd, "junk.rpm"}};
        bool refused = false;
        try { refused = !deployer.install_files(files, plan).ok; } catch (const Error &) { refused = true; }
        CHECK_MSG(refused, "a file that is not an RPM is refused");
        ::close(fd);
        ::unlink(path);
        bool bad_count = false;
        try { deployer.install_files({}, plan); } catch (const Error &) { bad_count = true; }
        CHECK_MSG(bad_count, "no files is refused");
    }
    {
        CHECK_MSG(deployer.list_installed().empty(), "nothing is installed in a fresh root");
        const auto missing = deployer.remove({"org.openwave.test.hello"}, plan);
        CHECK_MSG(!missing.ok, "removing what is not installed fails");
    }
    for (const char *bad : {"--force", "a b", "x;y", "$(id)", ""}) {
        bool refused = false;
        try { deployer.remove({bad}, plan); } catch (const Error &) { refused = true; }
        CHECK_MSG(refused, std::string("accepted package name: ") + bad);
    }

    std::string command = std::string("rm -rf ") + scratch;
    (void)!std::system(command.c_str());
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
