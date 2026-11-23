#pragma once

#include <QObject>
#include <QThreadPool>

class AppearanceApplier : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit AppearanceApplier(QObject *parent = nullptr);
    ~AppearanceApplier() override;

    bool busy() const { return m_busy; }
    Q_INVOKABLE void apply(const QString &id);

signals:
    void busyChanged();
    void applied(const QString &id);
    void failed(const QString &id, const QString &message);

private:
    QThreadPool m_pool;
    bool m_busy = false;
};
