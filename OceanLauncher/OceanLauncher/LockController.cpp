#include "LockController.h"

#include <QFile>
#include <QSaveFile>
#include <QTimer>
#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

LockController::LockController(QObject *parent)
    : LockController(QStringLiteral("/run/openwave/ocean-lock-state"), parent)
{
}

LockController::LockController(QString statePath, QObject *parent)
    : QObject(parent), m_statePath(std::move(statePath))
{
    struct stat stateInfo {};
    if (::lstat(m_statePath.toLocal8Bit().constData(), &stateInfo) != 0) {
        if (errno == ENOENT)
            return;
        m_state = Locked;
        return;
    }

    if (!S_ISREG(stateInfo.st_mode) || stateInfo.st_uid != geteuid()) {
        m_state = Locked;
        return;
    }

    QFile stateFile(m_statePath);
    if (!stateFile.open(QIODevice::ReadOnly) || stateFile.readAll() != QByteArrayLiteral("locked\n")) {
        m_state = Locked;
        return;
    }

    m_state = Locked;
}

LockController::State LockController::state() const
{
    return m_state;
}

bool LockController::locked() const
{
    return m_state != Unlocked;
}

bool LockController::unlockRequestActive() const
{
    return m_state == UnlockRequested || m_state == Authenticating || m_state == UnlockAuthorized;
}

bool LockController::unlockAuthorized() const
{
    return m_state == UnlockAuthorized;
}

bool LockController::developmentOnly() const
{
    return true;
}

bool LockController::requestLock()
{
    if (locked())
        return true;

    if (!persistLockedState()) {
        setState(Locked);
        emit persistenceError(QStringLiteral("Could not persist the lock state; the current session remains locked."));
        return false;
    }

    setState(Locked);
    return true;
}

bool LockController::requestDevelopmentSwipeUnlock()
{
    if (m_state != Locked)
        return false;

    setState(UnlockRequested);
    QTimer::singleShot(0, this, [this] {
        if (m_state != UnlockRequested)
            return;
        setState(Authenticating);
        QTimer::singleShot(0, this, [this] {
            if (m_state == Authenticating)
                setState(UnlockAuthorized);
        });
    });
    return true;
}

bool LockController::finishDevelopmentSwipeUnlock()
{
    if (m_state != UnlockAuthorized)
        return false;

    if (::unlink(m_statePath.toLocal8Bit().constData()) != 0 && errno != ENOENT) {
        emit persistenceError(QStringLiteral("Could not clear the lock state; the session remains locked."));
        setState(Locked);
        return false;
    }

    setState(Unlocked);
    return true;
}

void LockController::cancelUnlockRequest()
{
    if (m_state == UnlockRequested || m_state == Authenticating || m_state == UnlockAuthorized)
        setState(Locked);
}

bool LockController::persistLockedState()
{
    QSaveFile stateFile(m_statePath);
    if (!stateFile.open(QIODevice::WriteOnly))
        return false;
    stateFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (stateFile.write("locked\n") != 7)
        return false;
    return stateFile.commit();
}

void LockController::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged();
}
