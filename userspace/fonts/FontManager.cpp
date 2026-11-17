#include "FontManager.h"

#include <QDebug>
#include <QDir>
#include <QEvent>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QPointer>
#include <QSet>
#include <QThread>
#include <algorithm>

namespace OpenWave {

FontManager::FontManager(QObject *parent) : QObject(parent)
{
    initialize();
    if (auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        app->installEventFilter(this);
        connect(app, &QGuiApplication::fontDatabaseChanged, this, &FontManager::familiesChanged);
    }
}

bool FontManager::initialize(const QString &fontDirectory)
{
    auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
    if (!app || QThread::currentThread() != app->thread()) {
        qWarning() << "Initialize OpenWave fonts on the GUI thread after QGuiApplication.";
        return false;
    }
    static QSet<QString> loaded;
    static QPointer<QGuiApplication> owner;
    static bool initialized = false;
    if (owner != app) {
        loaded.clear();
        initialized = false;
        owner = app;
    }
    const QDir directory(fontDirectory.isEmpty()
        ? QStringLiteral("/usr/share/fonts/openwave/SourceSansPro") : fontDirectory);
    for (const QString &name : directory.entryList({QStringLiteral("SourceSansPro-*.otf")}, QDir::Files)) {
        const QString path = directory.absoluteFilePath(name);
        if (!loaded.contains(path) && QFontDatabase::addApplicationFont(path) >= 0)
            loaded.insert(path);
    }
    if (!QFontDatabase::families().contains(QStringLiteral("Source Sans Pro"), Qt::CaseInsensitive)) {
        qWarning() << "Source Sans Pro is unavailable; using Qt's system font fallback.";
        return false;
    }
    if (!initialized) {
        QFont font = QGuiApplication::font();
        font.setFamily(QStringLiteral("Source Sans Pro"));
        QGuiApplication::setFont(font);
        initialized = true;
    }
    return true;
}

QString FontManager::systemFamily() const { return QStringLiteral("Source Sans Pro"); }
QString FontManager::family() const { return QGuiApplication::font().family(); }
QFont FontManager::defaultFont() const { return QGuiApplication::font(); }

QStringList FontManager::families() const
{
    auto result = QFontDatabase::families();
    std::sort(result.begin(), result.end(), [](const QString &left, const QString &right) {
        return QString::localeAwareCompare(left, right) < 0;
    });
    return result;
}

bool FontManager::sourceSansProAvailable() const { return isAvailable(systemFamily()); }
bool FontManager::isAvailable(const QString &family) const
{
    return QFontDatabase::families().contains(family, Qt::CaseInsensitive);
}
QStringList FontManager::styles(const QString &family) const
{
    return isAvailable(family) ? QFontDatabase::styles(family) : QStringList{};
}
QFont FontManager::font(int pixelSize, int weight, bool italic, const QString &family) const
{
    QFont result = defaultFont();
    result.setStyleName(QString());
    if (!family.isEmpty() && isAvailable(family)) result.setFamily(family);
    result.setPixelSize(std::clamp(pixelSize, 1, 4096));
    result.setWeight(static_cast<QFont::Weight>(std::clamp(weight, 1, 1000)));
    result.setItalic(italic);
    return result;
}
bool FontManager::eventFilter(QObject *object, QEvent *event)
{
    if (object == QCoreApplication::instance() && event->type() == QEvent::ApplicationFontChange)
        emit fontChanged();
    return QObject::eventFilter(object, event);
}

}
