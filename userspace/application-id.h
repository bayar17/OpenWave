#pragma once
#include <QString>

inline QString openwaveCanonicalApplicationId(const QString &id)
{
    if (id == "browser") return QStringLiteral("com.openwave.ocean");
    if (id == "files") return QStringLiteral("com.openwave.files");
    if (id == "settings") return QStringLiteral("com.openwave.settings");
    return id;
}
