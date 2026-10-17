#pragma once
#include <QImage>
#include <QNetworkAccessManager>
#include <QSet>
#include <QTimer>
#include <QUrl>

namespace OpenWave {
class WebsiteIconFetcher : public QObject
{
    Q_OBJECT
public:
    explicit WebsiteIconFetcher(QObject *parent = nullptr);
    ~WebsiteIconFetcher() override { cancel(); }
    static QString discoveryScript();
    void fetch(const QUrl &page, const QString &metadata);
    void cancel();
signals:
    void finished(const QByteArray &png);
private:
    void enqueue(const QUrl &url, bool manifest = false);
    void consider(const QByteArray &bytes);
    void complete();
    QNetworkAccessManager m_network;
    QTimer m_deadline;
    QSet<QUrl> m_seen;
    QList<QNetworkReply *> m_replies;
    QImage m_best;
    qint64 m_area = 0;
    quint64 m_generation = 0;
};
}
