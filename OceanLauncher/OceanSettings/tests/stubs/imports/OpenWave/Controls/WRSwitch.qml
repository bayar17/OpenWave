import QtQuick
import QtQuick.Controls

Switch {
    function simulateUserToggle() {
        toggle()
        toggled()
    }
}
