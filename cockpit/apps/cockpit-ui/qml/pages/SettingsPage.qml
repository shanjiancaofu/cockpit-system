import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Item {
    id: root

    property var palette

    RowLayout {
        anchors.fill: parent
        spacing: 14

        SurfaceCard {
            Layout.preferredWidth: 260
            Layout.fillHeight: true
            palette: root.palette

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 22
                spacing: 14

                Label {
                    text: "设置"
                    color: root.palette.textPrimary
                    font.pixelSize: 24
                    font.weight: Font.DemiBold
                }

                Label {
                    Layout.fillWidth: true
                    text: "当前为只读设备信息。可写配置将在具备校验、持久化和回滚后开放。"
                    color: root.palette.textSecondary
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: root.palette.border
                }

                Repeater {
                    model: ["车辆与连接", "声音", "语音助手", "显示", "系统信息"]

                    delegate: Rectangle {
                        required property int index
                        required property string modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: 46
                        radius: 14
                        color: index === 0 ? root.palette.surfaceHover : "transparent"

                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 14
                            text: modelData
                            color: index === 0 ? root.palette.textPrimary : root.palette.textSecondary
                            font.pixelSize: 13
                            font.weight: index === 0 ? Font.DemiBold : Font.Normal
                        }
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }

        SurfaceCard {
            Layout.fillWidth: true
            Layout.fillHeight: true
            palette: root.palette

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 14

                Label {
                    text: "车辆与连接"
                    color: root.palette.textPrimary
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                }

                Repeater {
                    model: [
                        {"name": "车辆数据", "value": vehicleState.fresh ? "实时" : "未就绪"},
                        {"name": "数据来源", "value": vehicleState.source},
                        {"name": "Gateway", "value": vehicleState.connected ? "已连接" : "重连中"},
                        {"name": "云端能力", "value": vehicleState.cloudEnabled ? "配置开启" : "离线"},
                        {"name": "导航后端", "value": "未接入"},
                        {"name": "Android / 投屏", "value": "未选型"}
                    ]

                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 14
                        color: root.palette.surfaceRaised

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 16
                            anchors.rightMargin: 16

                            Label {
                                text: modelData.name
                                color: root.palette.textPrimary
                                font.pixelSize: 13
                            }

                            Item { Layout.fillWidth: true }

                            Label {
                                text: modelData.value
                                color: root.palette.textSecondary
                                font.pixelSize: 12
                            }
                        }
                    }
                }
            }
        }
    }
}
