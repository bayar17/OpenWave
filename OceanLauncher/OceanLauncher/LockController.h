#pragma once

#include <QObject>
#include <QString>

class LockController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool locked READ locked NOTIFY stateChanged)
    Q_PROPERTY(bool unlockRequestActive READ unlockRequestActive NOTIFY stateChanged)
    Q_PROPERTY(bool unlockAuthorized READ unlockAuthorized NOTIFY stateChanged)
    Q_PROPERTY(bool developmentOnly READ developmentOnly CONSTANT)

public:
    enum State {
        Unlocked,
        Locked,
        UnlockRequested,
        Authenticating,
        UnlockAuthorized
    };
    Q_ENUM(State)

    explicit LockController(QObject *parent = nullptr);
    explicit LockController(QString statePath, QObject *parent = nullptr);

    State state() const;
    bool locked() const;
    bool unlockRequestActive() const;
    bool unlockAuthorized() const;
    bool developmentOnly() const;

    Q_INVOKABLE bool requestLock();
    Q_INVOKABLE bool requestDevelopmentSwipeUnlock();
    Q_INVOKABLE bool finishDevelopmentSwipeUnlock();
    Q_INVOKABLE void cancelUnlockRequest();

signals:
    void stateChanged();
    void persistenceError(const QString &message);

private:
    bool persistLockedState();
    void setState(State state);

    QString m_statePath;
    State m_state = Unlocked;
};
