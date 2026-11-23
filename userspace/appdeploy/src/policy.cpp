#include "appdeploy/appdeploy.hpp"
#include "util.hpp"

#include <algorithm>
#include <regex>
#include <set>

namespace appdeploy {

namespace {
constexpr std::uint32_t kFileGhost = 1u << 6;
constexpr std::uint32_t kSetuid = 04000, kSetgid = 02000, kSticky = 01000;
constexpr std::uint32_t kGroupOtherWrite = 022;

const std::regex &id_pattern()
{
    static const std::regex pattern("^[a-zA-Z][a-zA-Z0-9_-]*(\\.[a-zA-Z0-9][a-zA-Z0-9_-]*){2,}$");
    return pattern;
}

bool reserved_prefix(const std::string &id)
{
    return util::starts_with(id, "com.openwave.") || util::starts_with(id, "org.openwave.android.")
        || util::starts_with(id, "org.openwave.webapp.");
}

void add(std::vector<Violation> &out, const char *rule, std::string message)
{
    out.push_back({rule, std::move(message)});
}
}

bool valid_application_id(const std::string &id)
{
    return id.size() <= 128 && std::regex_match(id, id_pattern());
}

std::vector<Violation> Policy::evaluate(const PackageInfo &info, Trust trust) const
{
    std::vector<Violation> out;
    if (!info.klass) {
        add(out, "class", info.name + " does not declare a package class: it must provide exactly one of "
            "openwave-application(" + info.name + ") or openwave-system-package(" + info.name + ")");
        return out;
    }
    const PackageClass klass = *info.klass;
    if (info.name.empty() || info.version.empty()) add(out, "identity", "the package has no name or version");
    if (info.arch != "x86_64" && info.arch != "noarch") add(out, "arch", "architecture " + info.arch + " is not supported");
    if (info.os != "linux") add(out, "os", "operating system " + info.os + " is not supported");
    if (!info.scripts.empty()) {
        std::string names;
        for (const auto &script : info.scripts) names += (names.empty() ? "" : ", ") + script;
        add(out, "scriptlet", "OpenWave packages are declarative: scriptlets and triggers (" + names + ") are never run, "
            "so a package that carries any is rejected");
    }
    if (info.payload_format != "cpio") add(out, "payload", "unsupported payload format " + info.payload_format);
    if (info.payload_compressor != "gzip" && info.payload_compressor != "zstd" && info.payload_compressor != "xz")
        add(out, "payload", "unsupported payload compression " + info.payload_compressor);
    if (info.relocatable) add(out, "relocatable", "relocatable packages are not supported");
    if (info.files.size() > config_.max_files) add(out, "limits", "too many files");
    if (info.installed_size > config_.max_installed_bytes) add(out, "limits", "installed size is over the limit");
    if (klass == PackageClass::System && trust != Trust::System)
        add(out, "signature", "system packages must be signed by a system key (this one is " + std::string(trust_name(trust)) + ")");

    std::string allowed_bundle;
    if (klass == PackageClass::Application) {
        if (!valid_application_id(info.name)) add(out, "identity", "the package name is not a valid application ID");
        if (reserved_prefix(info.name)) add(out, "identity", "the application ID " + info.name + " is in a reserved namespace");
        if (info.has_epoch && info.epoch != 0) add(out, "identity", "application packages must not use an epoch");
        allowed_bundle = config_.applications + "/" + info.name;
        for (const auto &obsolete : info.obsoletes)
            add(out, "obsoletes", "application packages may not obsolete other packages (" + obsolete + ")");
        for (const auto &provide : info.provides) {
            const auto name = provide.substr(0, provide.find(' '));
            const bool own = name == info.name || util::starts_with(name, info.name + "(")
                || name == "openwave-application(" + info.name + ")";
            if (!own) add(out, "provides", "an application may only provide itself, not " + name);
        }
    } else {
        for (const auto &provide : info.provides)
            if (util::starts_with(provide, "openwave-application(")
                && provide.substr(0, provide.find(' ')) != "openwave-application(" + info.name + ")")
                add(out, "provides", "a system package may not provide an application: " + provide);
    }

    std::set<std::string> seen, directories;
    for (const auto &entry : info.files)
        if (entry.directory()) directories.insert(entry.path);
    bool bundle_root = false;
    for (const auto &entry : info.files) {
        const std::string &path = entry.path;
        const std::string strict = util::strict_absolute(path);
        if (strict.empty()) {
            add(out, "path", "unsafe file name in package: " + path);
            continue;
        }
        if (!seen.insert(path).second) add(out, "path", "duplicate entry " + path);
        if (entry.user != "root" || entry.group != "root")
            add(out, "ownership", path + " is not owned by root:root (" + entry.user + ":" + entry.group + ")");
        if (entry.mode & (kSetuid | kSetgid | kSticky)) add(out, "mode", path + " has setuid/setgid/sticky bits");
        if (entry.mode & kGroupOtherWrite) add(out, "mode", path + " is writable by group or others");
        if (!entry.directory() && !entry.regular() && !entry.symlink())
            add(out, "filetype", path + " is not a regular file, directory or symbolic link");
        if (!entry.caps.empty()) add(out, "capabilities", path + " carries file capabilities");
        if (entry.regular() && entry.nlink > 1) add(out, "hardlink", path + " is a hard link");
        if (entry.flags & kFileGhost) add(out, "ghost", path + " is a %ghost file");
        for (const auto &protected_path : config_.protected_paths) {
            const bool inside_own = klass == PackageClass::Application && protected_path == config_.applications;
            if (!inside_own && util::within(strict, protected_path))
                add(out, "protected-path", path + " is in a protected location (" + protected_path + ")");
        }
        if (klass == PackageClass::Application) {
            if (!util::within(strict, allowed_bundle))
                add(out, "location", path + " is outside the application's own directory " + allowed_bundle);
            else if (strict != allowed_bundle) {
                static const std::set<std::string> layout = {"Application.toml", "bin", "sbin", "lib", "lib64", "plugins",
                                                             "qml", "resources", "share"};
                const std::string top = util::split(strict.substr(allowed_bundle.size() + 1), '/').front();
                if (!layout.count(top) && !util::starts_with(top, ".manifest"))
                    add(out, "layout", path + " is not in a standard bundle directory (bin, sbin, lib, lib64, plugins, qml, resources, share)");
                else if (top == "Application.toml" && (entry.directory() || strict != allowed_bundle + "/Application.toml"))
                    add(out, "layout", "Application.toml must be a file at the top of the bundle");
                else if (strict.size() > allowed_bundle.size() + 1 && top != "Application.toml" && util::split(strict.substr(allowed_bundle.size() + 1), '/').size() == 1 && !entry.directory())
                    add(out, "layout", path + " must be a directory (only Application.toml is a file at the top of a bundle)");
            }
            if (util::within(strict, allowed_bundle)) {
                if (strict == allowed_bundle) {
                    if (!entry.directory()) add(out, "location", allowed_bundle + " must be a directory");
                    bundle_root = true;
                } else if (strict == allowed_bundle + "/manifest.json" || util::starts_with(util::split(strict.substr(allowed_bundle.size() + 1), '/').front(), ".manifest"))
                    add(out, "manifest", "manifest.json is generated at installation; a package must not ship it");
                else if (strict == allowed_bundle + "/share/applications" || util::within(strict, allowed_bundle + "/share/applications"))
                    add(out, "manifest", "desktop entries are generated at installation; a package must not ship share/applications");
            }
        } else {
            bool ok = false;
            for (const auto &root : config_.system_roots)
                if (strict != root && util::within(strict, root)) ok = true;
            if (!ok) add(out, "location", path + " is outside the locations system packages may use");
        }
        if (entry.symlink()) {
            const std::string resolved = util::resolve_link(strict, entry.link);
            const std::string boundary = klass == PackageClass::Application ? allowed_bundle : std::string();
            bool inside = !resolved.empty();
            if (inside && klass == PackageClass::Application) inside = util::within(resolved, boundary);
            if (inside && klass == PackageClass::System) {
                inside = false;
                for (const auto &root : config_.system_roots) if (util::within(resolved, root)) inside = true;
            }
            if (!inside) add(out, "symlink", path + " is a link to " + entry.link + ", which leaves the package's own area");
        }
        if (klass == PackageClass::Application && util::within(strict, allowed_bundle) && strict != allowed_bundle) {
            std::string parent = strict.substr(0, strict.rfind('/'));
            while (parent != allowed_bundle && parent.size() > allowed_bundle.size()) {
                if (!directories.count(parent)) {
                    add(out, "unowned-directory", "directory " + parent + " is used but not owned by the package");
                    break;
                }
                parent = parent.substr(0, parent.rfind('/'));
            }
        }
    }
    if (klass == PackageClass::Application && !bundle_root)
        add(out, "bundle-root", "the package must own its directory " + allowed_bundle);
    std::vector<Violation> unique;
    for (auto &violation : out)
        if (std::none_of(unique.begin(), unique.end(), [&](const Violation &v) { return v.message == violation.message; }))
            unique.push_back(std::move(violation));
    return unique;
}

std::vector<Violation> Policy::declaration(const PackageInfo &info, const std::optional<std::string> &toml) const
{
    std::vector<Violation> out;
    if (!info.klass || *info.klass != PackageClass::Application) return out;
    const std::string bundle = config_.applications + "/" + info.name;
    if (!toml) {
        add(out, "toml", "the package has no " + bundle + "/Application.toml");
        return out;
    }
    ApplicationDescriptor descriptor;
    try {
        descriptor = ApplicationDescriptor::parse(*toml, info.name);
    } catch (const Error &error) {
        add(out, "toml", std::string("Application.toml is invalid: ") + error.what());
        return out;
    }
    if (descriptor.id != info.name) add(out, "toml", "Application.toml names " + descriptor.id + ", the package is " + info.name);
    if (descriptor.version != info.version)
        add(out, "toml", "Application.toml version " + descriptor.version + " differs from the package version " + info.version);
    auto find = [&](const std::string &relative) -> const FileEntry * {
        for (const auto &entry : info.files)
            if (entry.path == bundle + "/" + relative) return &entry;
        return nullptr;
    };
    if (descriptor.launchable()) {
        const FileEntry *executable = find(descriptor.executable);
        if (!executable) add(out, "toml", "the executable " + descriptor.executable + " is not in the package");
        else if (!executable->regular() || !(executable->mode & 0100))
            add(out, "toml", "the executable " + descriptor.executable + " is not an executable regular file");
    }
    if (!descriptor.icon.empty()) {
        const FileEntry *icon = find(descriptor.icon);
        if (!icon || !icon->regular()) add(out, "toml", "the icon " + descriptor.icon + " is not a regular file in the package");
    }
    return out;
}

}
