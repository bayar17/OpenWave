#include "appd.h"
#include "../webapp/WebAppDefinition.h"
#include "../webapp/WebAppIcon.h"
#include "../security/selinux/OpenWaveSELinux.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTemporaryDir>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace {
bool syncDirectory(const QString &path)
{
    const int fd = open(QFile::encodeName(path).constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return false;
    const bool okay = fsync(fd) == 0;
    close(fd);
    return okay;
}
bool copyRuntime(const QString &source, const QString &target, QFile::Permissions permissions)
{
    if (!appd_verified_file(QFile::encodeName(source).constData(), false, nullptr)
        || !QFile::copy(source, target) || !QFile::setPermissions(target, permissions)) return false;
    QFile file(target);
    return file.open(QIODevice::ReadOnly) && fsync(file.handle()) == 0;
}
}

static QByteArray declaration(const QJsonObject &definition)
{
    auto quote = [](const QString &value) {
        QString out;
        for (const QChar c : value) {
            if (c == '"' || c == '\\') out += '\\';
            out += c.unicode() < 0x20 ? QChar(' ') : c;
        }
        return '"' + out + '"';
    };
    QString text = "schema = 1\nid = " + quote(definition.value("id").toString()) + "\nname = " + quote(definition.value("name").toString())
        + "\nversion = " + quote(definition.value("version").toString()) + "\nkind = \"application\"\nexecutable = "
        + quote(definition.value("executable").toString()) + "\nruntime = \"openwave-qt\"\n";
    if (!definition.value("icon").toString().isEmpty()) text += "icon = " + quote(definition.value("icon").toString()) + "\n";
    text += "single_instance = true\naccepts_paths = false\n\n[sandbox]\nprofile = \"standard\"\n\n[permissions]\nrequest = []\n\n"
            "[web_app]\nurl = " + quote(definition.value("webApp").toObject().value("url").toString()) + "\n";
    return text.toUtf8();
}

static const char *const bundleDirectories[] = {"bin", "sbin", "lib", "lib64", "plugins", "qml", "resources",
    "resources/images", "resources/sounds", "resources/fonts", "share", "share/icons", "share/translations", "share/data"};

