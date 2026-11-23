#include "WebInput.h"
#include <QMouseEvent>
#include <QQuickItem>
#include <QTouchEvent>
#include <QHash>
#include <optional>
#undef signals
#include <wpe/webkit.h>

namespace OpenWave {
namespace {
WPEModifiers modifiers(Qt::KeyboardModifiers keys)
{
    unsigned value = 0;
    if (keys & Qt::ShiftModifier) value |= WPE_MODIFIER_KEYBOARD_SHIFT;
    if (keys & Qt::ControlModifier) value |= WPE_MODIFIER_KEYBOARD_CONTROL;
    if (keys & Qt::AltModifier) value |= WPE_MODIFIER_KEYBOARD_ALT;
    if (keys & Qt::MetaModifier) value |= WPE_MODIFIER_KEYBOARD_META;
    return static_cast<WPEModifiers>(value);
}

class ViewInput final : public QObject {
public:
    ViewInput(QQuickItem *item, WPEView *view)
        : QObject(item), m_item(item), m_view(WPE_VIEW(g_object_ref(view)))
    {
        item->installEventFilter(this);
        connect(item, &QQuickItem::visibleChanged, this, [this] {
            if (!m_item->isVisible()) cancel();
        });
        connect(item, &QQuickItem::enabledChanged, this, [this] {
            if (!m_item->isEnabled()) cancel();
        });
    }
    ~ViewInput() override { cancel(); g_object_unref(m_view); }

protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        switch (event->type()) {
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd:
        case QEvent::TouchCancel: {
            auto *touch = static_cast<QTouchEvent *>(event);
            if (m_mouseDown) cancel();
            if (event->type() == QEvent::TouchCancel) {
                cancel();
            } else {
                m_item->forceActiveFocus();
                const auto source = touch->pointingDevice()->type() == QInputDevice::DeviceType::TouchPad
                    ? WPE_INPUT_SOURCE_TOUCHPAD : WPE_INPUT_SOURCE_TOUCHSCREEN;
                for (const auto &point : touch->points()) {
                    WPEEventType type;
                    switch (point.state()) {
                    case QEventPoint::State::Pressed: type = WPE_EVENT_TOUCH_DOWN; break;
                    case QEventPoint::State::Updated: type = WPE_EVENT_TOUCH_MOVE; break;
                    case QEventPoint::State::Released: type = WPE_EVENT_TOUCH_UP; break;
                    default: continue;
                    }
                    dispatch(type, point.id(), point.position(), touch->timestamp(),
                             modifiers(touch->modifiers()), source);
                }
            }
            event->accept();
            return true;
        }
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick: {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() != Qt::LeftButton)
                return dispatchMouse(mouse);
            if (!m_points.isEmpty()) {
                event->accept();
                return true;
            }
            m_mouseDown = true;
            m_item->forceActiveFocus();
            dispatch(WPE_EVENT_TOUCH_DOWN, mouseSequence, mouse->position(), mouse->timestamp(),
                     modifiers(mouse->modifiers()));
            event->accept();
            return true;
        }
        case QEvent::MouseMove:
        case QEvent::MouseButtonRelease: {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (!m_mouseDown) {
                if (!m_points.isEmpty() && (mouse->buttons() & Qt::LeftButton
                    || mouse->button() == Qt::LeftButton)) {
                    event->accept();
                    return true;
                }
                return dispatchMouse(mouse);
            }
            if (event->type() == QEvent::MouseButtonRelease && mouse->button() != Qt::LeftButton)
                return false;
            const bool released = event->type() == QEvent::MouseButtonRelease;
            dispatch(released ? WPE_EVENT_TOUCH_UP : WPE_EVENT_TOUCH_MOVE,
                     mouseSequence, mouse->position(), mouse->timestamp(), modifiers(mouse->modifiers()));
            if (released) m_mouseDown = false;
            event->accept();
            return true;
        }
        case QEvent::UngrabMouse:
        case QEvent::FocusOut:
        case QEvent::WindowDeactivate:
            cancel();
            break;
        default: break;
        }
        return false;
    }

private:
    bool dispatchMouse(QMouseEvent *mouse)
    {
        unsigned keys = modifiers(mouse->modifiers());
        if (mouse->buttons() & Qt::LeftButton) keys |= WPE_MODIFIER_POINTER_BUTTON1;
        if (mouse->buttons() & Qt::MiddleButton) keys |= WPE_MODIFIER_POINTER_BUTTON2;
        if (mouse->buttons() & Qt::RightButton) keys |= WPE_MODIFIER_POINTER_BUTTON3;
        const auto position = mouse->position();
        const auto delta = m_mousePosition ? position - *m_mousePosition : QPointF();
        m_mousePosition = position;
        WPEEvent *event;
        if (mouse->type() == QEvent::MouseMove) {
            event = wpe_event_pointer_move_new(WPE_EVENT_POINTER_MOVE, m_view, WPE_INPUT_SOURCE_MOUSE,
                mouse->timestamp(), static_cast<WPEModifiers>(keys), position.x(), position.y(), delta.x(), delta.y());
        } else {
            const unsigned button = mouse->button() == Qt::LeftButton ? WPE_BUTTON_PRIMARY
                : mouse->button() == Qt::RightButton ? WPE_BUTTON_SECONDARY
                : mouse->button() == Qt::MiddleButton ? WPE_BUTTON_MIDDLE : 0;
            if (!button) return false;
            const bool released = mouse->type() == QEvent::MouseButtonRelease;
            if (!released) m_item->forceActiveFocus();
            const unsigned count = released ? 0
                : wpe_view_compute_press_count(m_view, position.x(), position.y(), button, mouse->timestamp());
            event = wpe_event_pointer_button_new(released ? WPE_EVENT_POINTER_UP : WPE_EVENT_POINTER_DOWN,
                m_view, WPE_INPUT_SOURCE_MOUSE, mouse->timestamp(), static_cast<WPEModifiers>(keys),
                button, position.x(), position.y(), count);
        }
        wpe_view_event(m_view, event);
        wpe_event_unref(event);
        mouse->accept();
        return true;
    }

