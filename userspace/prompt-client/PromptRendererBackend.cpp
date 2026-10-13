#include "PromptRendererBackend.h"
#undef signals
#include <gio/gio.h>

PromptRendererBackend::PromptRendererBackend(QObject *parent) : QObject(parent)
{
    m_timer.setInterval(200);
    connect(&m_timer, &QTimer::timeout, this, &PromptRendererBackend::poll);
    m_timer.start(); poll();
}
void PromptRendererBackend::unavailable()
{
    m_registered = false; m_polling = false; m_busy = false;
    if (!m_request.isEmpty()) { m_request.clear(); emit changed(); }
}
void PromptRendererBackend::poll()
{
    if (m_polling || m_busy) return;
    m_polling = true;
    if (!m_registered) {
        m_transport.call(QStringLiteral("RegisterRenderer"), {}, [this](const QVariantList &, const QString &error) {
            if (!error.isEmpty()) { unavailable(); return; }
            m_registered = true; fetch();
        });
    } else fetch();
}
void PromptRendererBackend::fetch()
{
    m_transport.call(QStringLiteral("Next"), {}, [this](const QVariantList &values, const QString &error) {
        m_polling = false;
        if (!error.isEmpty() || values.size() != 3) { unavailable(); return; }
        QVariantMap next;
        if (!values[0].toString().isEmpty()) {
            next = values[2].toMap();
            next.insert(QStringLiteral("id"), values[0]);
            next.insert(QStringLiteral("type"), values[1]);
        }
        if (next != m_request) { m_request = next; emit changed(); }
    });
}
void PromptRendererBackend::respond(const QString &id, const QString &result, const QString &text)
{
    if (m_busy || id.isEmpty() || id != m_request.value(QStringLiteral("id")).toString()) return;
    m_busy = true; emit changed();
    m_transport.call(QStringLiteral("Respond"), {id, result, text}, [this](const QVariantList &, const QString &error) {
        m_busy = false;
        if (!error.isEmpty()) emit errorOccurred(error);
        emit changed(); poll();
    });
}
void PromptRendererBackend::respondFields(const QString &id, const QString &result, const QVariantMap &values)
{
    if (result != QStringLiteral("accepted")) { respond(id, result); return; }
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a{ss}"));
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        g_variant_builder_add(&builder, "{ss}", it.key().toUtf8().constData(), it.value().toString().toUtf8().constData());
    GVariant *fields = g_variant_ref_sink(g_variant_builder_end(&builder));
    char *text = g_variant_print(fields, FALSE);
    respond(id, result, QString::fromUtf8(text));
    g_free(text); g_variant_unref(fields);
}
