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

                Label {
                    text: "语音助手"
                    color: root.palette.textSecondary
                    font.pixelSize: 13
                }

                Item { Layout.fillHeight: true }

                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 144
                    Layout.preferredHeight: 144
                    radius: 72
                    color: "#2052d6c8"
                    border.width: 2
                    border.color: root.palette.accentStrong

                    Rectangle {
                        anchors.centerIn: parent
                        width: 92
                        height: 92
                        radius: 46
                        color: root.palette.accent

                        Label {
                            anchors.centerIn: parent
                            text: "AI"
                            color: root.palette.background
                            font.pixelSize: 28
                            font.weight: Font.Bold
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: "你好，小山"
                    color: root.palette.textPrimary
                    font.pixelSize: 30
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                }

                Label {
                    Layout.fillWidth: true
                    text: "当前页面只展示能力状态；真实会话状态接口将在后续 UI 批次接入"
                    color: root.palette.textSecondary
                    font.pixelSize: 12
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }

                Item { Layout.fillHeight: true }

                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 114
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
                            color: root.palette.green
                        }

                        Label {
                            text: "等待唤醒"
                            color: root.palette.textPrimary
                            font.pixelSize: 11
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
