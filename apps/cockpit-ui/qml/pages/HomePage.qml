import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    readonly property color surface: "#171d20"
    readonly property color surfaceRaised: "#20272b"
    readonly property color borderColor: "#354046"
    readonly property color primaryText: "#f4f7f8"
    readonly property color secondaryText: "#9aa7ad"
    readonly property color green: "#45d483"
    readonly property color amber: "#f2b84b"
    readonly property color cyan: "#58c7e8"
    readonly property color red: "#ef6a6a"

    Rectangle {
        anchors.fill: parent
        color: "#0e1214"
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 20

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 54
            spacing: 18

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Label {
                    text: "SMART COCKPIT"
                    color: root.primaryText
                    font.pixelSize: 22
                    font.bold: true
                }

                Label {
                    text: "Vehicle control center"
                    color: root.secondaryText
                    font.pixelSize: 13
                }
            }

            Rectangle {
                Layout.preferredWidth: 10
                Layout.preferredHeight: 10
                radius: 5
                color: vehicleState.fresh ? root.green
                                          : (vehicleState.connected ? root.amber : root.red)
            }

            ColumnLayout {
                spacing: 1

                Label {
                    Layout.alignment: Qt.AlignRight
                    text: vehicleState.fresh ? "LIVE"
                                             : (vehicleState.connected ? "STALE" : "DISCONNECTED")
                    color: vehicleState.fresh ? root.green
                                              : (vehicleState.connected ? root.amber : root.red)
                    font.pixelSize: 13
                    font.bold: true
                }

                Label {
                    Layout.alignment: Qt.AlignRight
                    text: vehicleState.source
                    color: root.secondaryText
                    font.pixelSize: 12
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 18

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 560
                radius: 8
                color: root.surface
                border.color: root.borderColor

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 30
                    spacing: 8

                    Label {
                        text: "VEHICLE SPEED"
                        color: root.secondaryText
                        font.pixelSize: 14
                        font.bold: true
                    }

                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        Column {
                            anchors.centerIn: parent
                            spacing: 0

                            Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: Math.round(vehicleState.speedKph).toString()
                                color: root.primaryText
                                font.pixelSize: 112
                                font.bold: true
                            }

                            Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: "km/h"
                                color: root.cyan
                                font.pixelSize: 21
                                font.bold: true
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 8
                        radius: 4
                        color: root.surfaceRaised

                        Rectangle {
                            width: parent.width * Math.max(0, Math.min(vehicleState.speedKph / 160.0, 1.0))
                            height: parent.height
                            radius: 4
                            color: root.cyan
                        }
                    }
                }
            }

            ColumnLayout {
                Layout.preferredWidth: 330
                Layout.fillHeight: true
                spacing: 18

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 8
                    color: root.surface
                    border.color: root.borderColor

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 24

                        Label {
                            text: "GEAR"
                            color: root.secondaryText
                            font.pixelSize: 14
                            font.bold: true
                        }

                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.fillHeight: true
                            verticalAlignment: Text.AlignVCenter
                            text: vehicleState.gear.toString()
                            color: root.primaryText
                            font.pixelSize: 76
                            font.bold: true
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 8
                    color: root.surface
                    border.color: root.borderColor

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 24
                        spacing: 12

                        RowLayout {
                            Layout.fillWidth: true

                            Label {
                                Layout.fillWidth: true
                                text: "BATTERY"
                                color: root.secondaryText
                                font.pixelSize: 14
                                font.bold: true
                            }

                            Label {
                                text: vehicleState.socPercent + "%"
                                color: vehicleState.socPercent > 20 ? root.green : root.amber
                                font.pixelSize: 30
                                font.bold: true
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 18
                            radius: 5
                            color: root.surfaceRaised

                            Rectangle {
                                width: parent.width * Math.max(0, Math.min(vehicleState.socPercent / 100.0, 1.0))
                                height: parent.height
                                radius: 5
                                color: vehicleState.socPercent > 20 ? root.green : root.amber
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 54
            radius: 8
            color: root.surfaceRaised

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                spacing: 28

                Label {
                    text: "GATEWAY"
                    color: root.secondaryText
                    font.pixelSize: 12
                    font.bold: true
                }

                Label {
                    text: vehicleState.connected ? "CONNECTED" : "RECONNECTING"
                    color: vehicleState.connected ? root.green : root.amber
                    font.pixelSize: 13
                    font.bold: true
                }

                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 22
                    color: root.borderColor
                }

                Label {
                    text: "CLOUD"
                    color: root.secondaryText
                    font.pixelSize: 12
                    font.bold: true
                }

                Label {
                    text: vehicleState.cloudEnabled ? "ENABLED" : "OFFLINE"
                    color: vehicleState.cloudEnabled ? root.cyan : root.secondaryText
                    font.pixelSize: 13
                    font.bold: true
                }

                Item {
                    Layout.fillWidth: true
                }

                Label {
                    text: vehicleState.fresh ? "LIVE DATA"
                                             : (vehicleState.timestampMs > 0 ? "STALE DATA" : "NO DATA")
                    color: vehicleState.fresh ? root.green
                                              : (vehicleState.timestampMs > 0 ? root.amber : root.secondaryText)
                    font.pixelSize: 12
                }
            }
        }
    }
}
