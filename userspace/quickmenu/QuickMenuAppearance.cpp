#include "QuickMenuAppearance.h"

#include "DesktopEntry.h"

#include <OpenWave/application-data.h>
#include <OpenWave/preferences.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJSEngine>
#include <QLocale>
#include <QMap>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <algorithm>
#include <cmath>
#include <pwd.h>
#include <unistd.h>

namespace {

constexpr qint64 MaxPresetBytes = 16 * 1024;
constexpr double MinimumContrast = 4.5;
constexpr double MinimumGraphicContrast = 3.0;
constexpr double MaximumLightness = 0.62;
const QString CurrentKey = QStringLiteral("appearance/current");
const QString FavoritesKey = QStringLiteral("appearance/favorites");

double channelLuminance(double channel)
{
    return channel <= 0.03928 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
}

double luminance(const QColor &color)
{
    return 0.2126 * channelLuminance(color.redF()) + 0.7152 * channelLuminance(color.greenF())
        + 0.0722 * channelLuminance(color.blueF());
}

QColor blend(const QColor &top, double alpha, const QColor &bottom)
{
    return QColor::fromRgbF(top.redF() * alpha + bottom.redF() * (1 - alpha),
                            top.greenF() * alpha + bottom.greenF() * (1 - alpha),
                            top.blueF() * alpha + bottom.blueF() * (1 - alpha));
}

}

QuickMenuAppearance::QuickMenuAppearance(const QStringList &presetDirectories,
                                         const QString &preferencesPath, QObject *parent)
    : QObject(parent)
    , m_directories(presetDirectories)
    , m_preferencesPath(preferencesPath)
    , m_markerPath(userStoreDirectory().isEmpty() ? QString() : userStoreDirectory() + QStringLiteral("/current"))
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(150);
    connect(&m_debounce, &QTimer::timeout, this, [this] {
        const QVariantList before = presets();
        const QString beforeCurrent = m_current;
        load();
        const bool listChanged = before != presets();
        if (listChanged)
            emit presetsChanged();
        if (listChanged || beforeCurrent != m_current)
            emit changed();
        watchPaths();
    });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &) {
        m_debounce.start();
    });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString &) {
        m_debounce.start();
    });
    load();
    watchPaths();
}

QuickMenuAppearance *QuickMenuAppearance::shared()
{
    static QuickMenuAppearance *instance = new QuickMenuAppearance(
        defaultPresetDirectories(), openwavePreferencesPath(), QCoreApplication::instance());
    return instance;
}

QuickMenuAppearance *QuickMenuAppearance::create(QQmlEngine *, QJSEngine *)
{
    QuickMenuAppearance *instance = shared();
    QJSEngine::setObjectOwnership(instance, QJSEngine::CppOwnership);
    return instance;
}

QString QuickMenuAppearance::userStoreDirectory()
{
    QString name = qEnvironmentVariable("OPENWAVE_SESSION_USER");
    uid_t uid = geteuid();
    if (!name.isEmpty()) {
        const passwd *entry = getpwnam(name.toLocal8Bit().constData());
        if (!entry)
            return QString();
        uid = entry->pw_uid;
    }
    return QStringLiteral("/var/openwave/appearances/users/%1").arg(uid);
}

QStringList QuickMenuAppearance::defaultPresetDirectories()
{
    QStringList directories{QStringLiteral("/System/QuickMenu/Appearances")};
    const QString store = userStoreDirectory();
    if (!store.isEmpty())
        directories.append(store);
    return directories;
}

double QuickMenuAppearance::contrastRatio(const QColor &first, const QColor &second)
{
    const double a = luminance(first);
    const double b = luminance(second);
    return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
}

double QuickMenuAppearance::minimumContrast()
{
    return MinimumContrast;
}

double QuickMenuAppearance::minimumGraphicContrast()
{
    return MinimumGraphicContrast;
}

QString QuickMenuAppearance::current() const
{
    return m_current;
}