extern "C" char *appd_create_webapp(Manager *manager, Instance *caller, guint pid,
    const char *nameText, const char *urlText, const guint8 *iconData, gsize iconSize, GError **error)
{
    if (!caller || pid != static_cast<guint>(caller->pid)
        || strcmp(caller->app->id, "com.openwave.ocean")
        || strcmp(caller->app->argv[0], "/System/Applications/com.openwave.ocean/bin/ocean")
        || !appd_validate_bundle(caller->app, nullptr)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
            "Only the installed Ocean Browser may create web apps.");
        return nullptr;
    }
    const QString name = QString::fromUtf8(nameText);
    const QUrl url = OpenWaveWebApp::address(QString::fromUtf8(urlText));
    const QByteArray icon = iconSize <= 1024 * 1024 ? QByteArray(reinterpret_cast<const char *>(iconData), iconSize) : QByteArray();
    if (iconSize && !OpenWaveWebApp::validIcon(icon)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "The website icon must be a PNG of at most 512 pixels and 1 MiB.");
        return nullptr;
    }
    if (!OpenWaveWebApp::validName(name) || url.isEmpty()) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
            "A web app requires a valid name and an HTTP or HTTPS address without embedded credentials.");
        return nullptr;
    }
    struct stat metadata;
    if (geteuid() || lstat("/Applications", &metadata) || !S_ISDIR(metadata.st_mode)
        || metadata.st_uid || (metadata.st_mode & 0022)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, "Application storage is unavailable.");
        return nullptr;
    }
    const QString id = OpenWaveWebApp::identifier(url);
    const QString destination = "/Applications/" + id;
    if (QFile::exists(destination) || QFile::exists("/Applications/.uninstall-" + id)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_EXISTS, "A web app for this address is already installed.");
        return nullptr;
    }
    QTemporaryDir pending("/Applications/.webapp-install-XXXXXX");
    const auto readable = QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther;
    const auto executable = readable | QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther;
    bool okay = pending.isValid();
    for (const auto *directory : bundleDirectories)
        okay = okay && QDir(pending.path()).mkdir(directory)
            && QFile::setPermissions(pending.path() + '/' + directory, executable);
    okay = okay && copyRuntime("/usr/libexec/openwave-webapp", pending.path() + "/bin/openwave-webapp", executable);
    if (icon.isEmpty()) okay = okay && copyRuntime("/usr/share/pixmaps/openwave-webapp.svg", pending.path() + "/resources/icon.svg", readable);
    else {
        QSaveFile image(pending.path() + "/resources/icon.png");
        okay = okay && image.open(QIODevice::WriteOnly) && image.setPermissions(readable)
            && image.write(icon) == icon.size() && image.commit();
    }
    QSaveFile manifest(pending.path() + "/manifest.json");
    auto definition = OpenWaveWebApp::manifest(name, url);
    if (!icon.isEmpty()) definition.insert("icon", "resources/icon.png");
    const auto source = declaration(definition);
    QSaveFile toml(pending.path() + "/Application.toml");
    okay = okay && toml.open(QIODevice::WriteOnly) && toml.setPermissions(readable)
        && toml.write(source) == source.size() && toml.commit();
    const auto bytes = QJsonDocument(definition).toJson();
    const QString applicationsDirectory = pending.path() + "/share/applications";
    okay = okay && (QDir(applicationsDirectory).exists() || QDir(pending.path() + "/share").mkdir("applications"))
        && QFile::setPermissions(applicationsDirectory, executable);
    QString shownName = name;
    for (QChar &character : shownName) if (character.unicode() < 32 || character.unicode() == 127) character = ' ';
    shownName.replace('\\', QStringLiteral("\\\\"));
    const QString iconPath = "/Applications/" + id + (icon.isEmpty() ? "/resources/icon.svg" : "/resources/icon.png");
    const QByteArray desktopText = ("[Desktop Entry]\nType=Application\nVersion=1.0\nName=" + shownName
        + "\nExec=/System/Services/openwave-launch " + id + "\nIcon=" + iconPath
        + "\nTerminal=false\nCategories=Network;\nX-OpenWave-AppId=" + id + "\n").toUtf8();
    QSaveFile desktopFile(applicationsDirectory + "/" + id + ".desktop");
    okay = okay && desktopFile.open(QIODevice::WriteOnly) && desktopFile.setPermissions(readable)
        && desktopFile.write(desktopText) == desktopText.size() && desktopFile.commit();
    okay = okay && manifest.open(QIODevice::WriteOnly) && manifest.setPermissions(readable)
        && manifest.write(bytes) == bytes.size() && manifest.commit();
    okay = okay && QFile::setPermissions(pending.path(), executable)
        && openwave_selinux_restore(QFile::encodeName(pending.path()).constData()) == 0
        && syncDirectory(applicationsDirectory) && syncDirectory(pending.path() + "/bin") && syncDirectory(pending.path() + "/resources")
        && syncDirectory(pending.path());
    if (!okay || syscall(SYS_renameat2, AT_FDCWD, QFile::encodeName(pending.path()).constData(),
        AT_FDCWD, QFile::encodeName(destination).constData(), RENAME_NOREPLACE)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Could not create the web app bundle.");
        return nullptr;
    }
    pending.setAutoRemove(false);
    if (!syncDirectory("/Applications")) {
        QDir(destination).removeRecursively();
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Could not save the web app bundle.");
        return nullptr;
    }
    appd_registry_scan(manager);
    return g_strdup(id.toUtf8().constData());
}

