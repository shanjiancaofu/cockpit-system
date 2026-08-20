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
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 520
            palette: root.palette

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 28
                spacing: 14

                RowLayout {
                    Layout.fillWidth: true

                    Label {
                        text: "语音助手"
                        color: root.palette.textSecondary
                        font.pixelSize: 13
                    }

                    Item { Layout.fillWidth: true }

                    Rectangle {
                        Layout.preferredWidth: 8
                        Layout.preferredHeight: 8
                        radius: 4
                        color: voiceStatus.connected ? root.palette.green : root.palette.red
                    }

                    Label {
                        text: voiceStatus.connected ? "服务在线" : "服务离线"
                        color: root.palette.textSecondary
                        font.pixelSize: 11
                    }
                }

                Item { Layout.fillHeight: true }

                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 144
                    Layout.preferredHeight: 144
                    radius: 72
                    color: voiceStatus.active ? "#3052d6c8" : "#2052d6c8"
                    border.width: 2
                    border.color: voiceStatus.connected ? root.palette.accentStrong
                                                        : root.palette.textMuted

                    Rectangle {
                        anchors.centerIn: parent
                        width: 92
                        height: 92
                        radius: 46
                        color: voiceStatus.active ? root.palette.accent : root.palette.surfaceRaised

                        Label {
                            anchors.centerIn: parent
                            text: "AI"
                            color: voiceStatus.active ? root.palette.background : root.palette.textSecondary
                            font.pixelSize: 28
                            font.weight: Font.Bold
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: voiceStatus.stateLabel
                    color: root.palette.textPrimary
                    font.pixelSize: 30
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                }

                Label {
                    Layout.fillWidth: true
                    text: !voiceStatus.connected && voiceStatus.lastError.length > 0
                          ? voiceStatus.lastError
                          : (voiceStatus.stateReason.length > 0 ? voiceStatus.stateReason
                                                                : "你好，小山")
                    color: root.palette.textSecondary
                    font.pixelSize: 12
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: voiceStatus.transcriptText.length > 0 || voiceStatus.responseText.length > 0
                    spacing: 10

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 66
                        radius: 14
                        color: root.palette.surfaceRaised

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 3

                            Label {
                                text: "你说"
                                color: root.palette.textMuted
                                font.pixelSize: 9
                            }

                            Label {
                                Layout.fillWidth: true
                                text: voiceStatus.transcriptText.length > 0
                                      ? voiceStatus.transcriptText : "—"
                                color: root.palette.textPrimary
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 66
                        radius: 14
                        color: root.palette.surfaceRaised

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 3

                            Label {
                                text: "小山"
                                color: root.palette.textMuted
                                font.pixelSize: 9
                            }

                            Label {
                                Layout.fillWidth: true
                                text: voiceStatus.responseText.length > 0
                                      ? voiceStatus.responseText : "—"
                                color: root.palette.textPrimary
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 10

                    Rectangle {
                        Layout.preferredWidth: 132
                        Layout.preferredHeight: 34
                        radius: 17
                        color: root.palette.surfaceRaised

                        Row {
                            anchors.centerIn: parent
                            spacing: 8

                            Rectangle {
                                width: 8
                                height: 8
                                radius: 4
                                color: voiceStatus.active ? root.palette.accent
                                                          : (voiceStatus.connected ? root.palette.green
                                                                                   : root.palette.red)
                            }

                            Label {
                                text: voiceStatus.stateLabel
                                color: root.palette.textPrimary
                                font.pixelSize: 11
                            }
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: 100
                        Layout.preferredHeight: 34
                        radius: 17
                        visible: voiceStatus.canInterrupt || voiceStatus.interruptPending
                        color: interruptMouse.containsMouse ? "#45ff6f7d" : "#30ff6f7d"
                        border.width: 1
                        border.color: root.palette.red

                        Label {
                            anchors.centerIn: parent
                            text: voiceStatus.interruptPending ? "取消中" : "停止"
                            color: root.palette.textPrimary
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                        }

                        MouseArea {
                            id: interruptMouse
                            anchors.fill: parent
                            enabled: !voiceStatus.interruptPending
                            hoverEnabled: true
                            onClicked: voiceStatus.interrupt()
                        }
                    }
                }
            }
        }

        ColumnLayout {
            Layout.preferredWidth: 350
            Layout.minimumWidth: 350
            Layout.maximumWidth: 350
            Layout.fillHeight: true
            spacing: 14

            SurfaceCard {
                Layout.fillWidth: true
                Layout.fillHeight: true
                palette: root.palette

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 12

                    Label {
                        text: "可用能力"
                        color: root.palette.textPrimary
                        font.pixelSize: 17
                        font.weight: Font.DemiBold
                    }

                    Repeater {
                        model: [
                            {"title": "打开相机", "detail": "确定性动作 · 已接入"},
                            {"title": "查询车辆状态", "detail": "Gateway 状态 · 已接入"},
                            {"title": "开放问答", "detail": "本地 LLM · 默认关闭"},
                            {"title": "播放音乐", "detail": "媒体后端 · 未接入"}
                        ]

                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: 14
                            color: root.palette.surfaceRaised

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 13
                                spacing: 3

                                Label {
                                    text: modelData.title
                                    color: root.palette.textPrimary
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.detail
                                    color: root.palette.textSecondary
                                    font.pixelSize: 10
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                }
            }

            SurfaceCard {
                Layout.fillWidth: true
                Layout.preferredHeight: 100
                palette: root.palette
                strokeColor: root.palette.amber

                Label {
                    anchors.fill: parent
                    anchors.margins: 16
                    text: "车辆动作始终经过 TranscriptNormalizer、确定性路由和白名单 Dispatcher；LLM 不能直接控制车辆。"
                    color: root.palette.textSecondary
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }
}
