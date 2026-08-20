import QtQuick

Rectangle {
    id: root

    property var palette
    property color cardColor: palette ? palette.surface : "#111a24"
    property color strokeColor: palette ? palette.border : "#263747"
    property int cardRadius: 20
    default property alias content: contentItem.data

    radius: cardRadius
    color: cardColor
    border.width: 1
    border.color: strokeColor
    clip: true

    Item {
        id: contentItem
        anchors.fill: parent
    }
}
