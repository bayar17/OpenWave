#include "appd.h"
#include <QMimeDatabase>
#include <QMap>
#include <linux/openat2.h>
#include <sys/syscall.h>
#include <QFileInfo>
#include <QFile>
#include <QRegularExpression>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
const QMap<QString, QString> types{
    {"docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
    {"xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
    {"pdf", "application/pdf"}, {"txt", "text/plain"}, {"odt", "application/vnd.oasis.opendocument.text"},
    {"png", "image/png"}, {"jpg", "image/jpeg"}, {"jpeg", "image/jpeg"}, {"webp", "image/webp"},
    {"gif", "image/gif"}, {"svg", "image/svg+xml"}, {"zip", "application/zip"},
    {"tar", "application/x-tar"}, {"gz", "application/gzip"},
    {"apk", "application/vnd.android.package-archive"}, {"rpm", "application/x-rpm"}
};
}

extern "C" char *appd_type_key(const char *value)
{
    const QString type = QString::fromUtf8(value ? value : "");
    static const QRegularExpression extension("\\A[a-z0-9]{1,16}\\z");
    static const QRegularExpression mime("\\A[a-z0-9][a-z0-9!#$&^_.+-]{0,126}/[a-z0-9][a-z0-9!#$&^_.+-]{0,126}\\z");
    QString key;
    if (types.contains(type)) key = types.value(type);
    else if (extension.match(type).hasMatch()) {
        const auto detected = QMimeDatabase().mimeTypeForFile("file." + type, QMimeDatabase::MatchExtension);
        key = detected.isDefault() ? type : detected.name();
    } else if (mime.match(type).hasMatch()) {
        const auto detected = QMimeDatabase().mimeTypeForName(type);
        key = detected.isValid() ? detected.name() : type;
    } else return nullptr;
    return g_strdup(key.toUtf8().constData());
}

extern "C" char *appd_detect_type(const char *path, GError **error)
{
    struct open_how how{};
    how.flags = O_RDONLY | O_CLOEXEC | O_NONBLOCK;
    how.resolve = RESOLVE_NO_SYMLINKS;
    const int fd = syscall(SYS_openat2, AT_FDCWD, path, &how, sizeof(how));
    struct stat st{};
    if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode)) {
        if (fd >= 0) close(fd);
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "A readable regular file is required");
        return nullptr;
    }
    QFile file;
    if (!file.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        close(fd); g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Could not read file"); return nullptr;
    }
    const auto data = file.read(65536);
    if (file.error() != QFileDevice::NoError) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Could not read file"); return nullptr;
    }
    const QString extension = QFileInfo(QString::fromUtf8(path)).suffix().toLower();
    // Packages cannot be redirected to document handlers, even when malformed.
    if (extension == "rpm" || extension == "apk") return appd_type_key(extension.toUtf8().constData());
    if (data.startsWith(QByteArray::fromHex("edabeedb"))) return appd_type_key("rpm");
    QMimeDatabase database;
    const auto content = database.mimeTypeForData(data);
    const auto combined = database.mimeTypeForFileNameAndData(QString::fromUtf8(path), data);
    // Strong content wins over a misleading suffix. ZIP and text containers need
    // the filename to distinguish OOXML/ODF and text subtypes.
    const QString detected = (!content.isDefault() && content.name() != "text/plain"
        && content.name() != "application/zip" && content.name() != "application/x-zerosize")
        ? content.name() : combined.name();
    if ((content.isDefault() || content.name() == "text/plain") && !extension.isEmpty()
        && database.mimeTypeForFile(QString::fromUtf8(path), QMimeDatabase::MatchExtension).isDefault()) return appd_type_key(extension.toUtf8().constData());
    return appd_type_key(detected.toUtf8().constData());
}
