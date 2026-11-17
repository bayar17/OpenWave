#include "appdeploy/appdeploy.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace appdeploy;

static int check(const Config &config, const KeyStore &keys, const std::string &path)
{
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        std::fprintf(stderr, "%s: cannot open: %s\n", path.c_str(), std::strerror(errno));
        return 2;
    }
    const std::string name = std::filesystem::path(path).filename().string();
    int status = 0;
    try {
        PackageInfo info = read_package(fd, name);
        classify(info);
        std::string embedded;
        if (info.klass && *info.klass == PackageClass::Application) {
            try {
                embedded = read_payload_file(fd, name, config.applications + "/" + info.name + "/" + kPublisherKeyPath, 65536).value_or("");
            } catch (const Error &) {}
        }
        const auto verdict = keys.verify_package(fd, name, info.name, embedded);
        std::optional<std::string> toml;
        std::vector<Violation> problems;
        if (!info.klass || *info.klass != PackageClass::Application) {
            problems.push_back({"class", "this is not an application package"});
        } else {
            toml = read_payload_file(fd, name, config.applications + "/" + info.name + "/Application.toml", 65536);
            Policy policy(config);
            problems = policy.evaluate(info, verdict.trust);
            for (auto &item : policy.declaration(info, toml)) problems.push_back(std::move(item));
        }
        std::printf("package   %s %s-%s\n", info.name.c_str(), info.version.c_str(), info.release.c_str());
        std::printf("signature %s%s%s\n", trust_name(verdict.trust), verdict.intact ? "" : " (digests do not match)",
                    verdict.reason.empty() ? "" : (" - " + verdict.reason).c_str());
        if (!verdict.intact) status = 1;
        for (const auto &problem : problems) {
            std::printf("violation [%s] %s\n", problem.rule.c_str(), problem.message.c_str());
            status = 1;
        }
        std::printf("%s\n", status ? "FAIL" : "PASS");
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s: %s\n", path.c_str(), error.what());
        status = 2;
    }
    ::close(fd);
    return status;
}

int main(int argc, char **argv)
{
    Config config;
    config.owner_uid = ::getuid();
    std::vector<std::string> packages;
    std::string keys;
    for (int index = 1; index < argc; ++index) {
        if (!std::strcmp(argv[index], "--keys") && index + 1 < argc) keys = argv[++index];
        else packages.push_back(argv[index]);
    }
    if (packages.empty()) {
        std::fprintf(stderr, "usage: appdeploy-check [--keys DIRECTORY] PACKAGE.rpm...\n");
        return 2;
    }
    if (keys.empty()) {
        keys = std::filesystem::temp_directory_path().string() + "/appdeploy-check-keys-" + std::to_string(::getpid());
        std::filesystem::create_directories(keys + "/application");
        std::filesystem::create_directories(keys + "/system");
        for (const char *directory : {"", "/application", "/system"})
            std::filesystem::permissions(keys + directory, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace);
    }
    config.keys_dir = keys;
    int worst = 0;
    try {
        KeyStore store(config);
        for (const auto &package : packages) worst = std::max(worst, check(config, store, package));
    } catch (const std::exception &error) {
        std::fprintf(stderr, "appdeploy-check: %s\n", error.what());
        worst = 2;
    }
    return worst;
}
