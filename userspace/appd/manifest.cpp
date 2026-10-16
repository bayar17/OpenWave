#include "appd.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QCryptographicHash>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <selinux/selinux.h>

static bool installedPath(const QString &path, bool executable)
{
    const auto bytes = path.toUtf8();
    char *canonical = realpath(bytes.constData(), nullptr);
    struct stat st;
    const bool same = canonical && bytes == canonical && !lstat(bytes.constData(), &st) && st.st_nlink == 1;
    free(canonical);
    return same && appd_verified_file(bytes.constData(), executable, nullptr);
}

extern "C" App *appd_read_bundle(const char *directory)
{
    const QString bundle = QString::fromUtf8(directory);
    const QString path = bundle + "/manifest.json";
    if (!installedPath(path, false)) return nullptr;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) return nullptr;
    const auto bytes = file.readAll();
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return nullptr;
    const auto object = document.object();
    const QString id = object.value("id").toString();
    static const QRegularExpression identifier("^[a-zA-Z][a-zA-Z0-9_-]*(?:\\.[a-zA-Z0-9][a-zA-Z0-9_-]*){2,}$");
    if (object.value("schemaVersion").toInt() != 1 || id.size() > 128
        || !identifier.match(id).hasMatch() || bundle.section('/', -1) != id
        || (id.startsWith("com.openwave.") && !bundle.startsWith("/System/Applications/"))
        || object.value("name").toString().trimmed().isEmpty()
        || object.value("name").toString().size() > 256
        || object.value("version").toString().isEmpty()
        || object.value("runtime").toString() != "openwave-qt") return nullptr;
    const auto sandbox = object.value("sandbox").toObject();
    // Profiles select installed policy, never command line or kernel policy fragments.
    if (!sandbox.value("enabled").isBool() || !sandbox.value("enabled").toBool()
        || sandbox.value("profile").toString() != "standard") return nullptr;
    const auto requirements = sandbox.value("requires");
    if (!requirements.isUndefined() && !requirements.isArray()) return nullptr;
    for (const auto &requirement : requirements.toArray()) {
        if (!requirement.isString() || !QStringList{"landlock", "uid", "cgroups-v2", "selinux", "bubblewrap", "seccomp"}.contains(requirement.toString())) return nullptr;
        if (requirement.toString() == "selinux" && (is_selinux_enabled() != 1 || security_getenforce() != 1)) return nullptr;
        if (requirement.toString() == "bubblewrap" && !appd_verified_file("/usr/bin/bwrap", TRUE, nullptr)) return nullptr;
    }
    const auto idBytes = id.toUtf8();
    const auto executableBytes = (bundle + '/' + object.value("executable").toString()).toUtf8();
    char *policyArguments[] = {const_cast<char *>(executableBytes.constData()), nullptr};
    App policyIdentity{};
    policyIdentity.id = const_cast<char *>(idBytes.constData());
    policyIdentity.argv = policyArguments;
    if (!requirements.toArray().isEmpty() && appd_trusted_app(&policyIdentity)) return nullptr;
    const QString role = object.value("role").toString();
    if (!role.isEmpty()) {
        // The compositor is bootstrapped by PID 1, never exposed as a launchable app.
        if (role != "compositor" || id != "com.openwave.shell"
            || bundle != "/System/Applications/com.openwave.shell")
            g_warning("appd: unsupported application role in %s", directory);
        return nullptr;
    }
    const auto relative = [](const QString &value) {
        if (value.isEmpty() || value.startsWith('/') || value.contains('\\')) return false;
        for (const auto &part : value.split('/'))
            if (part.isEmpty() || part == "." || part == "..") return false;
        return true;
    };
    const QString executable = object.value("executable").toString();
    if (!relative(executable) || !executable.startsWith("bin/")
        || !installedPath(bundle + '/' + executable, true)) return nullptr;
    const auto permissions = object.value("permissions");
    if (!permissions.isArray() || permissions.toArray().size() > 64) return nullptr;
    const QRegularExpression permissionId(QRegularExpression::anchoredPattern("[A-Za-z0-9][A-Za-z0-9_.-]{0,127}"));
    QStringList declared;
    for (const auto &permission : permissions.toArray()) {
        if (!permission.isString() || !permissionId.match(permission.toString()).hasMatch()
            || declared.contains(permission.toString())) return nullptr;
        declared.append(permission.toString());
    }
    const QString icon = object.value("icon").toString();
    if (!icon.isEmpty() && (!relative(icon) || !installedPath(bundle + '/' + icon, false))) return nullptr;
    for (const auto key : {"singleInstance", "acceptsPaths"})
        if (object.contains(key) && !object.value(key).isBool()) return nullptr;
    const auto arguments = object.value("arguments");
    if (!arguments.isUndefined() && (!arguments.isArray() || arguments.toArray().size() > 16)) return nullptr;
    for (const auto &argument : arguments.toArray())
        if (!argument.isString() || argument.toString().size() > 4096 || argument.toString().contains(QChar(0))) return nullptr;
    auto *app = g_new0(App, 1);
    app->id = g_strdup(id.toUtf8().constData());
    app->name = g_strdup(object.value("name").toString().toUtf8().constData());
    app->bundle = g_strdup(directory);
    app->manifest = g_strdup(path.toUtf8().constData());
    app->digest = g_strdup(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().constData());
    app->permissions = g_new0(char *, declared.size() + 1);
    for (int i = 0; i < declared.size(); ++i) app->permissions[i] = g_strdup(declared[i].toUtf8().constData());
    app->argv = g_new0(char *, arguments.toArray().size() + 2);
    app->argv[0] = g_strdup((bundle + '/' + executable).toUtf8().constData());
    int argumentIndex = 1;
    for (const auto &argument : arguments.toArray()) app->argv[argumentIndex++] = g_strdup(argument.toString().toUtf8().constData());
    app->icon = icon.isEmpty() ? g_strdup("") : g_filename_to_uri((bundle + '/' + icon).toUtf8().constData(), nullptr, nullptr);
    app->single = object.value("singleInstance").toBool(true);
    app->paths = object.value("acceptsPaths").toBool(false);
    app->state = g_strdup("Stopped"); app->error = g_strdup("");
    return app;
}
