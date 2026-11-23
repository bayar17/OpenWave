#pragma once

#include <QColor>
#include <functional>
#include <QFileSystemWatcher>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class QJSEngine;
class QQmlEngine;

class QuickMenuAppearance : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Appearance)
    QML_SINGLETON
    Q_PROPERTY(QString current READ current NOTIFY changed FINAL)
    Q_PROPERTY(QString name READ name NOTIFY changed FINAL)
    Q_PROPERTY(QVariantList presets READ presets NOTIFY presetsChanged FINAL)
    Q_PROPERTY(QUrl preview READ preview NOTIFY changed FINAL)
    Q_PROPERTY(QUrl wallpaper READ wallpaper NOTIFY changed FINAL)
    Q_PROPERTY(QColor scrim READ scrim NOTIFY changed FINAL)
    Q_PROPERTY(QColor accent READ accent NOTIFY changed FINAL)
    Q_PROPERTY(QColor activeColor READ activeColor NOTIFY changed FINAL)
    Q_PROPERTY(QColor inactiveColor READ inactiveColor NOTIFY changed FINAL)
    Q_PROPERTY(QColor textColor READ textColor NOTIFY changed FINAL)
    Q_PROPERTY(QColor secondaryTextColor READ secondaryTextColor NOTIFY changed FINAL)
    Q_PROPERTY(QColor sliderTrack READ sliderTrack NOTIFY changed FINAL)
    Q_PROPERTY(QColor sliderFill READ sliderFill NOTIFY changed FINAL)
    Q_PROPERTY(QColor sliderHandle READ sliderHandle NOTIFY changed FINAL)
    Q_PROPERTY(QColor warningColor READ warningColor NOTIFY changed FINAL)
    Q_PROPERTY(QColor selectionColor READ selectionColor NOTIFY changed FINAL)

public:
    struct Palette {
        QColor scrim;
        QColor accent;
        QColor active;
        QColor inactive;
        QColor text;
        QColor secondaryText;
        QColor sliderTrack;
        QColor sliderFill;
        QColor sliderHandle;
        QColor warning;
        QColor selection;
    };

    struct Preset {
        QString id;
        QString name;
        int order = 1000;
        double hue = 0.58;
        double saturation = 0.7;
        double lightness = 0.42;
        QColor scrimColor = QColor(0x10, 0x14, 0x18);
        double scrimAlpha = 0.5;
        QString preview;
        QString wallpaper;
        bool user = false;
        bool favorite = false;
        QString topColor;
        QString bottomColor;
    };

    QuickMenuAppearance(const QStringList &presetDirectories, const QString &preferencesPath,
                        QObject *parent = nullptr);
    QuickMenuAppearance() = delete;

    static QuickMenuAppearance *shared();
    static QuickMenuAppearance *create(QQmlEngine *engine, QJSEngine *scriptEngine);
    static QColor accentForHue(double hue);
    static QString userStoreDirectory();
    static QStringList defaultPresetDirectories();
    static double contrastRatio(const QColor &first, const QColor &second);
    static double minimumContrast();
    static double minimumGraphicContrast();

    QString current() const;
    QString name() const;
    QVariantList presets() const;
    QUrl preview() const;
    QUrl wallpaper() const;
    QColor scrim() const;
    QColor accent() const;
    QColor activeColor() const;
    QColor inactiveColor() const;
    QColor textColor() const;
    QColor secondaryTextColor() const;
    QColor sliderTrack() const;
    QColor sliderFill() const;
    QColor sliderHandle() const;
    QColor warningColor() const;
    QColor selectionColor() const;

    using PreferenceWriter = std::function<bool(const QString &key, const QVariant &value)>;

    Q_INVOKABLE bool select(const QString &id);
    QStringList favorites() const;
    bool setFavorite(const QString &id, bool favorite);
    void setPreferenceWriter(PreferenceWriter writer);
    Q_INVOKABLE void refresh();
    QVariantList rejections() const;
    void setPresetDirectories(const QStringList &directories);
    void setPreferencesPath(const QString &path);
    void setMarkerPath(const QString &path);
    void reload();

signals:
    void changed();
    void presetsChanged();
    void presetRejected(const QString &path, const QString &reason);

private:
    void load();
    void loadSelection();
    bool parse(const QString &directory, const QString &path, Preset &preset, QString &reason) const;
    const Preset *find(const QString &id) const;
    QColor scrimFor(const Preset &preset) const;
    QColor accentFor(const Preset &preset) const;
    Palette paletteFor(const Preset *preset) const;
    void watchPaths();

    QStringList m_directories;
    QString m_preferencesPath;
    QString m_markerPath;
    QList<Preset> m_presets;
    QVariantList m_rejections;
    QString m_current;
    QStringList m_favorites;
    PreferenceWriter m_writer;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
};
