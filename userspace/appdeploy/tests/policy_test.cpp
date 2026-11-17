#include "appdeploy/appdeploy.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

using namespace appdeploy;

static int failures = 0, checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { ++failures; std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (0)
#define CHECK_MSG(condition, message) do { ++checks; if (!(condition)) { ++failures; std::fprintf(stderr, "FAIL %s:%d: %s -- %s\n", __FILE__, __LINE__, #condition, std::string(message).c_str()); } } while (0)

static std::string packages_dir;

struct Loaded {
    PackageInfo info;
    KeyStore::Verdict verdict;
    std::optional<std::string> toml;
    std::vector<Violation> violations, declaration;
    std::set<std::string> rules() const
    {
        std::set<std::string> out;
        for (const auto &v : violations) out.insert(v.rule);
        for (const auto &v : declaration) out.insert(v.rule);
        return out;
    }
};

static Loaded load(const Config &config, const KeyStore &keys, const std::string &file)
{
    Loaded loaded;
    const std::string path = packages_dir + "/" + file;
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) { std::fprintf(stderr, "cannot open %s\n", path.c_str()); std::exit(2); }
    loaded.verdict = keys.verify(fd, file);
    loaded.info = read_package(fd, file);
    classify(loaded.info);
    if (loaded.info.klass == PackageClass::Application)
        loaded.toml = read_payload_file(fd, file, config.applications + "/" + loaded.info.name + "/Application.toml", 65536);
    Policy policy(config);
    try { loaded.violations = policy.evaluate(loaded.info, loaded.verdict.trust); }
    catch (const std::exception &error) { std::fprintf(stderr, "%s: policy threw: %s\n", file.c_str(), error.what()); std::exit(2); }
    loaded.declaration = policy.declaration(loaded.info, loaded.toml);
    ::close(fd);
    return loaded;
}

static std::string dump(const Loaded &l)
{
    std::string out = std::string("trust=") + trust_name(l.verdict.trust);
    for (const auto &v : l.violations) out += " [" + v.rule + ": " + v.message + "]";
    for (const auto &v : l.declaration) out += " {" + v.rule + ": " + v.message + "}";
    return out;
}

static void expect_clean(const Config &config, const KeyStore &keys, const std::string &file, Trust trust)
{
    const Loaded l = load(config, keys, file);
    CHECK_MSG(l.verdict.trust == trust, file + " " + dump(l));
    CHECK_MSG(l.violations.empty() && l.declaration.empty(), file + " " + dump(l));
}

static void expect_rule(const Config &config, const KeyStore &keys, const std::string &file, const std::string &rule)
{
    const Loaded l = load(config, keys, file);
    CHECK_MSG(l.rules().count(rule) == 1, file + " should violate " + rule + ": " + dump(l));
}

