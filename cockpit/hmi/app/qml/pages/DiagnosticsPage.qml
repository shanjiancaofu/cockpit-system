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
            Layout.preferredWidth: 410
            Layout.fillHeight: true
            palette: root.palette

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Label {
                            text: "服务状态"
                            color: root.palette.textPrimary
                            font.pixelSize: 18
                            font.weight: Font.DemiBold
                        }

                        Label {
                            Layout.fillWidth: true
                            text: serviceHealth.summaryText
                            color: root.palette.textSecondary
                            font.pixelSize: 10
                            elide: Text.ElideRight
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: 12
                        Layout.preferredHeight: 12
                        radius: 6
                        color: serviceHealth.worstState === "OK" ? root.palette.green
                               : (serviceHealth.worstState === "DEGRADED" ? root.palette.amber
                                  : (serviceHealth.worstState === "FAULTED" ? root.palette.red
                                                                            : root.palette.textMuted))
                    }
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 8
                    model: serviceHealth

                    delegate: Rectangle {
                        width: ListView.view.width
                        height: 72
                        radius: 14
                        color: root.palette.surfaceRaised
                        border.width: 1
                        border.color: healthState === "FAULTED" ? root.palette.red
                                      : (healthState === "DEGRADED" ? root.palette.amber
                                                                     : root.palette.border)

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 14
                            anchors.rightMargin: 14
                            spacing: 10

                            Rectangle {
                                Layout.preferredWidth: 9
                                Layout.preferredHeight: 9
                                radius: 5
                                color: healthState === "OK" ? root.palette.green
                                       : (healthState === "DEGRADED" ? root.palette.amber
                                          : (healthState === "FAULTED" ? root.palette.red
                                                                        : root.palette.textMuted))
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3

                                RowLayout {
                                    Layout.fillWidth: true

                                    Label {
                                        Layout.fillWidth: true
                                        text: displayName
                                        color: root.palette.textPrimary
                                        font.pixelSize: 13
                                        font.weight: Font.DemiBold
                                    }

                                    Label {
                                        text: healthState
                                        color: healthState === "OK" ? root.palette.green
                                               : (healthState === "DEGRADED" ? root.palette.amber
                                                  : (healthState === "FAULTED" ? root.palette.red
                                                                                : root.palette.textMuted))
                                        font.pixelSize: 10
                                        font.weight: Font.Bold
                                    }
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: lastError.length > 0 ? lastError : message
                                    color: lastError.length > 0 ? root.palette.amber
                                                                : root.palette.textSecondary
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                }
            }
        }

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

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Label {
                            text: "最近状态变化"
                            color: root.palette.textPrimary
                            font.pixelSize: 18
                            font.weight: Font.DemiBold
                        }

                        Label {
                            text: "开发诊断信息，不在驾驶主页显示"
                            color: root.palette.textSecondary
                            font.pixelSize: 11
                        }
                    }

                    Label {
                        text: serviceHealth.recentTransitions.length + " / 32"
                        color: root.palette.textMuted
                        font.pixelSize: 11
                    }
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 8
                    model: serviceHealth.recentTransitions

                    delegate: Rectangle {
                        required property var modelData
                        width: ListView.view.width
                        height: 60
                        radius: 14
                        color: root.palette.surfaceRaised
                        border.width: 1
                        border.color: modelData.toState === "FAULTED" ? root.palette.red
                                      : (modelData.toState === "DEGRADED" ? root.palette.amber
                                                                           : root.palette.border)

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 14
                            anchors.rightMargin: 14
                            spacing: 12

                            Label {
                                Layout.preferredWidth: 108
                                text: modelData.displayName
                                color: root.palette.textPrimary
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }

                            Label {
                                Layout.preferredWidth: 154
                                text: modelData.fromState + "  →  " + modelData.toState
                                color: modelData.toState === "FAULTED" ? root.palette.red
                                       : (modelData.toState === "DEGRADED" ? root.palette.amber
                                                                           : root.palette.green)
                                font.pixelSize: 11
                                font.weight: Font.Bold
                            }

                            Label {
                                Layout.fillWidth: true
                                text: modelData.reason
                                color: root.palette.textSecondary
                                font.pixelSize: 11
                                elide: Text.ElideRight
                            }

                            Label {
                                text: Qt.formatDateTime(new Date(modelData.changedAtMs), "HH:mm:ss")
                                color: root.palette.textMuted
                                font.pixelSize: 10
                            }
                        }
                    }

                    Label {
                        anchors.centerIn: parent
                        visible: serviceHealth.recentTransitions.length === 0
                        text: "暂无状态变化"
                        color: root.palette.textMuted
                        font.pixelSize: 13
                    }
                }
            }
        }
    }
}
