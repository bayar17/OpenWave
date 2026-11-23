#pragma once

#include <QObject>
#include <QString>

class BrowserEngine : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int matches READ matches NOTIFY resultChanged)
    Q_PROPERTY(bool notFound READ notFound NOTIFY resultChanged)
    Q_PROPERTY(bool searching READ searching NOTIFY resultChanged)
    Q_PROPERTY(QString userAgent READ userAgent CONSTANT)

public:
    explicit BrowserEngine(QObject *parent = nullptr) : QObject(parent) {}

    int matches() const { return m_matches; }
    bool notFound() const { return m_notFound; }
    bool searching() const { return m_searching; }
    static QString mobileUserAgent();
    QString userAgent() const { return mobileUserAgent(); }

    Q_INVOKABLE bool configure(QObject *view);
    Q_INVOKABLE bool find(QObject *view, const QString &text);
    Q_INVOKABLE void findNext(QObject *view);
    Q_INVOKABLE void findPrevious(QObject *view);
    Q_INVOKABLE void clearFind(QObject *view);

    void reportMatches(void *controller, int matches);
    void reportMissing(void *controller);

signals:
    void resultChanged();

private:
    void *controllerFor(QObject *view);
    void reset();

    void *m_active = nullptr;
    int m_matches = 0;
    bool m_notFound = false;
    bool m_searching = false;
};
