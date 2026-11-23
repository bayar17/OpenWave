import QtQuick
import QtQuick.Controls

AbstractButton {
    property string heading: ""
    property string detail: ""
    property bool largeText: false
    width: parent ? parent.width : 400
    height: 66
    contentItem: Item {
        Text { objectName: "rowHeading"; text: heading }
        Text { objectName: "rowDetail"; text: detail }
    }
}
