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
                            text: "应用入口受固定 allowlist 管理，后端不可用时不会误报成功"
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
                            text: "受控入口"
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
                        text: appLauncher.lastError.length > 0
                              ? appLauncher.lastError
                              : "App Launcher 只接受固定应用 ID · 禁止路径、参数和 Shell"
                        color: appLauncher.lastError.length > 0 ? root.palette.amber
                                                                : root.palette.textMuted
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
