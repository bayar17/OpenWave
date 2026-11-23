#pragma once

#include <QString>
#include <QVariant>
#include <QVariantList>
#include <functional>
#include <memory>

class PreferenceBroker
{
public:
    using Completion = std::function<void(const QVariantList &values, const QString &error)>;
    using Call = std::function<void(const QString &method, const QVariantList &arguments, Completion completion)>;

    explicit PreferenceBroker(Call call, int timeoutMs = 4000);

    bool write(const QString &key, const QVariant &value);
    QString lastError() const { return m_error; }

    static bool brokered();
    static QString lastFailure();

private:
    Call m_call;
    int m_timeoutMs;
    bool m_busy = false;
    QString m_error;
};
