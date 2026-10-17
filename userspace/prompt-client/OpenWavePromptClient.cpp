#include "OpenWavePromptClient.h"
#undef signals
#include <gio/gio.h>

OpenWavePromptClient::OpenWavePromptClient(QObject *parent) : QObject(parent)
{
    m_timer.setInterval(200);
    connect(&m_timer, &QTimer::timeout, this, &OpenWavePromptClient::poll);
}
void OpenWavePromptClient::message(const QString &title, const QString &body, const QVariantMap &options)
{ start(QStringLiteral("message"), title, body, options); }
void OpenWavePromptClient::confirmation(const QString &title, const QString &body, const QVariantMap &options)
{ start(QStringLiteral("confirmation"), title, body, options); }
void OpenWavePromptClient::textInput(const QString &title, const QString &body, const QVariantMap &options)
{ start(QStringLiteral("text_input"), title, body, options); }
void OpenWavePromptClient::passwordInput(const QString &title, const QString &body, const QVariantMap &options)
{
    QVariantMap secretOptions = options;
    secretOptions.insert(QStringLiteral("secret"), true);
    if (!secretOptions.contains(QStringLiteral("allowPasswordReveal")))
        secretOptions.insert(QStringLiteral("allowPasswordReveal"), true);
    start(QStringLiteral("text_input"), title, body, secretOptions);
}
void OpenWavePromptClient::form(const QString &title, const QString &body, const QVariantMap &options)
{ start(QStringLiteral("form"), title, body, options); }
void OpenWavePromptClient::selection(const QString &title, const QString &body, const QVariantMap &options)
{ start(QStringLiteral("selection"), title, body, options); }
void OpenWavePromptClient::start(const QString &type, const QString &title, const QString &body, QVariantMap options)
{
    if (m_busy) { emit errorOccurred(QStringLiteral("A prompt request is already outstanding.")); return; }
    options.insert(QStringLiteral("title"), title);
    options.insert(QStringLiteral("message"), body);
    m_type = type;
    m_busy = true; m_cancelPending = false; m_cancelSent = false; emit busyChanged();
    m_transport.call(QStringLiteral("Request"), {type, options}, [this](const QVariantList &values, const QString &error) {
        if (!error.isEmpty() || values.size() != 1 || values[0].toString().isEmpty()) {
            finish(QStringLiteral("service_lost"), {}, error.isEmpty() ? QStringLiteral("Invalid prompt response.") : error);
            return;
        }
        m_id = values[0].toString(); emit requestIdChanged(); m_timer.start();
        if (m_cancelPending) cancel(); else poll();
    });
}
void OpenWavePromptClient::cancel()
{
    if (!m_busy) return;
    m_cancelPending = true;
    if (m_id.isEmpty() || m_cancelSent) return;
    m_cancelSent = true;
    const QString id = m_id;
    m_transport.call(QStringLiteral("Cancel"), {id}, [this, id](const QVariantList &, const QString &error) {
        if (m_id != id) return;
        if (!error.isEmpty()) finish(QStringLiteral("service_lost"), {}, error);
        else poll();
    });
}
void OpenWavePromptClient::poll()
{
    if (!m_busy || m_polling || m_id.isEmpty()) return;
    m_polling = true;
    const QString id = m_id;
    m_transport.call(QStringLiteral("GetResult"), {id}, [this, id](const QVariantList &values, const QString &error) {
        m_polling = false;
        if (m_id != id) return;
        if (!error.isEmpty() || values.size() != 3) {
            finish(QStringLiteral("service_lost"), {}, error.isEmpty() ? QStringLiteral("Invalid prompt result.") : error);
        } else if (values[0].toBool()) {
            m_transport.call(QStringLiteral("Acknowledge"), {id}, [](const QVariantList &, const QString &) {});
            finish(m_cancelPending ? QStringLiteral("cancelled") : values[1].toString(),
                   m_cancelPending ? QString() : values[2].toString());
        }
    });
}
void OpenWavePromptClient::finish(const QString &result, const QString &text, const QString &error)
{
    const QString id = m_id;
    const bool form = m_type == QStringLiteral("form");
    m_type.clear();
    m_timer.stop(); m_id.clear(); m_busy = false; m_cancelPending = false;
    emit requestIdChanged(); emit busyChanged();
    if (!error.isEmpty()) emit errorOccurred(error);
    if (form) {
        QVariantMap values;
        if (result == QStringLiteral("accepted")) {
            GVariant *fields = g_variant_parse(G_VARIANT_TYPE("a{ss}"), text.toUtf8().constData(), nullptr, nullptr, nullptr);
            if (!fields) { emit fieldsFinished(id, QStringLiteral("service_lost"), {}); return; }
            GVariantIter iter; const char *key, *value;
            g_variant_iter_init(&iter, fields);
            while (g_variant_iter_next(&iter, "{&s&s}", &key, &value))
                values.insert(QString::fromUtf8(key), QString::fromUtf8(value));
            g_variant_unref(fields);
        }
        emit fieldsFinished(id, result, values);
    } else emit finished(id, result, text);
}
