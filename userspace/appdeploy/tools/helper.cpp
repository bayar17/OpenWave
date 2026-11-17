#include "appdeploy/appdeploy.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace {

std::string clean(const std::string &text)
{
    std::string out;
    for (unsigned char c : text) out += (c < 0x20 || c == 0x7f) ? ' ' : char(c);
    return out;
}

void emit(const std::string &line)
{
    std::fputs(line.c_str(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

const char *kind_name(appdeploy::Progress::Kind kind)
{
    using K = appdeploy::Progress::Kind;
    switch (kind) {
    case K::Download: return "download";
    case K::Install: return "install";
    case K::Remove: return "remove";
    case K::Verify: return "verify";
    case K::Warning: return "warning";
    case K::Done: return "done";
    default: return "info";
    }
}

int usage(const char *why)
{
    emit(std::string("E\t") + why);
    emit("FAIL");
    return 2;
}

void report(const appdeploy::TransactionResult &result)
{
    for (const auto &name : result.installed) emit("R\tinstalled " + clean(name));
    for (const auto &name : result.upgraded) emit("R\tupgraded " + clean(name));
    for (const auto &name : result.removed) emit("R\tremoved " + clean(name));
    for (const auto &note : result.notes) emit("R\t" + clean(note));
    if (!result.ok) {
        std::string line;
        for (char c : result.error) {
            if (c == '\n') { emit("E\t" + clean(line)); line.clear(); }
            else line += c;
        }
        if (!line.empty()) emit("E\t" + clean(line));
    }
}

void row(const appdeploy::PackageRow &package)
{
    emit("ROW\t" + clean(package.name) + "\t" + clean(package.version) + "\t" + clean(package.release) + "\t"
         + clean(package.arch) + "\t" + (package.klass ? appdeploy::class_name(*package.klass) : "unmanaged") + "\t"
         + clean(package.summary));
}

bool ask(const appdeploy::Confirmation &c)
{
    using A = appdeploy::Confirmation::Action;
    std::string permissions;
    for (const auto &item : c.permissions) permissions += (permissions.empty() ? "" : ",") + clean(item);
    const char *trust = appdeploy::trust_name(c.trust);
    emit(std::string("Q\t") + (c.action == A::Install ? "install" : c.action == A::Update ? "update" : "remove") + "\t"
         + clean(c.name) + "\t" + clean(c.version) + "\t" + clean(c.old_version) + "\t" + clean(c.package) + "\t"
         + appdeploy::class_name(c.klass) + "\t" + trust + "\t" + clean(c.trust_reason) + "\t" + permissions);
    std::string answer;
    if (!std::getline(std::cin, answer)) return false;
    return answer == "Y";
}

}

int main(int argc, char **argv)
{
    std::vector<std::string> args(argv + 1, argv + argc);
    appdeploy::Config config;
    appdeploy::Options options;
    if (!args.empty() && args[0] == "--test-root") {
        if (geteuid() == 0 || args.size() < 3) return usage("--test-root is not available here");
        config.root = args[1];
        args.erase(args.begin(), args.begin() + 2);
    }
    if (args.empty()) return usage("no operation");
    const std::string verb = args[0];
    args.erase(args.begin());
    std::vector<std::string> operands;
    for (const auto &argument : args) {
        if (argument == "--system") options.allow_system = true;
        else if (argument == "--purge-data") options.purge_data = true;
        else if (argument == "--plan") options.assume_no = true;
        else if (argument == "--yes") options.assume_yes = true;
        else if (argument == "--allow-downgrade") options.allow_downgrade = true;
        else if (argument.rfind("--", 0) == 0) return usage("unknown option");
        else operands.push_back(argument);
    }
    if (config.root.empty() && geteuid() != 0 && verb != "verify" && verb != "list" && verb != "info")
        return usage("the helper must run with root privileges");

    appdeploy::ProgressSink sink = [](const appdeploy::Progress &progress) {
        emit(std::string("P\t") + kind_name(progress.kind) + "\t" + std::to_string(progress.current) + "\t"
             + std::to_string(progress.total) + "\t" + clean(progress.text));
    };
    auto finish = [](const appdeploy::TransactionResult &result) {
        report(result);
        emit(result.ok ? "OK" : "FAIL");
        return result.ok ? 0 : 1;
    };
    try {
        appdeploy::Deployer deployer(config, sink, ask);
        if (verb == "install-file") {
            if (operands.empty() || operands.size() > 16) return usage("install-file needs one to sixteen package files");
            std::vector<appdeploy::Deployer::File> files;
            for (std::size_t i = 0; i < operands.size(); ++i) {
                struct stat st;
                if (fstat(3 + int(i), &st) || !S_ISREG(st.st_mode)) return usage("a package descriptor is missing");
                files.push_back({3 + int(i), operands[i]});
            }
            return finish(deployer.install_files(files, options));
        }
        if (verb == "remove") {
            if (operands.empty()) return usage("no package given");
            return finish(deployer.remove(operands, options));
        }
        if (verb == "reconcile") return finish(deployer.reconcile());
        if (verb == "list") {
            for (const auto &package : deployer.list_installed()) row(package);
            emit("OK");
            return 0;
        }
        if (verb == "info") {
            if (operands.size() != 1) return usage("info needs one package");
            std::vector<std::string> detail;
            const auto found = deployer.info(operands[0], &detail);
            if (!found) { emit("E\tno package named " + clean(operands[0])); emit("FAIL"); return 1; }
            for (const auto &line : detail) emit("I\t" + clean(line));
            emit("OK");
            return 0;
        }
        if (verb == "verify") {
            if (operands.size() != 1) return usage("verify needs one package");
            const auto report_data = deployer.verify(operands[0]);
            for (const auto &problem : report_data.problems) emit("E\t" + clean(problem));
            if (report_data.ok) emit("R\t" + clean(operands[0]) + " verifies");
            emit(report_data.ok ? "OK" : "FAIL");
            return report_data.ok ? 0 : 1;
        }
        return usage("unknown operation");
    } catch (const std::exception &error) {
        emit(std::string("E\t") + clean(error.what()));
        emit("FAIL");
        return 1;
    }
}
