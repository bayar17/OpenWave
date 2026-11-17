#include "appdeploy/appdeploy.hpp"
#include "util.hpp"

#include <toml.hpp>

#include <algorithm>
#include <regex>
#include <set>

namespace appdeploy {

namespace {
constexpr std::size_t kMaxToml = 64 * 1024;

bool relative_path(const std::string &value)
{
    if (value.empty() || value[0] == '/' || value.find('\\') != std::string::npos
        || value.find('\0') != std::string::npos) return false;
    for (const auto &part : util::split(value, '/'))
        if (part.empty() || part == "." || part == "..") return false;
    return true;
}

bool has_control(const std::string &text)
{
    return std::any_of(text.begin(), text.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; });
}

template <typename T>
bool typed(const toml::value &table, const char *key, T &out)
{
    if (!table.contains(key)) return false;
    try { out = toml::find<T>(table, key); } catch (const std::exception &) { throw Error(std::string("Application.toml: ") + key + " has the wrong type"); }
    return true;
}
}

bool valid_package_spec(const std::string &spec)
{
    static const std::regex pattern("^[A-Za-z0-9][A-Za-z0-9_.+:~^*?-]{0,200}$");
    return std::regex_match(spec, pattern);
}

ApplicationDescriptor ApplicationDescriptor::parse(const std::string &text, const std::string &expected_id)
{
    if (text.size() > kMaxToml) throw Error("Application.toml is larger than 64 KiB");
    if (text.find('\0') != std::string::npos) throw Error("Application.toml contains a NUL byte");
    toml::value root;
    try {
        root = toml::parse_str(text);
    } catch (const std::exception &error) {
        throw Error(std::string("Application.toml is not valid TOML: ") + error.what());
    }
    if (!root.is_table()) throw Error("Application.toml is not a table");

    static const std::set<std::string> top = {"schema", "id", "name", "version", "kind", "executable", "icon",
                                              "runtime", "single_instance", "accepts_paths", "arguments",
                                              "sandbox", "permissions", "opens", "mime_types", "developer"};
    std::vector<std::string> problems;
    for (const auto &[key, value] : root.as_table()) {
        (void)value;
        if (!top.count(key)) problems.push_back("unknown key \"" + key + "\"");
    }
    ApplicationDescriptor d;
    std::int64_t schema = 0;
    if (!typed(root, "schema", schema) || schema != 1) problems.push_back("schema must be 1");
    if (!typed(root, "id", d.id)) problems.push_back("id is required");
    if (!typed(root, "name", d.name)) problems.push_back("name is required");
    if (!typed(root, "version", d.version)) problems.push_back("version is required");
    d.kind = "application";
    typed(root, "kind", d.kind);
    typed(root, "executable", d.executable);
    typed(root, "icon", d.icon);
    typed(root, "runtime", d.runtime);
    typed(root, "single_instance", d.single_instance);
    typed(root, "accepts_paths", d.accepts_paths);
    typed(root, "arguments", d.arguments);
    typed(root, "opens", d.opens);
    typed(root, "mime_types", d.mime_types);
    typed(root, "developer", d.developer);
    if (root.contains("sandbox")) {
        const auto &sandbox = root.at("sandbox");
        if (!sandbox.is_table()) problems.push_back("sandbox must be a table");
        else {
            for (const auto &[key, value] : sandbox.as_table()) {
                (void)value;
                if (key != "profile" && key != "requires") problems.push_back("unknown key sandbox." + key);
            }
            typed(sandbox, "profile", d.profile);
            typed(sandbox, "requires", d.requires_);
        }
    }
    if (root.contains("permissions")) {
        const auto &permissions = root.at("permissions");
        if (!permissions.is_table()) problems.push_back("permissions must be a table");
        else {
            for (const auto &[key, value] : permissions.as_table()) {
                (void)value;
                if (key != "request") problems.push_back("unknown key permissions." + key);
            }
            typed(permissions, "request", d.permissions);
        }
    }

    if (!valid_application_id(d.id)) problems.push_back("id is not a valid application ID");
    if (d.id != expected_id) problems.push_back("id \"" + d.id + "\" does not match the package (" + expected_id + ")");
    if (d.id.rfind("com.openwave.", 0) == 0 || d.id.rfind("org.openwave.android.", 0) == 0
        || d.id.rfind("org.openwave.webapp.", 0) == 0) problems.push_back("id is in a reserved namespace");
    if (d.name.empty() || d.name.size() > 256 || has_control(d.name)) problems.push_back("name must be 1-256 printable characters");
    if (d.version.empty() || d.version.size() > 128 || has_control(d.version)) problems.push_back("version is invalid");
    if (d.kind != "application" && d.kind != "library") problems.push_back("kind must be \"application\" or \"library\"");
    if (d.runtime != "openwave-qt") problems.push_back("runtime must be \"openwave-qt\"");
    if (d.profile != "standard") problems.push_back("sandbox.profile must be \"standard\"");
    static const std::set<std::string> allowed_requirements = {"landlock", "uid", "cgroups-v2", "selinux"};
    for (const auto &requirement : d.requires_)
        if (!allowed_requirements.count(requirement)) problems.push_back("unsupported sandbox requirement \"" + requirement + "\"");
    if (d.kind == "application") {
        if (!relative_path(d.executable) || d.executable.rfind("bin/", 0) != 0 || d.executable.size() <= 4)
            problems.push_back("executable must be a relative path below bin/");
    } else if (!d.executable.empty()) problems.push_back("a library has no executable");
    if (!d.icon.empty() && !relative_path(d.icon)) problems.push_back("icon must be a relative path inside the bundle");
    if (d.arguments.size() > 16) problems.push_back("at most 16 arguments");
    for (const auto &argument : d.arguments)
        if (argument.size() > 4096) problems.push_back("an argument is longer than 4096 characters");
    if (d.opens.size() > 32) problems.push_back("at most 32 file extensions in opens");
    static const std::regex extension("^[a-z0-9]{1,16}$");
    std::set<std::string> distinct;
    for (const auto &item : d.opens) {
        if (!std::regex_match(item, extension)) problems.push_back("invalid file extension \"" + item + "\" in opens");
        if (!distinct.insert(item).second) problems.push_back("file extension \"" + item + "\" is listed twice");
    }
    if (!d.opens.empty() && !d.accepts_paths) problems.push_back("opens requires accepts_paths = true");
    if (d.developer.size() > 256 || has_control(d.developer)) problems.push_back("invalid developer");
    static const std::regex mime("^[a-z0-9][a-z0-9!#$&^_.+-]{0,126}/[a-z0-9][a-z0-9!#$&^_.+-]{0,126}$");
    distinct.clear();
    if (d.mime_types.size() > 64) problems.push_back("at most 64 MIME types");
    for (const auto &item : d.mime_types)
        if (!std::regex_match(item, mime) || !distinct.insert(item).second) problems.push_back("invalid or repeated MIME type");
    if (!d.mime_types.empty() && !d.accepts_paths) problems.push_back("mime_types requires accepts_paths = true");
    if (d.permissions.size() > 64) problems.push_back("at most 64 permissions");
    static const std::regex permission("^[A-Za-z0-9][A-Za-z0-9_.-]{0,127}$");
    std::set<std::string> unique;
    for (const auto &item : d.permissions) {
        if (!std::regex_match(item, permission)) problems.push_back("invalid permission identifier \"" + item + "\"");
        if (!unique.insert(item).second) problems.push_back("permission \"" + item + "\" is listed twice");
    }
    if (!problems.empty()) {
        std::string message;
        for (const auto &problem : problems) message += (message.empty() ? "" : "; ") + problem;
        throw Error(message);
    }
    return d;
}

