#pragma once
#include <QObject>
#include <QVariantList>
#include <OpenWave/ApplicationsClient.h>
#include <OpenWave/OpenWavePromptClient.h>

class ApplicationsBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList applications READ applications NOTIFY changed)
    Q_PROPERTY(bool uninstallBusy READ uninstallBusy NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit ApplicationsBackend(QObject *parent = nullptr);
    QVariantList applications() const { return m_applications; }
    bool busy() const { return m_busy; }
    QString error() const { return m_uninstallError.isEmpty() ? m_error : m_uninstallError; }
    Q_INVOKABLE void refresh();
    bool uninstallBusy() const { return m_uninstalling || m_prompt.busy(); }
    Q_INVOKABLE void requestUninstall(const QString &appId);
    Q_INVOKABLE void cancelUninstallPrompt() { if (m_prompt.busy()) m_prompt.cancel(); }
signals:
    void changed();
    void uninstalled(const QString &appId);
private:
    ApplicationsClient m_client;
    OpenWavePromptClient m_prompt;
    QString m_pendingUninstall;
    bool m_uninstalling = false;
    QVariantList m_applications;
    QString m_error;
    QString m_uninstallError;
    bool m_busy = false, m_again = false;
};