extern "C" gboolean appd_update_webapp_icon(Manager *manager, Instance *caller, guint pid,
    const guint8 *iconData, gsize iconSize, GError **error)
{
    if (!caller || caller->app->removing || pid != static_cast<guint>(caller->pid) || !appd_validate_bundle(caller->app, nullptr)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, "A verified web app leader is required.");
        return FALSE;
    }
    const QString bundle = QString::fromUtf8(caller->app->bundle);
    QFile file(bundle + "/manifest.json");
    if (!file.open(QIODevice::ReadOnly)) return FALSE;
    auto definition = QJsonDocument::fromJson(file.readAll()).object();
    const auto url = OpenWaveWebApp::address(definition.value("webApp").toObject().value("url").toString());
    const auto id = OpenWaveWebApp::identifier(url);
    if (url.isEmpty() || id != QString::fromUtf8(caller->app->id) || bundle != "/Applications/" + id
        || definition.value("executable").toString() != "bin/openwave-webapp") {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, "Only a web app may refresh its own icon.");
        return FALSE;
    }
    const QByteArray icon = iconSize <= 1024 * 1024 ? QByteArray(reinterpret_cast<const char *>(iconData), iconSize) : QByteArray();
    if (!OpenWaveWebApp::validIcon(icon)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "A valid website PNG of at most 512 pixels is required.");
        return FALSE;
    }
    const QString relative = "resources/icon-" + QString::fromLatin1(QCryptographicHash::hash(icon, QCryptographicHash::Sha256).toHex()) + ".png";
    if (definition.value("icon").toString() == relative) return TRUE;
    const QString previous = definition.value("icon").toString();
    const auto readable = QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther;
    QSaveFile image(bundle + '/' + relative);
    bool okay = image.open(QIODevice::WriteOnly) && image.setPermissions(readable)
        && image.write(icon) == icon.size() && image.commit();
    okay = okay && openwave_selinux_restore(QFile::encodeName(bundle + '/' + relative).constData()) == 0
        && syncDirectory(bundle + "/resources");
    definition.insert("icon", relative);
    const auto bytes = QJsonDocument(definition).toJson();
    const auto source = declaration(definition);
    QSaveFile toml(bundle + "/Application.toml");
    okay = okay && toml.open(QIODevice::WriteOnly) && toml.setPermissions(readable)
        && toml.write(source) == source.size() && toml.commit()
        && openwave_selinux_restore(QFile::encodeName(bundle + "/Application.toml").constData()) == 0;
    QSaveFile manifest(bundle + "/manifest.json");
    okay = okay && manifest.open(QIODevice::WriteOnly) && manifest.setPermissions(readable)
        && manifest.write(bytes) == bytes.size() && manifest.commit();
    okay = okay && openwave_selinux_restore(QFile::encodeName(bundle + "/manifest.json").constData()) == 0 && syncDirectory(bundle);
    const QString desktopPath = bundle + "/share/applications/" + id + ".desktop";
    QFile desktopSource(desktopPath);
    if (okay && desktopSource.open(QIODevice::ReadOnly)) {
        QStringList lines = QString::fromUtf8(desktopSource.readAll()).split('\n');
        desktopSource.close();
        for (QString &line : lines)
            if (line.startsWith("Icon=")) line = "Icon=" + bundle + '/' + relative;
        const QByteArray text = lines.join('\n').toUtf8();
        QSaveFile rewritten(desktopPath);
        okay = rewritten.open(QIODevice::WriteOnly) && rewritten.setPermissions(readable)
            && rewritten.write(text) == text.size() && rewritten.commit()
            && openwave_selinux_restore(QFile::encodeName(desktopPath).constData()) == 0;
    }
    if (!okay) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Could not save the website icon.");
        return FALSE;
    }
    g_free(caller->app->digest);
    caller->app->digest = g_strdup(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().constData());
    g_free(caller->app->icon);
    caller->app->icon = g_filename_to_uri(QFile::encodeName(bundle + '/' + relative).constData(), nullptr, nullptr);
    const QString previousHash = previous.mid(15, 64);
    if (previous.size() == 83 && previous.startsWith("resources/icon-") && previous.endsWith(".png")
        && QByteArray::fromHex(previousHash.toLatin1()).toHex() == previousHash.toLatin1()) {
        QFile::remove(bundle + '/' + previous);
        syncDirectory(bundle + "/resources");
    }
    appd_emit(manager, "RegistryChanged", nullptr);
    return TRUE;
}
