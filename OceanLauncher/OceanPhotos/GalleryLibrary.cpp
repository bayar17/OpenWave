#include "GalleryLibrary.h"
#include <QDateTime>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QSaveFile>
#include <QRegularExpression>
#include <algorithm>

GalleryLibrary::GalleryLibrary(QObject *parent) : GalleryLibrary(QString(), parent) {}

GalleryLibrary::GalleryLibrary(const QString &storagePath, QObject *parent) : QObject(parent)
{
    m_storage = storagePath.isEmpty() ? m_imports.path() : QDir(storagePath).absolutePath();
    if (m_storage.isEmpty() || !QDir().mkpath(m_storage + "/Images")) {
        m_storage.clear(); m_error = tr("Could not open the saved photo library."); return;
    }
    QFile::setPermissions(m_storage, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    QFile::setPermissions(m_storage + "/Images", QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    QFileInfoList images;
    const QRegularExpression digest("^[a-f0-9]{64}$");
    const QDir directory(m_storage + "/Images");
    for (const auto &folder : directory.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
        if (!digest.match(folder.fileName()).hasMatch()) continue;
        images.append(QDir(folder.absoluteFilePath()).entryInfoList(QDir::Files | QDir::Readable | QDir::NoSymLinks));
    }
    std::stable_sort(images.begin(), images.end(), [](const QFileInfo &a, const QFileInfo &b) {
        return a.lastModified() > b.lastModified();
    });
    for (const auto &image : images) appendImage(image.absoluteFilePath(), tr("Selected photos"));
}

QStringList GalleryLibrary::supportedFormats() const
{
    QStringList formats;
    for (const auto &format : QImageReader::supportedImageFormats()) formats.append(QString::fromLatin1(format).toUpper());
    formats.removeDuplicates(); formats.sort(); return formats;
}

QStringList GalleryLibrary::supportedMimeTypes() const
{
    QStringList types;
    for (const auto &type : QImageReader::supportedMimeTypes()) types.append(QString::fromLatin1(type));
    types.removeDuplicates(); types.sort(); return types;
}

int GalleryLibrary::appendImage(const QString &path, const QString &album, const QString &date)
{
    const QFileInfo file(path);
    if (!file.isFile() || !file.isReadable()) return -1;
    const QUrl url = QUrl::fromLocalFile(file.canonicalFilePath());
    for (int i = 0; i < m_photos.size(); ++i)
        if (m_photos[i].toMap().value("url").toUrl() == url) return i;
    QImageReader reader(file.absoluteFilePath());
    reader.setAutoTransform(true);
    if (!reader.canRead()) return -1;
    const QSize size = reader.size();
    QDateTime taken = date.isEmpty() ? file.lastModified() : QDateTime::fromString(date, Qt::ISODate);
    if (!taken.isValid()) taken = file.lastModified();
    taken = taken.toLocalTime();
    const auto day = taken.date();
    const QLocale locale;
    const QString group = day == QDate::currentDate() ? tr("Today")
        : day == QDate::currentDate().addDays(-1) ? tr("Yesterday") : locale.toString(day, QLocale::LongFormat);
    m_photos.append(QVariantMap{
        {"url", url}, {"title", file.fileName()}, {"album", album.isEmpty() ? file.dir().dirName() : album},
        {"date", taken.toMSecsSinceEpoch()}, {"dateLabel", group},
        {"timestamp", locale.toString(taken, QLocale::LongFormat)},
        {"format", QString::fromLatin1(reader.format()).toUpper()},
        {"dimensions", size.isValid() ? tr("%1 × %2").arg(size.width()).arg(size.height()) : tr("Unknown dimensions")},
        {"size", locale.formattedDataSize(file.size())}, {"animated", reader.supportsAnimation()}
    });
    return m_photos.size() - 1;
}

QVariantList GalleryLibrary::albums() const
{
    QVariantList result;
    for (const auto &photo : m_photos) {
        const auto value = photo.toMap();
        const QString name = value.value("album").toString();
        auto found = std::find_if(result.begin(), result.end(), [&](const QVariant &item) {
            return item.toMap().value("name").toString() == name;
        });
        if (found == result.end()) result.append(QVariantMap{{"name", name}, {"cover", value.value("url")}, {"count", 1}});
        else { auto entry = found->toMap(); entry["count"] = entry["count"].toInt() + 1; *found = entry; }
    }
    return result;
}

bool GalleryLibrary::loadFolder(const QUrl &folder)
{
    if (!folder.isLocalFile() || !QFileInfo(folder.toLocalFile()).isDir()) {
        m_error = tr("Choose a local image folder."); emit changed(); return false;
    }
    m_photos.clear(); m_error.clear();
    const QDir directory(folder.toLocalFile());
    QStringList filters;
    for (const auto &format : supportedFormats()) filters << "*." + format.toLower() << "*." + format;
    for (const auto &file : directory.entryInfoList(filters, QDir::Files | QDir::Readable, QDir::Time)) {
        appendImage(file.absoluteFilePath());
        if (m_photos.size() >= 1000) break;
    }
    emit changed(); return true;
}

int GalleryLibrary::openImage(const QUrl &image)
{
    const int index = image.isLocalFile() ? appendImage(image.toLocalFile()) : -1;
    m_error = index < 0 ? tr("This image could not be opened by an installed decoder.") : QString();
    emit changed(); return index;
}

int GalleryLibrary::importImage(const QUrl &image, const QString &name)
{
    const QString filename = QFileInfo(name).fileName();
    auto failed = [this]() { m_error = tr("Could not save the selected image."); emit changed(); return -1; };
    if (m_storage.isEmpty() || !image.isLocalFile() || filename.isEmpty() || filename == "." || filename == "..") return failed();
    QFile input(image.toLocalFile());
    QImageReader reader(image.toLocalFile());
    if (!input.open(QIODevice::ReadOnly) || input.size() <= 0 || input.size() > 64 * 1024 * 1024 || !reader.canRead()) return failed();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&input) || !input.seek(0)) return failed();
    const QString folder = m_storage + "/Images/" + QString::fromLatin1(hash.result().toHex());
    if (QFileInfo(folder).isSymLink() || !QDir().mkpath(folder)) return failed();
    QFile::setPermissions(folder, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    const auto existing = QDir(folder).entryInfoList(QDir::Files | QDir::Readable | QDir::NoSymLinks);
    for (const auto &file : existing) {
        const int index = appendImage(file.absoluteFilePath(), tr("Selected photos"));
        if (index >= 0) { m_error.clear(); emit changed(); return index; }
    }
    QSaveFile output(folder + '/' + filename);
    if (!output.open(QIODevice::WriteOnly) || !output.setPermissions(QFile::ReadOwner | QFile::WriteOwner)) return failed();
    while (!input.atEnd()) {
        const QByteArray bytes = input.read(256 * 1024);
        if (bytes.isEmpty() || output.write(bytes) != bytes.size()) return failed();
    }
    output.setFileTime(QFileInfo(input).lastModified(), QFileDevice::FileModificationTime);
    if (!output.commit()) return failed();
    const int index = appendImage(output.fileName(), tr("Selected photos"));
    m_error = index < 0 ? tr("This image could not be opened by an installed decoder.") : QString();
    emit changed(); return index;
}

bool GalleryLibrary::loadSampleLibrary(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) return false;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isArray()) return false;
    m_photos.clear(); m_error.clear();
    for (const auto &item : document.array()) {
        const auto value = item.toVariant().toMap();
        appendImage(value.value("path").toString(), value.value("album").toString(), value.value("date").toString());
        if (m_photos.size() >= 1000) break;
    }
    std::stable_sort(m_photos.begin(), m_photos.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value("date").toLongLong() > b.toMap().value("date").toLongLong();
    });
    emit changed(); return true;
}
