#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace appdeploy {

class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct Config {
    std::string root;
    std::string applications = "/Applications";
    std::vector<std::string> system_roots = {"/opt", "/usr/local"};
    std::vector<std::string> protected_paths = {
        "/boot", "/dev", "/proc", "/sys", "/run", "/tmp", "/var/lib/appdeploy", "/var/cache/appdeploy",
        "/etc/appdeploy", "/etc/selinux", "/etc/init.d", "/etc/runlevels", "/lib/modules", "/usr/lib/modules",
        "/System", "/Data", "/Applications", "/usr/lib/rpm"};
    std::string keys_dir = "/etc/appdeploy/keys";
    std::string state_dir = "/var/lib/appdeploy";
    std::string cache_dir = "/var/cache/appdeploy";
    std::string users_dir = "/Data/Users";
    std::string selinux_file_contexts;
    std::uint32_t owner_uid = 0;
    std::uint64_t max_package_bytes = 512ull << 20;
    std::uint64_t max_installed_bytes = 2048ull << 20;
    std::size_t max_files = 100000;

    std::string path(const std::string &absolute) const { return root + absolute; }
};

enum class Trust {
    Untrusted,
    Application,
    System,
    Publisher,
};

inline constexpr const char *kPublisherKeyPath = "share/openwave/publisher.asc";
const char *trust_name(Trust trust);

class KeyStore {
public:
    explicit KeyStore(const Config &config);
    ~KeyStore();
    KeyStore(const KeyStore &) = delete;
    KeyStore &operator=(const KeyStore &) = delete;

    struct Key {
        std::string fingerprint;
        std::string id;
        Trust klass;
        std::string source;
    };
    const std::vector<Key> &keys() const;

    struct Verdict {
        Trust trust = Trust::Untrusted;
        bool intact = false;
        std::string reason;
        std::string signer_key_id;
        std::string publisher_name;
        std::string publisher_fingerprint;
        std::string publisher_key;
        bool pinned = false;
        bool publisher_mismatch = false;
    };
    Verdict verify(int fd, const std::string &name) const;
    Verdict verify_package(int fd, const std::string &name, const std::string &application_id,
                           const std::string &embedded_key) const;
    void pin_publisher(const std::string &application_id, const std::string &armored_key) const;
    void unpin_publisher(const std::string &application_id) const;


private:
    struct Impl;
    std::unique_ptr<Impl> p;
};

enum class PackageClass { Application, System };
const char *class_name(PackageClass klass);

struct FileEntry {
    std::string path;
    std::uint32_t mode = 0;
    std::string user, group;
    std::string link;
    std::string caps;
    std::uint32_t flags = 0;
    std::uint32_t nlink = 1;
    std::uint64_t size = 0;
    bool directory() const { return (mode & 0170000) == 0040000; }
    bool regular() const { return (mode & 0170000) == 0100000; }
    bool symlink() const { return (mode & 0170000) == 0120000; }
};

struct PackageInfo {
    std::string name, version, release, arch, os, summary, license, source_rpm;
    std::uint32_t epoch = 0;
    bool has_epoch = false;
    std::vector<std::string> provides, requires_, conflicts, obsoletes;
    std::vector<FileEntry> files;
    std::vector<std::string> scripts;
    std::string payload_format, payload_compressor;
    bool relocatable = false;
    std::uint64_t installed_size = 0;
    std::optional<PackageClass> klass;
    std::string application_id;
    std::string nevra() const;
};

PackageInfo read_package(int fd, const std::string &name);
std::optional<std::string> read_payload_file(int fd, const std::string &name, const std::string &path,
                                             std::size_t limit);

struct Violation {
    std::string rule;
    std::string message;
};

void classify(PackageInfo &info);

class Policy {
public:
    explicit Policy(const Config &config) : config_(config) {}
    std::vector<Violation> evaluate(const PackageInfo &info, Trust trust) const;
    std::vector<Violation> declaration(const PackageInfo &info, const std::optional<std::string> &toml) const;

private:
    const Config &config_;
};

struct ApplicationDescriptor {
    std::string id, name, version, kind;
    std::string executable, icon;
    std::vector<std::string> arguments, permissions, opens;
    std::vector<std::string> mime_types;
    std::string developer;
    bool single_instance = true, accepts_paths = false;
    std::string runtime = "openwave-qt", profile = "standard";
    std::vector<std::string> requires_;

    static ApplicationDescriptor parse(const std::string &text, const std::string &expected_id);
    std::string manifest_json() const;
    bool launchable() const { return kind == "application"; }
};

class Registry {
public:
    explicit Registry(const Config &config) : config_(config) {}
    std::string bundle_dir(const std::string &id) const;
    std::string manifest_path(const std::string &id) const;
    std::string toml_path(const std::string &id) const;
    bool register_bundle(const std::string &id, const std::string &expected_version);
    void unregister_bundle(const std::string &id);
    bool registered(const std::string &id) const;
    enum class State { Missing, Current, Stale, Invalid };
    State state(const std::string &id, const std::string &expected_version, std::string *detail = nullptr) const;

private:
    const Config &config_;
};

class Labeler {
public:
    explicit Labeler(const Config &config);
    ~Labeler();
    Labeler(const Labeler &) = delete;
    Labeler &operator=(const Labeler &) = delete;
    bool enabled() const;
    void label(const std::string &absolute_path, std::uint32_t mode) const;
    std::string expected(const std::string &absolute_path, std::uint32_t mode) const;
    std::string current(const std::string &absolute_path) const;
    void label_tree(const std::string &absolute_dir) const;

private:
    struct Impl;
    std::unique_ptr<Impl> p;
};

struct Progress {
    enum class Kind { Info, Warning, Download, Install, Remove, Verify, Done } kind = Kind::Info;
    std::string text;
    std::uint64_t current = 0, total = 0;
};
using ProgressSink = std::function<void(const Progress &)>;

struct Confirmation {
    enum class Action { Install, Update, Remove } action = Action::Install;
    std::string name;
    std::string version, old_version;
    std::string package;
    PackageClass klass = PackageClass::Application;
    Trust trust = Trust::Untrusted;
    std::string trust_reason;
    std::vector<std::string> permissions;
};
using ConfirmFn = std::function<bool(const Confirmation &)>;

struct Options {
    bool allow_system = false;
    bool purge_data = false;
    bool assume_no = false;
    bool assume_yes = false;
    bool allow_downgrade = false;
};

struct PackageRow {
    std::string name, version, release, arch, summary;
    std::optional<PackageClass> klass;
    std::string application_id;
};

struct TransactionResult {
    bool ok = false;
    std::vector<std::string> installed, removed, upgraded;
    std::string error;
    std::vector<std::string> notes;
};

class Deployer {
public:
    Deployer(Config config, ProgressSink sink, ConfirmFn confirm = nullptr);
    ~Deployer();
    Deployer(const Deployer &) = delete;
    Deployer &operator=(const Deployer &) = delete;

    struct File {
        int fd;
        std::string display_name;
    };
    TransactionResult install_files(const std::vector<File> &files, const Options &options);
    TransactionResult remove(const std::vector<std::string> &specs, const Options &options);

    std::vector<PackageRow> list_installed();
    std::optional<PackageRow> info(const std::string &spec, std::vector<std::string> *detail = nullptr);
    struct VerifyReport {
        bool ok = true;
        std::vector<std::string> problems;
    };
    VerifyReport verify(const std::string &spec);

    TransactionResult reconcile();

private:
    struct Impl;
    std::unique_ptr<Impl> p;
};

bool valid_application_id(const std::string &id);
bool valid_package_spec(const std::string &spec);

}
