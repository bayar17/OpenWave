import QtQuick

Rectangle {
    id: errorPage
    objectName: "browserErrorPage"
    property string message: ""
    property string fontFamily: ""
    property color errorColor: "#ff0000"
    property color messageColor: "black"
    readonly property real uiScale: Math.max(0.01, Math.min(width / 718, height / 1114))
    color: "white"

    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: wheel => wheel.accepted = true }
    Column {
        width: parent.width - 72 * errorPage.uiScale
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: -40 * errorPage.uiScale
        spacing: 14 * errorPage.uiScale
        Rectangle {
            objectName: "browserErrorSymbol"
            anchors.horizontalCenter: parent.horizontalCenter
            width: 196 * errorPage.uiScale; height: width; radius: width / 2
            color: errorPage.errorColor
            antialiasing: true
            Rectangle {
                anchors.centerIn: parent
                width: 74 * errorPage.uiScale; height: width; radius: 6 * errorPage.uiScale
                color: "white"; antialiasing: true
            }
        }
        Flickable {
            width: parent.width
            height: Math.min(errorMessage.implicitHeight,
                Math.max(0, errorPage.height - 300 * errorPage.uiScale))
            contentWidth: width; contentHeight: errorMessage.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.VerticalFlick
            clip: true
            Text {
                id: errorMessage
                objectName: "browserErrorMessage"
                width: parent.width
                text: errorPage.message.trim().toLowerCase() === "unacceptable tls certificate"
                    ? qsTr("Unacceptable TLS Certificate") : errorPage.message
                textFormat: Text.PlainText
                color: errorPage.messageColor
                font.family: errorPage.fontFamily; font.weight: Font.ExtraLight
                font.pixelSize: Math.max(1, Math.round(40 * errorPage.uiScale))
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
        }
    }
}
