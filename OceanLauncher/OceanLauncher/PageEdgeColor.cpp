#include "PageEdgeColor.h"
#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include <algorithm>
#include <cmath>

PageEdgeColor::PageEdgeColor(QObject *parent) : QObject(parent)
{
    m_timer.setInterval(250);
    connect(&m_timer, &QTimer::timeout, this, &PageEdgeColor::sample);
}
void PageEdgeColor::setSource(QQuickItem *source)
{
    if (m_source == source) return;
    m_source = source;
    ++m_generation;
    m_color = Qt::white;
    emit changed();
    emit colorChanged();
    sample();
}
void PageEdgeColor::setEnabled(bool enabled)
{
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    ++m_generation;
    if (enabled) { m_timer.start(); sample(); }
    else m_timer.stop();
    emit changed();
}
bool PageEdgeColor::darkForeground() const
{
    const auto linear = [](double channel) {
        return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(m_color.redF()) + 0.7152 * linear(m_color.greenF())
        + 0.0722 * linear(m_color.blueF()) > 0.179;
}
void PageEdgeColor::sample()
{
    if (!m_enabled || m_pending || !m_source || !m_source->isVisible() || !m_source->window()
        || !m_source->window()->isVisible() || m_source->width() <= 0 || m_source->height() <= 0) return;
    auto grab = m_source->grabToImage(QSize(64, qMax(1, qRound(64 * m_source->height() / m_source->width()))));
    if (!grab) return;
    m_pending = true;
    const auto generation = m_generation;
    connect(grab.data(), &QQuickItemGrabResult::ready, this, [this, grab, generation] {
        m_pending = false;
        if (!m_enabled || generation != m_generation) return;
        const auto image = grab->image();
        if (image.isNull()) return;
        QList<int> red, green, blue;
        for (int x = 0; x < image.width(); ++x) {
            const QColor pixel = image.pixelColor(x, 0);
            if (pixel.alpha() < 128) continue;
            red.append(pixel.red()); green.append(pixel.green()); blue.append(pixel.blue());
        }
        if (red.isEmpty()) return;
        for (auto *channel : {&red, &green, &blue}) std::sort(channel->begin(), channel->end());
        const auto middle = red.size() / 2;
        const QColor color(red[middle], green[middle], blue[middle]);
        if (color != m_color) { m_color = color; emit colorChanged(); }
    }, Qt::SingleShotConnection);
}
