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
    palette.window: "#090d12"
    palette.windowText: "#f4f7fa"
    palette.base: "#172330"
    palette.text: "#f4f7fa"
    palette.button: "#1d2c3a"
    palette.buttonText: "#f4f7fa"
    palette.highlight: "#52d6c8"
    palette.highlightedText: "#090d12"
    palette.disabled.buttonText: "#607384"

    HomePage {
        anchors.fill: parent
    }
}
