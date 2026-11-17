#pragma once

#include <QObject>
#include <QThreadPool>
#include <QVariantList>
#include <functional>

class PromptTransport : public QObject
{
public:
    using Completion = std::function<void(const QVariantList &, const QString &)>;
    explicit PromptTransport(QObject *parent = nullptr);
    ~PromptTransport() override;
    void call(const QString &method, const QVariantList &arguments, Completion completion);
private:
    QThreadPool m_pool;
    void *m_bus = nullptr;
};
