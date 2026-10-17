#pragma once
#include <QObject>
#include <QHash>
class BrowserPreferences;

class OceanCookieManager : public QObject {
    Q_OBJECT
public:
    explicit OceanCookieManager(const QString &applicationPath, BrowserPreferences *preferences, QObject *parent = nullptr);
    bool ready() const { return m_error.isEmpty(); }
    QString error() const { return m_error; }
    QString profilePath() const { return m_profile; }
    Q_INVOKABLE void attachView(QObject *view, bool privateTab);
signals:
    void storageError(const QString &message);
private:
    QString m_profile, m_error;
    BrowserPreferences *m_preferences;
    QHash<QObject *, bool> m_views;
};
