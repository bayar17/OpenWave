#include "BrowserEngine.h"

#include <QMetaObject>
#undef signals
#include <wpe/webkit.h>

namespace {

constexpr const char *ConnectedKey = "ocean-find-connected";
constexpr guint MaximumMatches = 1000;

WebKitWebView *nativeView(QObject *view)
{
    if (!view)
        return nullptr;
    WebKitWebView *native = nullptr;
    if (!QMetaObject::invokeMethod(view, "webView", Qt::DirectConnection,
                                   QGenericReturnArgument("WebKitWebView*", &native)))
        return nullptr;
    return native;
}

void onFound(WebKitFindController *controller, guint count, gpointer data)
{
    static_cast<BrowserEngine *>(data)->reportMatches(controller, static_cast<int>(count));
}

void onMissing(WebKitFindController *controller, gpointer data)
{
    static_cast<BrowserEngine *>(data)->reportMissing(controller);
}

void onCounted(WebKitFindController *controller, guint count, gpointer data)
{
    static_cast<BrowserEngine *>(data)->reportMatches(controller, static_cast<int>(count));
}

}

QString BrowserEngine::mobileUserAgent()
{
    return QStringLiteral("Mozilla/5.0 (Linux; OpenWave Mobile) AppleWebKit/605.1.15 (KHTML, like Gecko) "
                          "Version/17.4 Mobile Safari/605.1.15 OceanBrowser/1.0");
}

bool BrowserEngine::configure(QObject *view)
{
    WebKitWebView *native = nativeView(view);
    if (!native)
        return false;
    WebKitSettings *settings = webkit_web_view_get_settings(native);
    if (!settings)
        return false;
    webkit_settings_set_user_agent(settings, mobileUserAgent().toUtf8().constData());
    return true;
}

void *BrowserEngine::controllerFor(QObject *view)
{
    WebKitWebView *native = nativeView(view);
    if (!native)
        return nullptr;
    WebKitFindController *controller = webkit_web_view_get_find_controller(native);
    if (controller && !g_object_get_data(G_OBJECT(controller), ConnectedKey)) {
        g_signal_connect(controller, "found-text", G_CALLBACK(onFound), this);
        g_signal_connect(controller, "failed-to-find-text", G_CALLBACK(onMissing), this);
        g_signal_connect(controller, "counted-matches", G_CALLBACK(onCounted), this);
        g_object_set_data(G_OBJECT(controller), ConnectedKey, GINT_TO_POINTER(1));
    }
    return controller;
}

void BrowserEngine::reset()
{
    const bool changed = m_matches != 0 || m_notFound || m_searching;
    m_matches = 0;
    m_notFound = false;
    m_searching = false;
    if (changed)
        emit resultChanged();
}

bool BrowserEngine::find(QObject *view, const QString &text)
{
    auto *controller = static_cast<WebKitFindController *>(controllerFor(view));
    if (!controller)
        return false;
    const QString needle = text.left(256);
    if (m_active && m_active != controller)
        webkit_find_controller_search_finish(static_cast<WebKitFindController *>(m_active));
    m_active = controller;
    if (needle.isEmpty()) {
        webkit_find_controller_search_finish(controller);
        reset();
        return true;
    }
    m_searching = true;
    m_notFound = false;
    emit resultChanged();
    webkit_find_controller_search(controller, needle.toUtf8().constData(),
                                  static_cast<guint>(WEBKIT_FIND_OPTIONS_CASE_INSENSITIVE | WEBKIT_FIND_OPTIONS_WRAP_AROUND),
                                  MaximumMatches);
    return true;
}

void BrowserEngine::findNext(QObject *view)
{
    if (auto *controller = static_cast<WebKitFindController *>(controllerFor(view)))
        webkit_find_controller_search_next(controller);
}

void BrowserEngine::findPrevious(QObject *view)
{
    if (auto *controller = static_cast<WebKitFindController *>(controllerFor(view)))
        webkit_find_controller_search_previous(controller);
}

void BrowserEngine::clearFind(QObject *view)
{
    if (auto *controller = static_cast<WebKitFindController *>(controllerFor(view)))
        webkit_find_controller_search_finish(controller);
    m_active = nullptr;
    reset();
}

void BrowserEngine::reportMatches(void *controller, int matches)
{
    if (controller != m_active)
        return;
    m_matches = matches;
    m_notFound = false;
    m_searching = false;
    emit resultChanged();
}

void BrowserEngine::reportMissing(void *controller)
{
    if (controller != m_active)
        return;
    m_matches = 0;
    m_notFound = true;
    m_searching = false;
    emit resultChanged();
}