int main(int argc, char **argv)
{
    if (argc != 3 && argc != 4) { std::fprintf(stderr, "usage: policy_test KEYS PACKAGES [PUBLISHER-KEYS]\n"); return 2; }
    Config config;
    config.keys_dir = argv[1];
    config.owner_uid = ::getuid();
    packages_dir = argv[2];
    KeyStore keys(config);
    CHECK(keys.keys().size() == 2);

    expect_clean(config, keys, "org.openwave.test.hello-1.0.0-1.x86_64.rpm", Trust::Application);
    expect_clean(config, keys, "org.openwave.test.hello-2.0.0-1.x86_64.rpm", Trust::Application);
    expect_clean(config, keys, "org.openwave.test.lib-1.0.0-1.x86_64.rpm", Trust::Application);
    expect_clean(config, keys, "org.openwave.test.viewer-1.0.0-1.x86_64.rpm", Trust::Application);
    expect_clean(config, keys, "org.openwave.test.cleanup-1.0.0-1.x86_64.rpm", Trust::Application);
    expect_clean(config, keys, "org.openwave.test.bundle-1.0.0-1.x86_64.rpm", Trust::Application);
    expect_clean(config, keys, "openwave-test-sys-a-1.0.0-1.x86_64.rpm", Trust::System);
    expect_clean(config, keys, "openwave-test-sys-b-1.0.0-1.x86_64.rpm", Trust::System);
    expect_clean(config, keys, "openwave-test-sys-unowned-1.0.0-1.x86_64.rpm", Trust::System);

    {
        const Loaded hello = load(config, keys, "org.openwave.test.hello-1.0.0-1.x86_64.rpm");
        CHECK(hello.info.klass == PackageClass::Application);
        CHECK(hello.info.application_id == "org.openwave.test.hello");
        CHECK(hello.info.version == "1.0.0");
        CHECK(hello.info.scripts.empty());
        CHECK(hello.toml.has_value());
        const Loaded sys = load(config, keys, "openwave-test-sys-a-1.0.0-1.x86_64.rpm");
        CHECK(sys.info.klass == PackageClass::System);
        const Loaded viewer = load(config, keys, "org.openwave.test.viewer-1.0.0-1.x86_64.rpm");
        CHECK_MSG(std::any_of(viewer.info.requires_.begin(), viewer.info.requires_.end(),
                              [](const std::string &r) { return r.find("org.openwave.test.lib") == 0; }), "viewer requires the library");
    }

    expect_rule(config, keys, "openwave-test-sys-bad-1.0.0-1.x86_64.rpm", "signature");
    expect_rule(config, keys, "openwave-test-sys-etc-1.0.0-1.x86_64.rpm", "protected-path");
    expect_rule(config, keys, "org.openwave.test.badtoml-1.0.0-1.x86_64.rpm", "toml");
    expect_rule(config, keys, "org.openwave.test.mismatch-1.0.0-1.x86_64.rpm", "toml");
    expect_rule(config, keys, "org.openwave.test.scriptlet-1.0.0-1.x86_64.rpm", "scriptlet");
    expect_rule(config, keys, "org.openwave.test.trigger-1.0.0-1.x86_64.rpm", "scriptlet");
    expect_rule(config, keys, "org.openwave.test.filetrigger-1.0.0-1.x86_64.rpm", "scriptlet");
    expect_rule(config, keys, "org.openwave.test.scriptlet-sh-1.0.0-1.x86_64.rpm", "scriptlet");
    expect_rule(config, keys, "org.openwave.test.symlink-1.0.0-1.x86_64.rpm", "symlink");
    expect_rule(config, keys, "org.openwave.test.dotdot-1.0.0-1.x86_64.rpm", "symlink");
    expect_rule(config, keys, "org.openwave.test.outside-1.0.0-1.x86_64.rpm", "location");
    expect_rule(config, keys, "org.openwave.test.suid-1.0.0-1.x86_64.rpm", "mode");
    expect_rule(config, keys, "org.openwave.test.manifest-1.0.0-1.x86_64.rpm", "manifest");
    expect_rule(config, keys, "org.openwave.test.noclass-1.0.0-1.x86_64.rpm", "class");
    expect_rule(config, keys, "org.openwave.test.layout-1.0.0-1.x86_64.rpm", "layout");
    expect_rule(config, keys, "org.openwave.test.reserved-1.0.0-1.x86_64.rpm", "toml");
    expect_clean(config, keys, "org.openwave.test.untrusted-1.0.0-1.x86_64.rpm", Trust::Untrusted);
    expect_clean(config, keys, "org.openwave.test.unsigned-1.0.0-1.x86_64.rpm", Trust::Untrusted);
    {
        const Loaded unsigned_package = load(config, keys, "org.openwave.test.unsigned-1.0.0-1.x86_64.rpm");
        CHECK_MSG(unsigned_package.verdict.intact && !unsigned_package.verdict.reason.empty(), dump(unsigned_package));
        const Loaded unknown_signer = load(config, keys, "org.openwave.test.untrusted-1.0.0-1.x86_64.rpm");
        CHECK_MSG(unknown_signer.verdict.intact && !unknown_signer.verdict.reason.empty(), dump(unknown_signer));
    }
    {
        const Loaded tampered = load(config, keys, "org.openwave.test.tampered-1.0.0-1.x86_64.rpm");
        CHECK_MSG(tampered.verdict.trust == Trust::Untrusted, dump(tampered));
        CHECK_MSG(!tampered.verdict.intact, dump(tampered));
        CHECK(!tampered.verdict.reason.empty());
    }

    {
        const Loaded hello = load(config, keys, "org.openwave.test.hello-2.0.0-1.x86_64.rpm");
        const auto descriptor = ApplicationDescriptor::parse(*hello.toml, "org.openwave.test.hello");
        CHECK(descriptor.version == "2.0.0");
        CHECK(descriptor.permissions == std::vector<std::string>{"files.read"});
        const std::string manifest = descriptor.manifest_json();
        CHECK(manifest.find("\"schemaVersion\": 1") != std::string::npos);
        CHECK(manifest.find("\"executable\": \"bin/hello\"") != std::string::npos);
        CHECK(manifest.find("\"permissions\": [\"files.read\"]") != std::string::npos);
        CHECK(manifest.find("\"type\": \"application\"") != std::string::npos);
        CHECK(descriptor.manifest_json() == manifest);
        for (const char *bad : {
                 "schema = 2\nid = \"a.b.c\"\nname = \"x\"\nversion = \"1\"\nexecutable = \"bin/x\"\n",
                 "schema = 1\nid = \"org.x.y\"\nname = \"x\"\nversion = \"1\"\nexecutable = \"/bin/sh\"\n",
                 "schema = 1\nid = \"org.x.y\"\nname = \"x\"\nversion = \"1\"\nexecutable = \"bin/../x\"\n",
                 "schema = 1\nid = \"org.x.y\"\nname = \"x\"\nversion = \"1\"\nexecutable = \"bin/x\"\nsurprise = 1\n",
                 "schema = 1\nid = \"org.x.y\"\nname = \"x\"\nversion = \"1\"\nexecutable = \"bin/x\"\nruntime = \"sh\"\n",
                 "schema = 1\nid = \"org.x.y\"\nname = \"x\"\nversion = \"1\"\nexecutable = \"bin/x\"\n[sandbox]\nprofile = \"none\"\n",
                 "schema = 1\nid = \"org.x.y\"\nname = \"x\"\nversion = \"1\"\nexecutable = \"bin/x\"\n[sandbox]\nrequires = [\"bubblewrap\"]\n",
                 "schema = 1\nid = \"com.openwave.y\"\nname = \"x\"\nversion = \"1\"\nexecutable = \"bin/x\"\n",
                 "schema = 1\nid = \"org.x.y\"\nname = \"x\"\nversion = \"1\"\nexecutable = \"bin/x\"\n[permissions]\nrequest = [\"a b\"]\n",
                 "this is not toml ===",
             }) {
            bool rejected = false;
            try { ApplicationDescriptor::parse(bad, "org.x.y"); } catch (const Error &) { rejected = true; }
            CHECK_MSG(rejected, std::string("accepted: ") + bad);
        }
        const auto tricky = ApplicationDescriptor::parse(
            "schema = 1\nid = \"org.x.y\"\nname = \"a\\\"b\\\\c\"\nversion = \"1\"\nexecutable = \"bin/x\"\n", "org.x.y");
        CHECK(tricky.manifest_json().find("\"name\": \"a\\\"b\\\\c\"") != std::string::npos);
        const std::string header = "schema = 1\nid = \"org.x.y\"\nname = \"x\"\nversion = \"1\"\nexecutable = \"bin/x\"\n";
        const auto opener = ApplicationDescriptor::parse(header + "accepts_paths = true\nopens = [\"docx\", \"xlsx\"]\n", "org.x.y");
        CHECK((opener.opens == std::vector<std::string>{"docx", "xlsx"}));
        CHECK(opener.manifest_json().find("\"opens\": [\"docx\", \"xlsx\"]") != std::string::npos);
        CHECK(tricky.manifest_json().find("\"opens\"") == std::string::npos);
        const auto mime = ApplicationDescriptor::parse(header + "accepts_paths = true\nmime_types = [\"application/pdf\", \"application/x-openwave-test\"]\ndeveloper = \"Test Publisher\"\n", "org.x.y");
        CHECK(mime.mime_types.size() == 2);
        CHECK(mime.manifest_json().find("\"mimeTypes\": [\"application/pdf\"") != std::string::npos);
        CHECK(mime.manifest_json().find("\"developer\": \"Test Publisher\"") != std::string::npos);
        for (const char *bad : {"mime_types = [\"application/pdf\"]\n", "accepts_paths = true\nmime_types = [\"image/*\"]\n",
                               "accepts_paths = true\nmime_types = [\"image/png\", \"image/png\"]\n",
                               "accepts_paths = true\nmime_types = [\"Text/Plain\"]\n", "mime_types = 3\n"}) {
            bool rejected = false;
            try { ApplicationDescriptor::parse(header + bad, "org.x.y"); } catch (const Error &) { rejected = true; }
            CHECK(rejected);
        }
        for (const char *bad : {"opens = [\"docx\"]\n", "accepts_paths = true\nopens = [\"DOCX\"]\n",
                                "accepts_paths = true\nopens = [\"docx\", \"docx\"]\n",
                                "accepts_paths = true\nopens = [\"do.cx\"]\n",
                                "accepts_paths = true\nopens = [\"\"]\n",
                                "accepts_paths = true\nopens = \"docx\"\n"}) {
            bool rejected = false;
            try { ApplicationDescriptor::parse(header + bad, "org.x.y"); } catch (const Error &) { rejected = true; }
            CHECK_MSG(rejected, std::string("accepted: ") + bad);
        }
    }

    CHECK(valid_application_id("org.example.viewer"));
    CHECK(!valid_application_id("viewer"));
    CHECK(!valid_application_id("org..viewer"));
    CHECK(!valid_application_id("org.example.view er"));
    CHECK(valid_package_spec("org.openwave.test.hello"));
    CHECK(!valid_package_spec("foo bar"));
    CHECK(!valid_package_spec("--force"));
    CHECK(!valid_package_spec("a;b"));

    {
        char scratch[] = "/tmp/appdeploy-registry-XXXXXX";
        CHECK(::mkdtemp(scratch) != nullptr);
        Config tree = config;
        tree.root = scratch;
        Registry registry(tree);
        const std::string app_dir = std::string(scratch) + "/Applications";
        const std::string bundle = app_dir + "/org.openwave.test.hello";
        ::mkdir(app_dir.c_str(), 0755);
        ::mkdir(bundle.c_str(), 0755);
        ::mkdir((bundle + "/bin").c_str(), 0755);
        const Loaded hello = load(config, keys, "org.openwave.test.hello-1.0.0-1.x86_64.rpm");
        { std::ofstream(bundle + "/Application.toml") << *hello.toml; }
        { std::ofstream(bundle + "/bin/hello") << "x"; ::chmod((bundle + "/bin/hello").c_str(), 0755); }
        ::chmod((bundle + "/Application.toml").c_str(), 0644);
        CHECK(registry.state("org.openwave.test.hello", "1.0.0") == Registry::State::Missing);
        CHECK(registry.register_bundle("org.openwave.test.hello", "1.0.0"));
        CHECK(registry.registered("org.openwave.test.hello"));
        CHECK(registry.state("org.openwave.test.hello", "1.0.0") == Registry::State::Current);
        registry.unregister_bundle("org.openwave.test.hello");
        bool refused = false;
        try { registry.register_bundle("org.openwave.test.hello", "9.9.9"); } catch (const Error &) { refused = true; }
        CHECK(refused);
        CHECK(!registry.registered("org.openwave.test.hello"));
        CHECK(registry.register_bundle("org.openwave.test.hello", "1.0.0"));
        { std::ofstream(bundle + "/manifest.json") << "{}"; }
        CHECK(registry.state("org.openwave.test.hello", "1.0.0") == Registry::State::Stale);
        ::symlink(bundle.c_str(), (app_dir + "/org.openwave.test.link").c_str());
        bool linked = false;
        try { registry.register_bundle("org.openwave.test.link", "1.0.0"); } catch (const Error &) { linked = true; }
        CHECK(linked);
        ::chmod(bundle.c_str(), 0775);
        bool writable = false;
        try { registry.register_bundle("org.openwave.test.hello", "1.0.0"); } catch (const Error &) { writable = true; }
        CHECK(writable);
        ::chmod(bundle.c_str(), 0755);
        ::unlink((bundle + "/bin/hello").c_str());
        bool missing = false;
        try { registry.register_bundle("org.openwave.test.hello", "1.0.0"); } catch (const Error &) { missing = true; }
        CHECK(missing);
        std::string command = std::string("rm -rf ") + scratch;
        (void)!std::system(command.c_str());
    }

    {
        const std::string sources = argc > 3 ? argv[3] : std::string(argv[1]);
        auto read_file = [](const std::string &path) {
            std::ifstream stream(path);
            return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        };
        const std::string untrusted_key = read_file(sources + "/untrusted/untrusted.asc");
        const std::string rival_key = read_file(sources + "/rival/rival.asc");
        if (!untrusted_key.empty() && !rival_key.empty()) {
            char directory[] = "/tmp/appdeploy-pins-XXXXXX";
            CHECK(::mkdtemp(directory) != nullptr);
            Config pinned = config;
            pinned.state_dir = directory;
            KeyStore store(pinned);
            const std::string id = "org.openwave.test.pub";
            auto judge = [&](const std::string &file, const std::string &application, const std::string &embedded) {
                const int fd = ::open((packages_dir + "/" + file).c_str(), O_RDONLY | O_CLOEXEC);
                if (fd < 0) { std::fprintf(stderr, "cannot open %s\n", file.c_str()); std::exit(2); }
                KeyStore::Verdict verdict = store.verify_package(fd, file, application, embedded);
                ::close(fd);
                return verdict;
            };
            const std::string first = "org.openwave.test.pub-1.0.0-1.x86_64.rpm", second = "org.openwave.test.pub-2.0.0-1.x86_64.rpm",
                              rival = "org.openwave.test.pub-3.0.0-1.x86_64.rpm";

            KeyStore::Verdict verdict = judge(first, id, untrusted_key);
            CHECK(verdict.trust == Trust::Publisher && verdict.intact && !verdict.pinned && !verdict.publisher_mismatch);
            CHECK_MSG(verdict.publisher_name.find("Untrusted Key") != std::string::npos, verdict.publisher_name);
            CHECK(verdict.publisher_fingerprint.size() == 16);
            CHECK(verdict.publisher_key == untrusted_key);
            CHECK_MSG(verdict.reason.find("first install") != std::string::npos, verdict.reason);

            verdict = judge(first, id, "");
            CHECK(verdict.trust == Trust::Untrusted && verdict.intact);
            verdict = judge(first, id, "not a key");
            CHECK(verdict.trust == Trust::Untrusted && verdict.intact);
            verdict = judge(first, id, rival_key);
            CHECK(verdict.trust == Trust::Untrusted && verdict.intact);
            verdict = judge("org.openwave.test.pubmismatch-1.0.0-1.x86_64.rpm", "org.openwave.test.pubmismatch", untrusted_key);
            CHECK(verdict.trust == Trust::Untrusted && verdict.intact);
            verdict = judge("org.openwave.test.pubadmin-1.0.0-1.x86_64.rpm", "org.openwave.test.pubadmin", untrusted_key);
            CHECK(verdict.trust == Trust::Application && !verdict.pinned);
            verdict = judge("org.openwave.test.tampered-1.0.0-1.x86_64.rpm", id, untrusted_key);
            CHECK(!verdict.intact && verdict.trust == Trust::Untrusted);

            store.pin_publisher(id, untrusted_key);
            verdict = judge(second, id, untrusted_key);
            CHECK(verdict.trust == Trust::Application && verdict.pinned && verdict.intact);
            CHECK(verdict.publisher_name.find("Untrusted Key") != std::string::npos);
            verdict = judge(second, id, "");
            CHECK(verdict.trust == Trust::Application && verdict.pinned);
            verdict = judge(rival, id, rival_key);
            CHECK(verdict.publisher_mismatch && verdict.trust == Trust::Untrusted && verdict.intact);
            CHECK_MSG(verdict.reason.find("not signed by that key") != std::string::npos, verdict.reason);
            verdict = judge(rival, "org.openwave.test.other", rival_key);
            CHECK(verdict.trust == Trust::Publisher && !verdict.publisher_mismatch);

            struct stat pin;
            const std::string pin_file = std::string(directory) + "/publishers/" + id + ".asc";
            CHECK(::stat(pin_file.c_str(), &pin) == 0 && (pin.st_mode & 0777) == 0644);

            store.unpin_publisher(id);
            CHECK(::stat(pin_file.c_str(), &pin) != 0);
            verdict = judge(rival, id, rival_key);
            CHECK(verdict.trust == Trust::Publisher && !verdict.publisher_mismatch);

            bool refused = false;
            try { store.pin_publisher(id, "garbage"); } catch (const Error &) { refused = true; }
            CHECK(refused);
            refused = false;
            try { store.pin_publisher("../escape", untrusted_key); } catch (const Error &) { refused = true; }
            CHECK(refused);

            { std::ofstream(pin_file) << untrusted_key; }
            ::chmod(pin_file.c_str(), 0666);
            refused = false;
            try { judge(second, id, untrusted_key); } catch (const Error &) { refused = true; }
            CHECK(refused);

            std::string command = std::string("rm -rf ") + directory;
            (void)!std::system(command.c_str());
        }
    }

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