    void dispatch(WPEEventType type, int id, QPointF position, ulong timestamp,
                  WPEModifiers keys = static_cast<WPEModifiers>(0),
                  WPEInputSource source = WPE_INPUT_SOURCE_TOUCHSCREEN)
    {
        if (type == WPE_EVENT_TOUCH_DOWN || type == WPE_EVENT_TOUCH_MOVE)
            m_points.insert(id, position);
        else m_points.remove(id);
        auto *event = wpe_event_touch_new(type, m_view, source, timestamp, keys,
                                        id, position.x(), position.y());
        wpe_view_event(m_view, event);
        wpe_event_unref(event);
    }
    void cancel()
    {
        const auto points = m_points;
        for (auto it = points.cbegin(); it != points.cend(); ++it)
            dispatch(WPE_EVENT_TOUCH_CANCEL, it.key(), it.value(), 0);
        m_mouseDown = false;
    }
    static constexpr int mouseSequence = 0x7fffffff;
    QQuickItem *m_item;
    WPEView *m_view;
    bool m_mouseDown = false;
    QHash<int, QPointF> m_points;
    std::optional<QPointF> m_mousePosition;
};
}

WebInput::WebInput(QObject *parent) : QObject(parent) {}

bool WebInput::reloadView(QObject *object, const QUrl &url)
{
    WebKitWebView *view = nullptr;
    if (!object || url.isEmpty() || !url.isValid()
            || !QMetaObject::invokeMethod(object, "webView", Qt::DirectConnection,
                QGenericReturnArgument("WebKitWebView*", &view)) || !view) return false;
    webkit_web_view_load_uri(view, url.toEncoded().constData());
    return true;
}

void WebInput::attachView(QObject *object)
{
    auto *item = qobject_cast<QQuickItem *>(object);
    if (!item || item->property("openwaveWebInputAttached").toBool()) return;
    WebKitWebView *webView = nullptr;
    if (!QMetaObject::invokeMethod(object, "webView", Qt::DirectConnection,
            QGenericReturnArgument("WebKitWebView*", &webView)) || !webView) return;
    new ViewInput(item, webkit_web_view_get_wpe_view(webView));
    item->setProperty("openwaveWebInputAttached", true);
}

}
