#include "PreferenceBroker.h"

#include <QDebug>
#include <QEventLoop>
#include <QMetaType>
#include <QTimer>
#include <unistd.h>

namespace {
QString failureText;
}

QString PreferenceBroker::lastFailure()
{
    return failureText;
}

PreferenceBroker::PreferenceBroker(Call call, int timeoutMs)
    : m_call(std::move(call))
    , m_timeoutMs(timeoutMs)
{
}

bool PreferenceBroker::brokered()
{
    return geteuid() && qEnvironmentVariableIsSet("OPENWAVE_SESSION_USER");
}

bool PreferenceBroker::write(const QString &key, const QVariant &value)
{
    if (m_busy) {
        m_error = QStringLiteral("Another preference is still being saved.");
        failureText = m_error;
        return false;
    }
    QString method;
    QVariantList arguments{key};
    switch (value.metaType().id()) {
    case QMetaType::Bool:
        method = QStringLiteral("SetPreferenceBool");
        arguments.append(value.toBool());
        break;
    case QMetaType::QString:
        method = QStringLiteral("SetPreferenceString");
        arguments.append(value.toString());
        break;
    case QMetaType::QStringList:
        method = QStringLiteral("SetPreferenceList");
        arguments.append(value.toStringList());
        break;
    case QMetaType::UnknownType:
        method = QStringLiteral("ResetPreferenceGroup");
        break;
    default:
        m_error = QStringLiteral("This preference type cannot be saved.");
        failureText = m_error;
        return false;
    }
    if (!m_call) {
        m_error = QStringLiteral("The preference service is unavailable.");
        failureText = m_error;
        return false;
    }

    struct State {
        bool done = false;
        QString error;
        QEventLoop loop;
    };
    auto state = std::make_shared<State>();
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &state->loop, [state] {
        if (!state->done) {
            state->done = true;
            state->error = QStringLiteral("The preference service did not answer in time.");
        }
        state->loop.quit();
    });
    m_busy = true;
    m_call(method, arguments, [state](const QVariantList &, const QString &error) {
        if (state->done)
            return;
        state->done = true;
        state->error = error;
        state->loop.quit();
    });
    if (!state->done) {
        timeout.start(m_timeoutMs);
        state->loop.exec();
    }
    m_busy = false;
    m_error = state->error;
    if (!m_error.isEmpty())
        failureText = m_error;
    if (!m_error.isEmpty())
        qWarning().noquote() << "PreferenceBroker:" << method << key << "failed:" << m_error;
    return m_error.isEmpty();
}
