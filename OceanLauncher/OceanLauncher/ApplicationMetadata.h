#pragma once
#include <QVariantMap>

// appd owns eligibility. Unknown or malformed records fail closed, including
// components that have no graphical application entry point.
inline bool oceanLaunchableApplication(const QVariantMap &info)
{
    const auto launchable = info.value(QStringLiteral("launchable"));
    const QString id = info.value(QStringLiteral("appId")).toString();
    return launchable.metaType().id() == QMetaType::Bool && launchable.toBool()
        && !id.isEmpty() && id != QStringLiteral("com.openwave.shell");
}