QString QuickMenuAppearance::name() const
{
    const Preset *preset = find(m_current);
    return preset ? preset->name : QString();
}

QuickMenuAppearance::Palette QuickMenuAppearance::paletteFor(const Preset *preset) const
{
    Palette palette;
    palette.scrim = preset ? scrimFor(*preset) : QColor(0x10, 0x14, 0x18, 0xb3);
    palette.accent = preset ? accentFor(*preset) : QColor::fromHslF(0.58, 0.7, 0.42);
    palette.active = palette.accent;
    palette.inactive = QColor(0x8a, 0x8d, 0x94, 0x99);
    palette.text = QColor(Qt::white);
    palette.secondaryText = QColor(255, 255, 255, 0xc0);
    palette.sliderTrack = QColor(255, 255, 255, 0x59);
    palette.sliderFill = QColor(Qt::white);
    const qreal hue = palette.accent.hslHueF();
    palette.sliderHandle = QColor::fromHslF(hue < 0 ? 0 : hue, 0.25, 0.9);
    palette.warning = QColor(0xbb, 0x4b, 0x56);
    palette.selection = QColor(Qt::white);
    return palette;
}

QVariantList QuickMenuAppearance::presets() const
{
    QVariantList result;
    for (const Preset &preset : m_presets) {
        const Palette palette = paletteFor(&preset);
        result.append(QVariantMap{
            {QStringLiteral("id"), preset.id},
            {QStringLiteral("name"), preset.name},
            {QStringLiteral("preview"), preset.preview.isEmpty() ? QUrl() : QUrl::fromLocalFile(preset.preview)},
            {QStringLiteral("wallpaper"), preset.wallpaper.isEmpty() ? QUrl() : QUrl::fromLocalFile(preset.wallpaper)},
            {QStringLiteral("thumbnail"), QUrl::fromLocalFile(preset.preview.isEmpty() ? preset.wallpaper : preset.preview)},
            {QStringLiteral("user"), preset.user},
            {QStringLiteral("favorite"), preset.favorite},
            {QStringLiteral("scrim"), palette.scrim},
            {QStringLiteral("accent"), palette.accent},
            {QStringLiteral("activeColor"), palette.active},
            {QStringLiteral("inactiveColor"), palette.inactive},
            {QStringLiteral("textColor"), palette.text},
            {QStringLiteral("secondaryTextColor"), palette.secondaryText},
            {QStringLiteral("sliderTrack"), palette.sliderTrack},
            {QStringLiteral("sliderFill"), palette.sliderFill},
            {QStringLiteral("sliderHandle"), palette.sliderHandle},
            {QStringLiteral("warningColor"), palette.warning},
            {QStringLiteral("selectionColor"), palette.selection},
            {QStringLiteral("topColor"), preset.topColor.isEmpty() ? palette.scrim : QColor(preset.topColor)},
            {QStringLiteral("bottomColor"), preset.bottomColor.isEmpty() ? palette.accent : QColor(preset.bottomColor)}
        });
    }
    return result;
}

QUrl QuickMenuAppearance::preview() const
{
    const Preset *preset = find(m_current);
    return preset && !preset->preview.isEmpty() ? QUrl::fromLocalFile(preset->preview) : QUrl();
}

QUrl QuickMenuAppearance::wallpaper() const
{
    const Preset *preset = find(m_current);
    return preset && !preset->wallpaper.isEmpty() ? QUrl::fromLocalFile(preset->wallpaper) : QUrl();
}