std::string ApplicationDescriptor::manifest_json() const
{
    using util::json_escape;
    auto quoted = [](const std::string &s) { return "\"" + json_escape(s) + "\""; };
    auto list = [&](const std::vector<std::string> &values) {
        std::string out = "[";
        for (std::size_t i = 0; i < values.size(); ++i) out += (i ? ", " : "") + quoted(values[i]);
        return out + "]";
    };
    std::string out = "{\n";
    out += "  \"schemaVersion\": 1,\n";
    out += "  \"id\": " + quoted(id) + ",\n";
    out += "  \"name\": " + quoted(name) + ",\n";
    out += "  \"version\": " + quoted(version) + ",\n";
    out += "  \"type\": \"application\",\n";
    out += "  \"executable\": " + quoted(executable) + ",\n";
    out += "  \"runtime\": " + quoted(runtime) + ",\n";
    if (!icon.empty()) out += "  \"icon\": " + quoted(icon) + ",\n";
    out += "  \"sandbox\": {\"enabled\": true, \"profile\": " + quoted(profile);
    if (!requires_.empty()) out += ", \"requires\": " + list(requires_);
    out += "},\n";
    out += "  \"permissions\": " + list(permissions) + ",\n";
    out += std::string("  \"singleInstance\": ") + (single_instance ? "true" : "false") + ",\n";
    out += std::string("  \"acceptsPaths\": ") + (accepts_paths ? "true" : "false");
    if (!arguments.empty()) out += ",\n  \"arguments\": " + list(arguments);
    if (!developer.empty()) out += ",\n  \"developer\": " + quoted(developer);
    if (!mime_types.empty()) out += ",\n  \"mimeTypes\": " + list(mime_types);
    if (!opens.empty()) out += ",\n  \"opens\": " + list(opens);
    out += "\n}\n";
    return out;
}

}
