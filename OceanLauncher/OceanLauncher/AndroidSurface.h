#pragma once
// Which OpenWave application, if any, an Android (Waydroid) window belongs to.
//
// Android's hardware composer connects to Ocean as one Wayland client for the
// whole container and announces each Android task as an xdg_toplevel with
// app_id "waydroid.<package>" (multi-window mode; the full-UI window is
// "Waydroid"). A client PID cannot tell Android apps apart, and a toplevel's
// app_id is chosen by the client, so neither is trusted on its own:
//   * the peer must be the Android payload, proven by the SELinux label the
//     kernel reports for the connected socket (SO_PEERSEC), which a native
//     application cannot obtain;
//   * the app_id must be a syntactically valid Android package name;
//   * the result is an identifier in appd's reserved Android namespace, and
//     the caller still requires appd to list it as a launchable application.
// The full Android UI ("Waydroid") is never accepted: OpenWave shows Android
// applications, not the Android home screen, status bar or navigation bar.
#include <QByteArray>
#include <QRegularExpression>
#include <QString>

inline constexpr char OpenWaveAndroidPayloadLabel[] = "system_u:system_r:openwave_android_t:s0";
inline constexpr char OpenWaveAndroidIdPrefix[] = "org.openwave.android.";

inline QString openwaveAndroidPackage(const QString &toplevelAppId)
{
    static const QRegularExpression pattern(QStringLiteral(
        "\\Awaydroid\\.([A-Za-z][A-Za-z0-9_]*(?:\\.[A-Za-z][A-Za-z0-9_]*)+)\\z"));
    const auto match = pattern.match(toplevelAppId);
    if (!match.hasMatch())
        return {};
    const QString package = match.captured(1);
    // appd identifiers are at most 128 characters including the prefix.
    if (package.size() > 128 - int(sizeof(OpenWaveAndroidIdPrefix) - 1))
        return {};
    return package;
}

inline bool openwaveAndroidPeer(QByteArray peerLabel)
{
    while (peerLabel.endsWith('\0'))
        peerLabel.chop(1);
    return peerLabel == QByteArray(OpenWaveAndroidPayloadLabel);
}

inline QString openwaveAndroidApplicationId(QByteArray peerLabel, const QString &toplevelAppId)
{
    if (!openwaveAndroidPeer(peerLabel))
        return {};
    const QString package = openwaveAndroidPackage(toplevelAppId);
    return package.isEmpty() ? QString() : QString::fromLatin1(OpenWaveAndroidIdPrefix) + package;
}

inline bool openwaveAndroidApplicationIdPrefix(const QString &appId)
{
    return appId.startsWith(QLatin1String(OpenWaveAndroidIdPrefix));
}
