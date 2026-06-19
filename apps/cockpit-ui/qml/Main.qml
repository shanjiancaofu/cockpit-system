import QtQuick 2.15
import QtQuick.Controls 2.15

ApplicationWindow {
    width: 1280
    height: 720
    visible: true
    title: "Smart Cockpit"

    StackView {
        anchors.fill: parent
        initialItem: "pages/HomePage.qml"
    }
}
