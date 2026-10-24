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
                            text: mediaControl.available ? "本地媒体控制已连接"
                                                         : "媒体 owner 未接入，当前不会误报播放成功"
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
                            text: mediaControl.stateLabel
                            color: mediaControl.state === "PLAYING" ? root.palette.green
                                   : (mediaControl.state === "ERROR" ? root.palette.red
                                                                     : root.palette.textMuted)
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
                    text: mediaControl.title.length > 0 ? mediaControl.title
                                                        : "等待本地媒体后端"
                    color: root.palette.textPrimary
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                }

                Label {
                    Layout.fillWidth: true
                    text: mediaControl.artist.length > 0 ? mediaControl.artist
                                                         : "现有 Audio PCM RPC 不作为长音乐播放器"
                    color: root.palette.textSecondary
                    font.pixelSize: 12
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }

                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    visible: mediaControl.available
                    spacing: 10

                    Button {
                        text: mediaControl.state === "PLAYING" ? "暂停"
                              : (mediaControl.state === "PAUSED" ? "继续" : "播放")
                        enabled: mediaControl.canPlay || mediaControl.canPause
                        onClicked: {
                            if (mediaControl.canPlay) {
                                mediaControl.playDefault()
                            } else {
                                mediaControl.togglePause()
                            }
                        }
                    }

                    Button {
                        text: "下一首"
                        enabled: mediaControl.canPause
                        onClicked: mediaControl.next()
                    }

                    Button {
                        text: "停止"
                        enabled: mediaControl.canStop
                        onClicked: mediaControl.stopPlayback()
                    }
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
                        text: mediaControl.lastError.length > 0 ? mediaControl.lastError
                              : (appLauncher.lastError.length > 0 ? appLauncher.lastError
                                 : "媒体和 App Launcher 只接受固定 ID · 禁止路径、URL、参数和 Shell")
                        color: mediaControl.lastError.length > 0 || appLauncher.lastError.length > 0
                               ? root.palette.amber : root.palette.textMuted
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
                model: appLauncher

                delegate: SurfaceCard {
                    required property string appId
                    required property string displayName
                    required property string appMark
                    required property string description
                    required property string appState
                    required property string stateLabel
                    required property string appMessage
                    required property bool appAvailable
                    required property bool appRunning
                    required property bool appBusy
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    palette: root.palette
                    strokeColor: appState === "FAILED" ? root.palette.red : root.palette.border

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
                                text: appMark
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
                                    text: displayName
                                    color: root.palette.textPrimary
                                    font.pixelSize: 14
                                    font.weight: Font.DemiBold
                                }

                                Label {
                                    text: stateLabel
                                    color: appState === "RUNNING" ? root.palette.green
                                           : (appState === "FAILED" ? root.palette.red
                                                                    : root.palette.textMuted)
                                    font.pixelSize: 10
                                }
                            }

                            Label {
                                Layout.fillWidth: true
                                text: appMessage.length > 0 ? appMessage : description
                                color: root.palette.textSecondary
                                font.pixelSize: 11
                                wrapMode: Text.WordWrap
                            }
                        }

                        Rectangle {
                            Layout.preferredWidth: 62
                            Layout.preferredHeight: 32
                            radius: 12
                            visible: appAvailable
                            color: appBusy ? root.palette.surfaceRaised
                                           : (appButtonMouse.containsMouse
                                              ? root.palette.surfaceHover : root.palette.backgroundRaised)
                            border.width: 1
                            border.color: appRunning ? root.palette.amber : root.palette.accentStrong

                            Label {
                                anchors.centerIn: parent
                                text: appBusy ? "处理中" : (appRunning ? "停止" : "启动")
                                color: appBusy ? root.palette.textMuted : root.palette.textPrimary
                                font.pixelSize: 10
                                font.weight: Font.DemiBold
                            }

                            MouseArea {
                                id: appButtonMouse
                                anchors.fill: parent
                                enabled: !appBusy
                                hoverEnabled: true
                                onClicked: {
                                    if (appRunning) {
                                        appLauncher.stop(appId)
                                    } else {
                                        appLauncher.launch(appId)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
