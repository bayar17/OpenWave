import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OpenWave.Application 1.0
import OpenWave.Controls 1.0
import OpenWave.Theme 1.0
import OpenWave.Fonts 1.0

WRApplicationWindow {
    id: window
    visible: true
    width: 768
    height: 1366
    minimumWidth: 360
    minimumHeight: 640
    title: qsTr("Water Rouge Controls")

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        ColumnLayout {
            width: window.width - 48
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 18

            Text {
                Layout.fillWidth: true
                text: qsTr("Water Rouge")
                color: WaterRouge.textColor
                font: Fonts.font(Math.round(34 * WaterRouge.textScale), Font.Light)
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Native controls from the OpenWave SDK")
                color: WaterRouge.secondaryTextColor
                font: Fonts.font(Math.round(17 * WaterRouge.textScale), Font.Normal)
            }

            GroupBox {
                Layout.fillWidth: true
                title: qsTr("Theme and scaling")
                ColumnLayout {
                    anchors.fill: parent
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: qsTr("Accent palette"); color: WaterRouge.textColor }
                        WRComboBox {
                            model: WaterRouge.availablePalettes
                            currentIndex: Math.max(0, model.indexOf(WaterRouge.palette))
                            onActivated: index => WaterRouge.selectPalette(model[index])
                            Accessible.name: qsTr("Accent palette")
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: qsTr("Text scale"); color: WaterRouge.textColor }
                        WRSlider {
                            Layout.fillWidth: true
                            from: 0.8; to: 1.6; stepSize: 0.1
                            value: WaterRouge.textScale
                            onMoved: WaterRouge.textScale = value
                            Accessible.name: qsTr("Text scale")
                        }
                        Label { text: Math.round(WaterRouge.textScale * 100) + "%"; color: WaterRouge.textColor }
                    }
                    Label {
                        text: qsTr("Reduced motion: %1").arg(WaterRouge.reducedMotion ? qsTr("on") : qsTr("off"))
                        color: WaterRouge.secondaryTextColor
                    }
                }
            }

            GroupBox {
                Layout.fillWidth: true
                title: qsTr("Buttons")
                Flow {
                    anchors.fill: parent
                    spacing: 12
                    WRButton { text: qsTr("Button"); onClicked: buttonStatus.text = qsTr("Button activated") }
                    WRButton { text: qsTr("Disabled"); enabled: false }
                    WRButton { text: qsTr("Custom text"); textColor: WaterRouge.accentColor }
                    Label { id: buttonStatus; text: qsTr("Ready"); color: WaterRouge.secondaryTextColor; anchors.verticalCenter: parent.verticalCenter }
                }
            }

            GroupBox {
                Layout.fillWidth: true
                title: qsTr("Switches and checkboxes")
                ColumnLayout {
                    anchors.fill: parent
                    WRSwitch { text: qsTr("Wi-Fi"); checked: true; Layout.fillWidth: true }
                    WRSwitch {
                        text: qsTr("Dual-tone switch")
                        offTrackColor: "#d7d7d7"
                        onTrackColor: WaterRouge.secondaryAccentColor
                        Layout.fillWidth: true
                    }
                    WRSwitch { text: qsTr("Disabled"); enabled: false; Layout.fillWidth: true }
                    WRCheckBox { text: qsTr("Not selected"); Layout.fillWidth: true }
                    WRCheckBox { text: qsTr("Selected"); checked: true; Layout.fillWidth: true }
                    WRCheckBox { text: qsTr("Indeterminate"); tristate: true; checkState: Qt.PartiallyChecked; Layout.fillWidth: true }
                    WRCheckBox { text: qsTr("Disabled"); checked: true; enabled: false; Layout.fillWidth: true }
                }
            }

            GroupBox {
                Layout.fillWidth: true
                title: qsTr("Radio buttons")
                ColumnLayout {
                    anchors.fill: parent
                    ButtonGroup { id: choices }
                    WRRadioButton { text: qsTr("Option one"); ButtonGroup.group: choices; checked: true; Layout.fillWidth: true }
                    WRRadioButton { text: qsTr("Option two"); ButtonGroup.group: choices; Layout.fillWidth: true }
                    WRRadioButton { text: qsTr("Disabled option"); ButtonGroup.group: choices; enabled: false; Layout.fillWidth: true }
                }
            }

            GroupBox {
                Layout.fillWidth: true
                title: qsTr("Text fields")
                ColumnLayout {
                    anchors.fill: parent
                    WRTextField { accessibleLabel: qsTr("Normal text field"); placeholderText: qsTr("Enter text"); Layout.fillWidth: true }
                    WRTextField { accessibleLabel: qsTr("Password"); placeholderText: qsTr("Password"); echoMode: TextInput.Password; text: "oceanwave"; Layout.fillWidth: true }
                    WRTextField { accessibleLabel: qsTr("Error field"); text: qsTr("Invalid value"); error: true; errorText: qsTr("Enter a valid value"); Layout.fillWidth: true }
                    WRTextField { accessibleLabel: qsTr("Disabled field"); placeholderText: qsTr("Unavailable"); enabled: false; Layout.fillWidth: true }
                }
            }
            GroupBox {
                Layout.fillWidth: true
                title: qsTr("Lists and selection")
                ColumnLayout {
                    anchors.fill: parent
                    WRListItem { heading: qsTr("List item"); detail: qsTr("Subtitle text"); Layout.fillWidth: true }
                    WRListItem { heading: qsTr("Destructive item"); detail: qsTr("Removes the selected entry"); destructive: true; showChevron: false; Layout.fillWidth: true }
                }
            }
            Item { Layout.preferredHeight: 20 }
        }
    }
}
