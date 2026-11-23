import QtQuick
import QtQuick.Controls
import OpenWave.Theme 1.0
import OpenWave.Fonts 1.0
import OceanSettings 1.0

Flickable {
    id: root
    property var info: ({})
    property string ssid: ""
    property bool loading: false
    property bool passwordVisible: false
    property bool actionsEnabled: false
    signal backRequested()
    signal passwordToggled()
    signal ipConfigRequested()
    signal forgetRequested()

    contentHeight: column.implicitHeight
    boundsBehavior: Flickable.StopAtBounds
    clip: true

    Column {
        id: column
        width: parent.width
        spacing: 0

        AbstractButton {
            objectName: "wifiInfoBack"
            width: parent.width
            height: 86
            Accessible.name: qsTr("Back to Wi-Fi networks")
            onClicked: root.backRequested()
            contentItem: Text {
                text: qsTr("‹  Saved network")
                color: "white"
                font.family: Fonts.family
                font.pixelSize: 34
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                color: parent.down ? "#18ffffff" : "transparent"
            }
        }
        Text {
            objectName: "wifiInfoSsid"
            width: parent.width
            text: root.ssid
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: "white"
            font.family: Fonts.family
            font.weight: Font.DemiBold
            font.pixelSize: 40
            bottomPadding: 22
        }
        Text {
            objectName: "wifiInfoLoading"
            visible: root.loading
            text: qsTr("Loading saved network details…")
            color: "#d4dadd"
            font.family: Fonts.family
            font.pixelSize: 27
            bottomPadding: 18
        }
        SettingsRow {
            objectName: "wifiInfoStatus"
            heading: qsTr("Status")
            detail: root.info.connected ? qsTr("Connected") : qsTr("Saved")
            enabled: false
        }
        SettingsRow {
            objectName: "wifiInfoSecurity"
            heading: qsTr("Security type")
            detail: root.info.security || qsTr("Unavailable")
            enabled: false
        }
        SettingsRow {
            objectName: "wifiInfoPassword"
            heading: qsTr("Password")
            detail: root.loading ? qsTr("Loading…")
                  : !root.info.password ? (root.info.passwordStatus || qsTr("Unavailable"))
                  : root.passwordVisible ? root.info.password : "••••••••"
            enabled: !!root.info.password
            Accessible.name: qsTr("Password, ") + (root.passwordVisible ? qsTr("visible") : qsTr("hidden"))
            onClicked: root.passwordToggled()
        }
        Text {
            objectName: "wifiInfoPasswordAction"
            visible: !!root.info.password
            text: root.passwordVisible ? qsTr("Tap password to hide") : qsTr("Tap password to show")
            color: Theme.accentColor
            font.family: Fonts.family
            font.pixelSize: 24
            topPadding: 8
            bottomPadding: 10
        }
        SettingsRow {
            heading: qsTr("Auto-connect")
            detail: root.info.autoConnect ? qsTr("On") : qsTr("Off")
            enabled: false
        }
        SettingsRow {
            heading: qsTr("Signal")
            detail: root.info.strength > 0 ? root.info.strength + "%" : qsTr("Out of range")
            enabled: false
        }
        SettingsRow {
            heading: qsTr("Frequency")
            detail: root.info.frequency > 0 ? root.info.frequency + " MHz" : qsTr("Unavailable")
            enabled: false
        }
        SettingsRow {
            heading: qsTr("Access point (BSSID)")
            detail: root.info.bssid || qsTr("Unavailable")
            enabled: false
        }
        SettingsRow {
            objectName: "wifiIpConfig"
            heading: qsTr("IP and DNS settings")
            detail: (root.info.ipManual ? qsTr("Manual IPv4") : qsTr("Automatic IP"))
                    + " • " + (root.info.dnsManual ? qsTr("Manual DNS") : qsTr("Automatic DNS"))
            enabled: root.actionsEnabled
            onClicked: root.ipConfigRequested()
        }
        SettingsRow {
            visible: !!root.info.ipv4Address
            heading: qsTr("IP address")
            detail: root.info.ipv4Address || ""
            enabled: false
        }
        SettingsRow {
            visible: !!root.info.gateway
            heading: qsTr("Gateway")
            detail: root.info.gateway || ""
            enabled: false
        }
        SettingsRow {
            visible: !!root.info.dns
            heading: qsTr("DNS")
            detail: root.info.dns || ""
            enabled: false
        }
        SettingsRow {
            objectName: "wifiForget"
            heading: qsTr("Forget network")
            detail: qsTr("Remove saved password and settings")
            enabled: root.actionsEnabled
            onClicked: root.forgetRequested()
        }
    }
}
