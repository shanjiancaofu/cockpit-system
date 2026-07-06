import QtQuick
import QtQuick.Controls
import "pages"

ApplicationWindow {
    width: 1280
    height: 720
    minimumWidth: 960
    minimumHeight: 540
    visible: true
    color: "#0e1214"
    title: "Smart Cockpit System"

    HomePage {
        anchors.fill: parent
    }
}