QColor QuickMenuAppearance::scrim() const { return paletteFor(find(m_current)).scrim; }
QColor QuickMenuAppearance::accent() const { return paletteFor(find(m_current)).accent; }
QColor QuickMenuAppearance::activeColor() const { return paletteFor(find(m_current)).active; }
QColor QuickMenuAppearance::inactiveColor() const { return paletteFor(find(m_current)).inactive; }
QColor QuickMenuAppearance::textColor() const { return paletteFor(find(m_current)).text; }
QColor QuickMenuAppearance::secondaryTextColor() const { return paletteFor(find(m_current)).secondaryText; }
QColor QuickMenuAppearance::sliderTrack() const { return paletteFor(find(m_current)).sliderTrack; }
QColor QuickMenuAppearance::sliderFill() const { return paletteFor(find(m_current)).sliderFill; }
QColor QuickMenuAppearance::sliderHandle() const { return paletteFor(find(m_current)).sliderHandle; }
QColor QuickMenuAppearance::warningColor() const { return paletteFor(find(m_current)).warning; }
QColor QuickMenuAppearance::selectionColor() const { return paletteFor(find(m_current)).selection; }

bool QuickMenuAppearance::select(const QString &id)
{
    if (!find(id))
        return false;
    {
        QSettings settings(m_preferencesPath, QSettings::IniFormat);
        settings.setValue(CurrentKey, id);
        settings.sync();
    }
    if (m_current != id) {
        m_current = id;
        emit changed();
    }
    return true;
}

QVariantList QuickMenuAppearance::rejections() const
{
    return m_rejections;
}

void QuickMenuAppearance::setPresetDirectories(const QStringList &directories)
{
    m_directories = directories;
    reload();
    watchPaths();
}

void QuickMenuAppearance::setMarkerPath(const QString &path)
{
    m_markerPath = path;
    reload();
    watchPaths();
}

void QuickMenuAppearance::setPreferencesPath(const QString &path)
{
    m_preferencesPath = path;
    reload();
    watchPaths();
}

void QuickMenuAppearance::refresh()
{
    const QVariantList before = presets();
    const QString beforeCurrent = m_current;
    load();
    const bool listChanged = before != presets();
    if (listChanged)
        emit presetsChanged();
    if (listChanged || beforeCurrent != m_current)
        emit changed();
    watchPaths();
}

void QuickMenuAppearance::reload()
{
    load();
    emit presetsChanged();
    emit changed();
}

void QuickMenuAppearance::load()
{
    m_presets.clear();
    m_rejections.clear();
    QSet<QString> seen;
    const auto reject = [this](const QString &path, const QString &reason) {
        m_rejections.append(QVariantMap{{QStringLiteral("path"), path}, {QStringLiteral("reason"), reason}});
        emit presetRejected(path, reason);
    };
    for (int directoryIndex = 0; directoryIndex < m_directories.size(); ++directoryIndex) {
        const QFileInfo info(m_directories.at(directoryIndex));
        if (!info.isDir())
            continue;
        const QString canonical = info.canonicalFilePath();
        const QFileInfoList files = QDir(canonical).entryInfoList(
            {QStringLiteral("*.desktop")}, QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &file : files) {
            Preset preset;
            QString reason;
            if (file.isSymLink()) {
                reject(file.absoluteFilePath(), QStringLiteral("symbolic links are not allowed"));
                continue;
            }
            if (!parse(canonical, file.absoluteFilePath(), preset, reason)) {
                reject(file.absoluteFilePath(), reason);
                continue;
            }
            if (seen.contains(preset.id)) {
                reject(file.absoluteFilePath(), QStringLiteral("duplicate appearance id \"%1\"").arg(preset.id));
                continue;
            }
            seen.insert(preset.id);
            preset.user = directoryIndex > 0;
            m_presets.append(preset);
        }
    }
    std::stable_sort(m_presets.begin(), m_presets.end(), [](const Preset &left, const Preset &right) {
        if (left.order != right.order)
            return left.order < right.order;
        return left.id < right.id;
    });
    loadSelection();
    QSettings favoriteSettings(m_preferencesPath, QSettings::IniFormat);
    const QStringList saved = favoriteSettings.value(FavoritesKey).toStringList();
    m_favorites.clear();
    for (Preset &preset : m_presets) {
        preset.favorite = saved.contains(preset.id);
        if (preset.favorite)
            m_favorites.append(preset.id);
    }
    std::stable_partition(m_presets.begin(), m_presets.end(), [](const Preset &preset) { return preset.favorite; });
}

