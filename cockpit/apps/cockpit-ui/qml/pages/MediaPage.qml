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
                spacing: 18

                RowLayout {
                    Layout.fillWidth: true

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Label {
                            text: "媒体中心"
                            color: root.palette.textPrimary
                            font.pixelSize: 24
                            font.weight: Font.DemiBold
                        }

                        Label {
                            text: "播放器后端尚未接入，当前不会误报播放成功"
                            color: root.palette.textSecondary
                            font.pixelSize: 12
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: 82
                        Layout.preferredHeight: 30
                        radius: 15
                        color: root.palette.surfaceRaised

                        Label {
                            anchors.centerIn: parent
                            text: "未连接"
                            color: root.palette.textMuted
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 152
                    Layout.preferredHeight: 152
                    radius: 40
                    color: root.palette.surfaceRaised

                    Rectangle {
                        anchors.centerIn: parent
                        width: 86
                        height: 86
                        radius: 26
                        color: "#2867a8ff"

                        Label {
                            anchors.centerIn: parent
                            text: "M"
                            color: root.palette.blue
                            font.pixelSize: 42
                            font.weight: Font.Bold
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: "选择音乐后端后，这里将显示封面、曲目和播放控制"
                    color: root.palette.textSecondary
                    font.pixelSize: 14
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }

                Item { Layout.fillHeight: true }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 56
                    radius: 16
                    color: root.palette.backgroundRaised
                    border.width: 1
                    border.color: root.palette.border

                    Label {
                        anchors.centerIn: parent
                        text: "受控 App Launcher 尚未实现 · 禁止任意 Shell 启动"
                        color: root.palette.textMuted
                        font.pixelSize: 12
                    }
                }
            }
        }

        ColumnLayout {
            Layout.preferredWidth: 330
            Layout.minimumWidth: 330
            Layout.maximumWidth: 330
            Layout.fillHeight: true
            spacing: 14

            Repeater {
                model: [
                    {"mark": "L", "title": "本地音乐", "detail": "等待原生播放器后端", "state": "待接入"},
                    {"mark": "P", "title": "手机互联", "detail": "等待投屏与音频焦点方案", "state": "待选型"},
                    {"mark": "A", "title": "Android 应用", "detail": "当前 Ubuntu 不运行 APK", "state": "不可用"}
                ]

                delegate: SurfaceCard {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    palette: root.palette

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 18
                        spacing: 14

                        Rectangle {
                            Layout.preferredWidth: 48
                            Layout.preferredHeight: 48
                            radius: 16
                            color: root.palette.surfaceRaised

                            Label {
                                anchors.centerIn: parent
                                text: modelData.mark
                                color: root.palette.accent
                                font.pixelSize: 18
                                font.weight: Font.Bold
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            RowLayout {
                                Layout.fillWidth: true

                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.title
                                    color: root.palette.textPrimary
                                    font.pixelSize: 14
                                    font.weight: Font.DemiBold
                                }

                                Label {
                                    text: modelData.state
                                    color: root.palette.textMuted
                                    font.pixelSize: 10
                                }
                            }

                            Label {
                                Layout.fillWidth: true
                                text: modelData.detail
                                color: root.palette.textSecondary
                                font.pixelSize: 11
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
            }
        }
    }
}
