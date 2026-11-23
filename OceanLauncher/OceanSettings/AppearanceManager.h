#pragma once

#include <QColor>
#include <QObject>
#include <QThreadPool>
#include <QUrl>
#include <QVariantList>

class QuickMenuAppearance;

class AppearanceManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList presets READ presets NOTIFY presetsChanged)
    Q_PROPERTY(QString current READ current NOTIFY currentChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit AppearanceManager(QuickMenuAppearance *appearance, QObject *parent = nullptr);
    ~AppearanceManager() override;

    QVariantList presets() const;
    QString current() const;
    bool busy() const { return m_busy; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void set(const QString &id);
    Q_INVOKABLE void save(const QString &id, const QString &name, double hue, const QUrl &image,
                          double x, double y, double width, double height);
    Q_INVOKABLE void remove(const QString &id);
    Q_INVOKABLE bool setFavorite(const QString &id, bool favorite);
    Q_INVOKABLE QColor accentForHue(double hue) const;
    Q_INVOKABLE QVariantMap imageSize(const QUrl &url) const;

signals:
    void presetsChanged();
    void currentChanged();
    void busyChanged();
    void finished(const QString &operation, const QString &id);
    void failed(const QString &operation, const QString &message);

private:
    struct Outcome {
        QString id;
        QString error;
    };
    void run(const QString &operation, std::function<Outcome()> work);
    static QByteArray cropToPng(const QUrl &image, double x, double y, double width, double height,
                                QString &error);
    QuickMenuAppearance *m_appearance;
    QThreadPool m_pool;
    bool m_busy = false;
};
