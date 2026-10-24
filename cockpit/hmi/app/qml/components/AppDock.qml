import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    objectName: "appDock"

    property var palette
    property int currentIndex: 0
    signal pageRequested(int index)

    radius: 24
    color: "#e6111a24"
    border.width: 1
    border.color: root.palette.border

    ListModel {
        id: dockModel
        ListElement { page: 0; mark: "H"; title: "主页" }
        ListElement { page: 1; mark: "C"; title: "相机" }
        ListElement { page: 3; mark: "M"; title: "媒体" }
        ListElement { page: 4; mark: "AI"; title: "小山" }
        ListElement { page: 5; mark: "S"; title: "设置" }
        ListElement { page: 2; mark: "D"; title: "诊断" }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 7
        spacing: 8

        Repeater {
            model: dockModel

            delegate: Rectangle {
                required property int page
                required property string mark
                required property string title

                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 17
                color: root.currentIndex === page ? root.palette.surfaceHover
                                                   : (mouseArea.containsMouse
                                                      ? root.palette.surfaceRaised : "transparent")
                border.width: root.currentIndex === page ? 1 : 0
                border.color: root.palette.accentStrong

                Row {
                    anchors.centerIn: parent
                    spacing: 9

                    Rectangle {
                        width: 28
                        height: 28
                        radius: 9
                        color: root.currentIndex === page ? root.palette.accent : root.palette.border

                        Label {
                            anchors.centerIn: parent
                            text: mark
                            color: root.currentIndex === page ? root.palette.background
                                                              : root.palette.textSecondary
                            font.pixelSize: mark.length > 1 ? 10 : 13
                            font.weight: Font.Bold
                        }
                    }

                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: title
                        color: root.currentIndex === page ? root.palette.textPrimary
                                                          : root.palette.textSecondary
                        font.pixelSize: 13
                        font.weight: root.currentIndex === page ? Font.DemiBold : Font.Normal
                    }
                }

                MouseArea {
                    id: mouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.pageRequested(page)
                }
            }
        }
    }
}
