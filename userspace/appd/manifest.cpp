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
    const auto reject = [directory](const char *reason) -> App * {
        g_warning("appd: invalid manifest %s: %s", directory, reason);
        return nullptr;
    };
    const QString bundle = QString::fromUtf8(directory);
    const QString path = bundle + "/manifest.json";
    if (!installedPath(path, false)) {
        if (QFile::exists(path)) return reject("metadata path or ownership is unsafe");
        return nullptr;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) return reject("metadata unreadable or larger than 64 KiB");
    const auto bytes = file.readAll();
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return reject("invalid JSON object");
    const auto object = document.object();
    const QString id = object.value("id").toString();
    static const QRegularExpression identifier("^[a-zA-Z][a-zA-Z0-9_-]*(?:\\.[a-zA-Z0-9][a-zA-Z0-9_-]*){2,}$");
    if (object.value("schemaVersion").toInt() != 1 || id.size() > 128
        || !identifier.match(id).hasMatch() || bundle.section('/', -1) != id
        || (id.startsWith("com.openwave.") && !bundle.startsWith("/System/Applications/"))
        || id.startsWith(QStringLiteral(APPD_ANDROID_PREFIX)) || id.startsWith(QStringLiteral("org.openwave.linux."))
        || object.value("name").toString().trimmed().isEmpty()
        || object.value("name").toString().size() > 256
        || object.value("version").toString().isEmpty()
        || object.value("runtime").toString() != "openwave-qt") return reject("unsupported schema, application identity, name, version or runtime");
    const auto sandbox = object.value("sandbox").toObject();
    // Profiles select installed policy, never command line or kernel policy fragments.
    if (!sandbox.value("enabled").isBool() || !sandbox.value("enabled").toBool()
        || sandbox.value("profile").toString() != "standard") return reject("mandatory sandbox profile missing or invalid");
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
    if (!role.isEmpty() || id == "com.openwave.shell") {
        // The compositor is started by OpenRC, never exposed as a launchable app.
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
        || !installedPath(bundle + '/' + executable, true)) return reject("executable path, ownership or permissions are invalid");
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
    const auto opens = object.value("opens");
    if (!opens.isUndefined() && (!opens.isArray() || opens.toArray().size() > 32)) return reject("opens must list at most 32 file extensions");
    static const QRegularExpression extension(QRegularExpression::anchoredPattern("[a-z0-9]{1,16}"));
    QStringList openedExtensions;
    for (const auto &item : opens.toArray()) {
        if (!item.isString() || !extension.match(item.toString()).hasMatch() || openedExtensions.contains(item.toString()))
            return reject("opens contains an invalid or repeated file extension");
        openedExtensions.append(item.toString());
    }
    if (!openedExtensions.isEmpty() && !object.value("acceptsPaths").toBool(false)) return reject("opens requires acceptsPaths");
    const auto mimeTypes = object.value("mimeTypes");
    if (!mimeTypes.isUndefined() && (!mimeTypes.isArray() || mimeTypes.toArray().size() > 64)) return reject("invalid MIME list");
    const QRegularExpression mime(QRegularExpression::anchoredPattern("[a-z0-9][a-z0-9!#$&^_.+-]{0,126}/[a-z0-9][a-z0-9!#$&^_.+-]{0,126}"));
    QStringList types;
    for (const auto &item : mimeTypes.toArray()) {
        if (!item.isString() || !mime.match(item.toString()).hasMatch() || types.contains(item.toString())) return reject("invalid MIME type");
        types.append(item.toString());
    }
    if (!types.isEmpty() && !object.value("acceptsPaths").toBool()) return reject("MIME types require acceptsPaths");
    const auto uriSchemes = object.value("uriSchemes");
    if (!uriSchemes.isUndefined() && (!uriSchemes.isArray() || uriSchemes.toArray().size() > 16)) return reject("invalid URI scheme list");
    static const QRegularExpression schemePattern(QRegularExpression::anchoredPattern("[a-z][a-z0-9+.-]{0,31}"));
    QStringList schemes;
    for (const auto &item : uriSchemes.toArray()) {
        if (!item.isString() || !schemePattern.match(item.toString()).hasMatch() || schemes.contains(item.toString())) return reject("invalid URI scheme");
        schemes.append(item.toString());
    }
    if (!schemes.isEmpty() && !object.value("acceptsPaths").toBool()) return reject("URI schemes require acceptsPaths");
    const auto developer = object.value("developer");
    if (!developer.isUndefined() && (!developer.isString() || developer.toString().size() > 256)) return reject("invalid developer");
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
    app->opens = g_new0(char *, openedExtensions.size() + 1);
    for (int i = 0; i < openedExtensions.size(); ++i) app->opens[i] = g_strdup(openedExtensions[i].toUtf8().constData());
    app->version = g_strdup(object.value("version").toString().toUtf8().constData());
    app->developer = g_strdup(developer.toString().toUtf8().constData());
    app->mime_types = g_new0(char *, types.size() + 1);
    for (int i = 0; i < types.size(); ++i) app->mime_types[i] = g_strdup(types[i].toUtf8().constData());
    app->schemes = g_new0(char *, schemes.size() + 1);
    for (int i = 0; i < schemes.size(); ++i) app->schemes[i] = g_strdup(schemes[i].toUtf8().constData());
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
