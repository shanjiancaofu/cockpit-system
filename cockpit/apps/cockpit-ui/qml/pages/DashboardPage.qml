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

        ColumnLayout {
            Layout.preferredWidth: 214
            Layout.fillHeight: true
            spacing: 14

            SurfaceCard {
                Layout.fillWidth: true
                Layout.fillHeight: true
                palette: root.palette

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 6

                    Label {
                        text: "当前车速"
                        color: root.palette.textSecondary
                        font.pixelSize: 13
                    }

                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        Column {
                            anchors.centerIn: parent
                            spacing: -8

                            Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: Math.round(vehicleState.speedKph).toString()
                                color: root.palette.textPrimary
                                font.pixelSize: 86
                                font.weight: Font.Light
                            }

                            Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: "km/h"
                                color: root.palette.accent
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 5
                        radius: 3
                        color: root.palette.surfaceRaised

                        Rectangle {
                            width: parent.width * Math.max(0, Math.min(vehicleState.speedKph / 160, 1))
                            height: parent.height
                            radius: 3
                            color: root.palette.accent
                        }
                    }
                }
            }

            SurfaceCard {
                Layout.fillWidth: true
                Layout.preferredHeight: 128
                palette: root.palette

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            text: "电量"
                            color: root.palette.textSecondary
                            font.pixelSize: 12
                        }

                        Item { Layout.fillWidth: true }

                        Label {
                            text: vehicleState.socPercent + "%"
                            color: vehicleState.socPercent > 20 ? root.palette.green : root.palette.amber
                            font.pixelSize: 20
                            font.weight: Font.DemiBold
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 7
                        radius: 4
                        color: root.palette.surfaceRaised

                        Rectangle {
                            width: parent.width * Math.max(0, Math.min(vehicleState.socPercent / 100, 1))
                            height: parent.height
                            radius: 4
                            color: vehicleState.socPercent > 20 ? root.palette.green : root.palette.amber
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            text: "挡位  " + vehicleState.gear
                            color: root.palette.textPrimary
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                        }

                        Item { Layout.fillWidth: true }

                        Label {
                            text: vehicleState.fresh ? "READY" : "WAIT"
                            color: vehicleState.fresh ? root.palette.green : root.palette.amber
                            font.pixelSize: 12
                            font.weight: Font.Bold
                        }
                    }
                }
            }
        }

        SurfaceCard {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 380
            palette: root.palette
            cardColor: root.palette.backgroundRaised

            Canvas {
                id: roadCanvas
                anchors.fill: parent
                opacity: 0.7

                onPaint: {
                    var ctx = getContext("2d")
                    ctx.reset()
                    ctx.fillStyle = root.palette.backgroundRaised
                    ctx.fillRect(0, 0, width, height)
                    ctx.strokeStyle = root.palette.border
                    ctx.lineWidth = 1
                    for (var row = 0; row < 7; ++row) {
                        ctx.beginPath()
                        ctx.moveTo(0, height * (0.12 + row * 0.14))
                        ctx.bezierCurveTo(width * 0.25, height * (0.06 + row * 0.13),
                                          width * 0.68, height * (0.20 + row * 0.11), width,
                                          height * (0.10 + row * 0.14))
                        ctx.stroke()
                    }
                    for (var column = 0; column < 6; ++column) {
                        ctx.beginPath()
                        ctx.moveTo(width * (0.06 + column * 0.18), 0)
                        ctx.bezierCurveTo(width * (0.16 + column * 0.14), height * 0.3,
                                          width * (0.02 + column * 0.2), height * 0.7,
                                          width * (0.14 + column * 0.16), height)
                        ctx.stroke()
                    }
                    ctx.strokeStyle = root.palette.accentStrong
                    ctx.lineWidth = 7
                    ctx.lineCap = "round"
                    ctx.beginPath()
                    ctx.moveTo(width * 0.52, height * 0.86)
                    ctx.bezierCurveTo(width * 0.48, height * 0.68, width * 0.64, height * 0.52,
                                      width * 0.57, height * 0.26)
                    ctx.stroke()
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.margins: 20
                width: Math.min(292, parent.width - 40)
                height: 104
                radius: 16
                color: "#e6172330"
                border.width: 1
                border.color: root.palette.border

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 14

                    Rectangle {
                        Layout.preferredWidth: 52
                        Layout.preferredHeight: 52
                        radius: 16
                        color: root.palette.accent

                        Label {
                            anchors.centerIn: parent
                            text: "N"
                            color: root.palette.background
                            font.pixelSize: 24
                            font.weight: Font.Bold
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Label {
                            text: "导航尚未连接"
                            color: root.palette.textPrimary
                            font.pixelSize: 17
                            font.weight: Font.DemiBold
                        }

                        Label {
                            Layout.fillWidth: true
                            text: "等待 ROS2 / 地图后端"
                            color: root.palette.textSecondary
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                    }
                }
            }

            Rectangle {
                width: 42
                height: 42
                radius: 21
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: 82
                color: root.palette.accent
                border.width: 6
                border.color: "#4052d6c8"

                Rectangle {
                    anchors.centerIn: parent
                    width: 10
                    height: 10
                    radius: 5
                    color: root.palette.background
                }
            }

            Label {
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.margins: 20
                text: "地图为界面占位，不代表导航已可用"
                color: root.palette.textMuted
                font.pixelSize: 11
            }
        }

        ColumnLayout {
            Layout.preferredWidth: 272
            Layout.fillHeight: true
            spacing: 14

            SurfaceCard {
                Layout.fillWidth: true
                Layout.fillHeight: true
                palette: root.palette

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            text: "哨兵模式"
                            color: root.palette.textPrimary
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                        }

                        Item { Layout.fillWidth: true }

                        Label {
                            text: sentinelStatus.stateLabel
                            color: sentinelStatus.state === "FAULTED" ? root.palette.red
                                   : (sentinelStatus.armed ? root.palette.green
                                                          : root.palette.textMuted)
                            font.pixelSize: 11
                        }
                    }

                    Rectangle {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: 80
                        Layout.preferredHeight: 80
                        radius: 24
                        color: root.palette.surfaceRaised

                        Label {
                            anchors.centerIn: parent
                            text: "S"
                            color: sentinelStatus.armed ? root.palette.green : root.palette.textMuted
                            font.pixelSize: 30
                            font.weight: Font.Bold
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: !sentinelStatus.connected ? "等待 Sentinel 服务"
                              : (sentinelStatus.lastSnapshotPath.length > 0
                                 ? "最近取证 " + sentinelStatus.lastSnapshotPath
                                 : "STM32 运动事件将触发相机取证")
                        color: root.palette.textSecondary
                        font.pixelSize: 12
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                    }

                    Item { Layout.fillHeight: true }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 42
                        radius: 13
                        color: sentinelMouse.containsMouse ? root.palette.surfaceHover
                                                           : root.palette.surfaceRaised
                        opacity: sentinelStatus.connected && !sentinelStatus.busy ? 1.0 : 0.5

                        Label {
                            anchors.centerIn: parent
                            text: sentinelStatus.busy ? "处理中"
                                  : (sentinelStatus.armed ? "解除布防" : "启用布防")
                            color: root.palette.textPrimary
                            font.pixelSize: 12
                        }

                        MouseArea {
                            id: sentinelMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            enabled: sentinelStatus.connected && !sentinelStatus.busy
                            onClicked: sentinelStatus.setArmed(!sentinelStatus.armed)
                        }
                    }
                }
            }

            SurfaceCard {
                Layout.fillWidth: true
                Layout.preferredHeight: 142
                palette: root.palette

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true

                        Rectangle {
                            Layout.preferredWidth: 34
                            Layout.preferredHeight: 34
                            radius: 12
                            color: root.palette.accent

                            Label {
                                anchors.centerIn: parent
                                text: "AI"
                                color: root.palette.background
                                font.pixelSize: 10
                                font.weight: Font.Bold
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1

                            Label {
                                text: "你好，小山"
                                color: root.palette.textPrimary
                                font.pixelSize: 15
                                font.weight: Font.DemiBold
                            }

                            Label {
                                text: voiceStatus.stateLabel
                                color: voiceStatus.active ? root.palette.accent
                                                          : root.palette.textSecondary
                                font.pixelSize: 11
                            }
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: voiceStatus.connected
                              ? (voiceStatus.stateReason.length > 0 ? voiceStatus.stateReason
                                                                    : "说出唤醒词后使用确定性车辆命令")
                              : "语音服务未连接，后台将自动重试"
                        color: root.palette.textMuted
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }
}
