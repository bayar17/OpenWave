#pragma once
#include <QBuffer>
#include <QImageReader>

namespace OpenWaveWebApp {
inline bool validIcon(const QByteArray &png)
{
    if (png.isEmpty() || png.size() > 1024 * 1024 || !png.startsWith("\x89PNG\r\n\x1a\n")) return false;
    QBuffer buffer; buffer.setData(png); buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, "png");
    const auto size = reader.size();
    return size.isValid() && size.width() <= 512 && size.height() <= 512 && !reader.read().isNull();
}
}
