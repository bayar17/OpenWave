#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class SettingsBackend;

class SettingsGeneralService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY changed FINAL)
    Q_PROPERTY(QVariantMap systemInfo READ systemInfo NOTIFY changed FINAL)
    Q_PROPERTY(bool powerActionsAvailable READ powerActionsAvailable NOTIFY changed FINAL)
    Q_PROPERTY(bool powerBusy READ powerBusy NOTIFY changed FINAL)
    Q_PROPERTY(bool timeServiceAvailable READ timeServiceAvailable NOTIFY changed FINAL)
    Q_PROPERTY(bool timeBusy READ timeBusy NOTIFY changed FINAL)
    Q_PROPERTY(QString timezone READ timezone NOTIFY changed FINAL)
    Q_PROPERTY(QStringList zones READ zones NOTIFY changed FINAL)

public:
    SettingsGeneralService(SettingsBackend *settings, QObject *time, QObject *connectivity,
                           QObject *parent = nullptr);

    QString deviceName() const;
    QVariantMap systemInfo() const;
    bool powerActionsAvailable() const;
    bool powerBusy() const;
    bool timeServiceAvailable() const;
    bool timeBusy() const;
    QString timezone() const;
    QStringList zones() const;

    Q_INVOKABLE bool saveDeviceName(const QString &name);
    Q_INVOKABLE bool setTimezone(const QString &zone);
    Q_INVOKABLE bool requestPowerAction(const QString &action);
    Q_INVOKABLE void refreshTimeZones();

signals:
    void changed();
    void errorOccurred(const QString &message);

private:
    QPointer<SettingsBackend> m_settings;
    QPointer<QObject> m_time;
    QPointer<QObject> m_connectivity;
};