QStringList QuickMenuAppearance::favorites() const
{
    return m_favorites;
}

void QuickMenuAppearance::setPreferenceWriter(PreferenceWriter writer)
{
    m_writer = std::move(writer);
}

bool QuickMenuAppearance::setFavorite(const QString &id, bool favorite)
{
    if (!find(id))
        return false;
    QStringList next = m_favorites;
    next.removeAll(id);
    if (favorite)
        next.append(id);
    bool stored = false;
    if (m_writer) {
        stored = m_writer(FavoritesKey, next);
    } else {
        QSettings settings(m_preferencesPath, QSettings::IniFormat);
        if (next.isEmpty())
            settings.remove(FavoritesKey);
        else
            settings.setValue(FavoritesKey, next);
        settings.sync();
        stored = settings.status() == QSettings::NoError;
    }
    if (!stored)
        return false;
    const QString before = m_current;
    load();
    emit presetsChanged();
    if (before != m_current)
        emit changed();
    return true;
}

void QuickMenuAppearance::loadSelection()
{
    static const QRegularExpression idPattern(QStringLiteral("^[a-z][a-z0-9-]{0,31}$"));
    QString recorded;
    if (!m_markerPath.isEmpty()) {
        QFile marker(m_markerPath);
        if (marker.open(QIODevice::ReadOnly))
            recorded = QString::fromUtf8(marker.read(64)).trimmed();
    }
    if (idPattern.match(recorded).hasMatch() && find(recorded)) {
        m_current = recorded;
        return;
    }
    QSettings settings(m_preferencesPath, QSettings::IniFormat);
    const QString saved = settings.value(CurrentKey).toString();
    if (find(saved))
        m_current = saved;
    else if (!m_presets.isEmpty())
        m_current = m_presets.first().id;
    else
        m_current.clear();
}

bool QuickMenuAppearance::parse(const QString &directory, const QString &path, Preset &preset,
                                QString &reason) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > MaxPresetBytes) {
        reason = QStringLiteral("cannot read the file or it is larger than %1 bytes").arg(MaxPresetBytes);
        return false;
    }
    const DesktopEntry::Fields entry = DesktopEntry::parse(file.readAll());
    if (entry.isEmpty()) {
        reason = QStringLiteral("missing [Desktop Entry] group");
        return false;
    }
    if (entry.value(QStringLiteral("Type")) != QLatin1String("X-OpenWave-Appearance")) {
        reason = QStringLiteral("Type must be X-OpenWave-Appearance");
        return false;
    }
    static const QRegularExpression idPattern(QStringLiteral("^[a-z][a-z0-9-]{0,31}$"));
    preset.id = QFileInfo(path).completeBaseName();
    if (!idPattern.match(preset.id).hasMatch()) {
        reason = QStringLiteral("the file name must be a lowercase appearance id");
        return false;
    }
    preset.name = DesktopEntry::localized(entry, QStringLiteral("Name")).trimmed();
    if (preset.name.isEmpty() || preset.name.size() > 32) {
        reason = QStringLiteral("Name must be 1 to 32 characters");
        return false;
    }
    double order = preset.order;
    const QString prefix = QStringLiteral("X-OpenWave-Appearance-");
    if (!DesktopEntry::readNumber(entry, prefix + QStringLiteral("Order"), 0, 1000000, order, reason)
        || !DesktopEntry::readNumber(entry, prefix + QStringLiteral("Hue"), 0, 1, preset.hue, reason)
        || !DesktopEntry::readNumber(entry, prefix + QStringLiteral("Saturation"), 0, 1, preset.saturation, reason)
        || !DesktopEntry::readNumber(entry, prefix + QStringLiteral("Lightness"), 0, 1, preset.lightness, reason)
        || !DesktopEntry::readNumber(entry, prefix + QStringLiteral("ScrimAlpha"), 0, 1, preset.scrimAlpha, reason)
        || !DesktopEntry::readColor(entry, prefix + QStringLiteral("ScrimColor"), preset.scrimColor, reason))
        return false;
    preset.order = static_cast<int>(order);
    QColor top;
    QColor bottom;
    if (!DesktopEntry::readColor(entry, prefix + QStringLiteral("TopColor"), top, reason)
        || !DesktopEntry::readColor(entry, prefix + QStringLiteral("BottomColor"), bottom, reason))
        return false;
    preset.topColor = top.isValid() ? top.name() : QString();
    preset.bottomColor = bottom.isValid() ? bottom.name() : QString();
    const auto resolveImage = [&directory, &reason](const QString &name, const QString &key, QString &result) {
        if (name.isEmpty())
            return true;
        if (name.contains(QLatin1Char('/')) || name.startsWith(QLatin1Char('.'))) {
            reason = QStringLiteral("%1 must be a file name next to the .desktop file").arg(key);
            return false;
        }
        const QFileInfo info(directory + QLatin1Char('/') + name);
        if (!info.isFile() || info.isSymLink()) {
            reason = QStringLiteral("%1 %2 is missing or not a regular file").arg(key, name);
            return false;
        }
        result = info.canonicalFilePath();
        return true;
    };
    if (!resolveImage(entry.value(prefix + QStringLiteral("Preview")), QStringLiteral("Preview"), preset.preview)
        || !resolveImage(entry.value(prefix + QStringLiteral("Wallpaper")), QStringLiteral("Wallpaper"), preset.wallpaper))
        return false;
    return true;
}

