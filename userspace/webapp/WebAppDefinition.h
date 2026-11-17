#pragma once

#include <QCryptographicHash>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>

namespace OpenWaveWebApp {
inline QUrl address(const QString &text)
{
    const QUrl url(text, QUrl::StrictMode);
    if (!url.isValid() || (url.scheme() != "https" && url.scheme() != "http")
        || url.host().isEmpty() || !url.userInfo().isEmpty()
        || url.toEncoded().size() > 2048) return {};
    return url;
}
inline bool validName(const QString &name)
{
    if (name.isEmpty() || name != name.trimmed() || name.size() > 256) return false;
    for (const QChar c : name)
        if (c.isNull() || c.category() == QChar::Other_Control || c.category() == QChar::Other_Format
            || c.category() == QChar::Separator_Line || c.category() == QChar::Separator_Paragraph) return false;
    return true;
}
inline QString suggestedName(QString title, const QUrl &url)
{
    QString result;
    for (const QChar c : title.simplified())
        if (c.category() != QChar::Other_Control && c.category() != QChar::Other_Format) result += c;
    result = result.trimmed().left(256).trimmed();
    return result.isEmpty() ? url.host().left(256) : result;
}
inline QString identifier(const QUrl &url)
{
    return "org.openwave.webapp.h" + QString::fromLatin1(
        QCryptographicHash::hash(url.toEncoded(), QCryptographicHash::Sha256).toHex());
}
inline QJsonObject manifest(const QString &name, const QUrl &url)
{
    return {{"schemaVersion", 1}, {"id", identifier(url)}, {"name", name}, {"version", "1.0"},
        {"runtime", "openwave-qt"}, {"executable", "bin/openwave-webapp"},
        {"sandbox", QJsonObject{{"enabled", true}, {"profile", "standard"}}},
        {"permissions", QJsonArray{}}, {"singleInstance", true}, {"acceptsPaths", false},
        {"icon", "resources/icon.svg"}, {"webApp", QJsonObject{{"url", url.toString(QUrl::FullyEncoded)}}}};
}
}
