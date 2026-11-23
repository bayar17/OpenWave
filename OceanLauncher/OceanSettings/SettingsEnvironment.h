#pragma once

#include <QObject>
#include <QPointer>
#include <QString>

class SettingsBackend;

class SettingsEnvironment : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY changed FINAL)
    Q_PROPERTY(bool largeText READ largeText NOTIFY changed FINAL)
    Q_PROPERTY(bool reduceMotion READ reduceMotion NOTIFY changed FINAL)

public:
    explicit SettingsEnvironment(SettingsBackend *backend, QObject *parent = nullptr);

    QString deviceName() const;
    bool largeText() const;
    bool reduceMotion() const;

signals:
    void changed();

private:
    QPointer<SettingsBackend> m_backend;
};
