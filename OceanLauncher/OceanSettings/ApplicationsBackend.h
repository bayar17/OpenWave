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
    Q_PROPERTY(QVariantList fileTypes READ fileTypes NOTIFY fileTypesChanged)
    Q_PROPERTY(QString fileTypesError READ fileTypesError NOTIFY fileTypesChanged)
public:
    explicit ApplicationsBackend(QObject *parent = nullptr, bool handleDetailsRequests = true);
    QVariantList applications() const { return m_applications; }
    bool busy() const { return m_busy; }
    QString error() const { return m_uninstallError.isEmpty() ? m_error : m_uninstallError; }
    QVariantList fileTypes() const { return m_fileTypes; }
    QString fileTypesError() const { return m_fileTypesError; }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void clearDefaults(const QString &appId);
    Q_INVOKABLE void loadDetails(const QString &appId);
    Q_PROPERTY(QString storageUsage READ storageUsage NOTIFY storageChanged)
    QString storageUsage() const { return m_storageUsage; }

    Q_INVOKABLE void refreshFileTypes();
    Q_INVOKABLE void setFileDefault(const QString &extension, const QString &appId);
    bool uninstallBusy() const { return m_uninstalling || m_prompt.busy(); }
    Q_INVOKABLE void requestUninstall(const QString &appId);
    Q_INVOKABLE void cancelUninstallPrompt() { if (m_prompt.busy()) m_prompt.cancel(); }
signals:
    void changed();
    void storageChanged();
    void fileTypesChanged();
    void uninstalled(const QString &appId);
    void detailsRequested(const QString &appId);
private:
    ApplicationsClient m_client;
    QString m_storageUsage;
    quint64 m_storageGeneration = 0;
    OpenWavePromptClient m_prompt;
    QString m_pendingUninstall;
    bool m_uninstalling = false;
    QVariantList m_applications;
    QVariantList m_fileTypes;
    QString m_fileTypesError;
    QVariantList m_associationValues;
    bool m_associationsLoaded = false;
    void rebuildFileTypes();
    QString m_error;
    QString m_uninstallError;
    bool m_busy = false, m_again = false;
};