const QuickMenuAppearance::Preset *QuickMenuAppearance::find(const QString &id) const
{
    for (const Preset &preset : m_presets) {
        if (preset.id == id)
            return &preset;
    }
    return nullptr;
}

QColor QuickMenuAppearance::scrimFor(const Preset &preset) const
{
    double alpha = preset.scrimAlpha;
    const QColor white(Qt::white);
    while (alpha < 0.92 && contrastRatio(blend(preset.scrimColor, alpha, white), white) < MinimumContrast)
        alpha += 0.01;
    QColor result = preset.scrimColor;
    result.setAlphaF(alpha);
    return result;
}

QColor QuickMenuAppearance::accentForHue(double hue)
{
    Preset preset;
    preset.hue = std::max(0.0, std::min(1.0, hue));
    const QColor white(Qt::white);
    double lightness = std::min(preset.lightness, MaximumLightness);
    QColor color = QColor::fromHslF(preset.hue, preset.saturation, lightness);
    while (lightness > 0.05 && contrastRatio(color, white) < MinimumGraphicContrast) {
        lightness -= 0.01;
        color = QColor::fromHslF(preset.hue, preset.saturation, lightness);
    }
    return color;
}

QColor QuickMenuAppearance::accentFor(const Preset &preset) const
{
    const QColor white(Qt::white);
    double lightness = std::min(preset.lightness, MaximumLightness);
    QColor color = QColor::fromHslF(preset.hue, preset.saturation, lightness);
    while (lightness > 0.05 && contrastRatio(color, white) < MinimumGraphicContrast) {
        lightness -= 0.01;
        color = QColor::fromHslF(preset.hue, preset.saturation, lightness);
    }
    return color;
}

void QuickMenuAppearance::watchPaths()
{
    const QString directory = QFileInfo(m_preferencesPath).absolutePath();
    if (QDir(directory).exists() && !m_watcher.directories().contains(directory))
        m_watcher.addPath(directory);
    if (QFile::exists(m_preferencesPath) && !m_watcher.files().contains(m_preferencesPath))
        m_watcher.addPath(m_preferencesPath);
    if (!m_markerPath.isEmpty() && QFile::exists(m_markerPath) && !m_watcher.files().contains(m_markerPath))
        m_watcher.addPath(m_markerPath);
    for (const QString &presets : std::as_const(m_directories)) {
        if (QDir(presets).exists() && !m_watcher.directories().contains(presets))
            m_watcher.addPath(presets);
    }
}
