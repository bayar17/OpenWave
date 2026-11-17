#include "WebsiteIconFetcher.h"
#include <QBuffer>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace OpenWave {
WebsiteIconFetcher::WebsiteIconFetcher(QObject *parent) : QObject(parent)
{
    m_deadline.setSingleShot(true);
    m_deadline.setInterval(6000);
    connect(&m_deadline, &QTimer::timeout, this, &WebsiteIconFetcher::complete);
}
QString WebsiteIconFetcher::discoveryScript()
{
    return QStringLiteral(R"JS(JSON.stringify({url:location.href,icons:Array.from(document.querySelectorAll('link[rel]')).filter(e=>e.rel.split(/\s+/).some(r=>['icon','apple-touch-icon','apple-touch-icon-precomposed'].includes(r.toLowerCase()))).slice(0,12).map(e=>e.href),manifest:(document.querySelector('link[rel="manifest"]')||{}).href||''}))JS");
}
void WebsiteIconFetcher::cancel()
{
    ++m_generation;
    m_deadline.stop();
    const auto replies = m_replies;
    m_replies.clear();
    for (auto *reply : replies) { reply->disconnect(this); reply->abort(); reply->deleteLater(); }
    m_seen.clear(); m_best = {}; m_area = 0;
}
void WebsiteIconFetcher::fetch(const QUrl &page, const QString &metadata)
{
    cancel();
    m_deadline.start();
    const auto object = metadata.size() <= 65536 ? QJsonDocument::fromJson(metadata.toUtf8()).object() : QJsonObject();
    if (QUrl(object.value("url").toString()) == page) {
        for (const auto &value : object.value("icons").toArray()) enqueue(page.resolved(QUrl(value.toString())));
        if (!object.value("manifest").toString().isEmpty()) enqueue(page.resolved(QUrl(object.value("manifest").toString())), true);
    }
    QUrl fallback = page; fallback.setPath("/favicon.ico"); fallback.setQuery({}); fallback.setFragment({});
    enqueue(fallback);
    if (m_replies.isEmpty()) QTimer::singleShot(0, this, [this, generation = m_generation] {
        if (generation == m_generation && m_deadline.isActive()) complete();
    });
}
void WebsiteIconFetcher::enqueue(const QUrl &url, bool manifest)
{
    if (m_seen.size() >= 24 || m_seen.contains(url) || !url.isValid() || !url.userInfo().isEmpty()
        || (url.scheme() != "https" && url.scheme() != "http" && (manifest || url.scheme() != "data"))) return;
    m_seen.insert(url);
    if (url.scheme() == "data") {
        if (url.toEncoded().size() <= 2 * 1024 * 1024) {
            const auto encoded = url.toEncoded(); const auto comma = encoded.indexOf(',');
            if (comma > 0 && encoded.left(comma).contains(";base64")) consider(QByteArray::fromBase64(encoded.mid(comma + 1)));
        }
        return;
    }
    QNetworkRequest request(url);
    request.setTransferTimeout(4000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setMaximumRedirectsAllowed(4);
    auto *reply = m_network.get(request);
    reply->setReadBufferSize(2 * 1024 * 1024 + 1);
    m_replies.append(reply);
    connect(reply, &QNetworkReply::readyRead, this, [reply, manifest] {
        const qint64 limit = manifest ? 65536 : 2 * 1024 * 1024;
        if (reply->bytesAvailable() > limit || reply->header(QNetworkRequest::ContentLengthHeader).toLongLong() > limit) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, manifest] {
        if (reply->error() == QNetworkReply::NoError) {
            const auto bytes = reply->readAll();
            if (manifest && bytes.size() <= 65536) {
                const auto icons = QJsonDocument::fromJson(bytes).object().value("icons").toArray();
                for (const auto &icon : icons) {
                    const auto src = icon.toObject().value("src").toString();
                    if (!src.isEmpty()) enqueue(reply->url().resolved(QUrl(src)));
                }
            } else if (!manifest && bytes.size() <= 2 * 1024 * 1024) consider(bytes);
        }
        m_replies.removeAll(reply); reply->deleteLater();
        if (m_replies.isEmpty()) complete();
    });
}
void WebsiteIconFetcher::consider(const QByteArray &bytes)
{
    QBuffer buffer; buffer.setData(bytes); buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    for (int i = 0; i < qBound(1, reader.imageCount(), 32); ++i) {
        if (i && !reader.jumpToImage(i)) break;
        const auto size = reader.size();
        if (!size.isValid() || size.width() > 4096 || size.height() > 4096) continue;
        const qint64 area = qint64(size.width()) * size.height();
        if (area <= m_area) continue;
        if (size.width() > 512 || size.height() > 512) reader.setScaledSize(size.scaled(512, 512, Qt::KeepAspectRatio));
        const auto image = reader.read();
        if (!image.isNull()) { m_best = image; m_area = area; }
    }
}
void WebsiteIconFetcher::complete()
{
    QByteArray png;
    if (!m_best.isNull()) { QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly); m_best.save(&buffer, "PNG"); }
    cancel();
    emit finished(png);
}
}
