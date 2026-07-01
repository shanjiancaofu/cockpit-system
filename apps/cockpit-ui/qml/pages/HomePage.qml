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

        TabBar {
            id: viewTabs
            Layout.fillWidth: true
            Layout.preferredHeight: 42

            TabButton {
                text: "DASHBOARD"
            }

            TabButton {
                text: "CAMERA"
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: viewTabs.currentIndex

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
                Layout.fillHeight: true
                radius: 8
                color: "#080b0d"
                border.color: root.borderColor

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 54
                        color: root.surfaceRaised

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 14
                            anchors.rightMargin: 14
                            spacing: 10

                            ComboBox {
                                id: cameraDeviceBox
                                Layout.preferredWidth: 210
                                model: cameraControl.devices
                                enabled: !cameraControl.busy && !cameraControl.running
                                displayText: currentText.length > 0 ? currentText : "NO CAMERA DEVICE"
                            }

                            ComboBox {
                                id: cameraResolutionBox
                                Layout.preferredWidth: 130
                                model: ["640 x 480", "1280 x 720", "1920 x 1080"]
                                enabled: !cameraControl.busy && !cameraControl.running

                                readonly property int selectedWidth: currentIndex === 0 ? 640
                                                                       : (currentIndex === 1 ? 1280 : 1920)
                                readonly property int selectedHeight: currentIndex === 0 ? 480
                                                                        : (currentIndex === 1 ? 720 : 1080)
                            }

                            ComboBox {
                                id: cameraFpsBox
                                Layout.preferredWidth: 86
                                model: [30, 60]
                                enabled: !cameraControl.busy && !cameraControl.running
                                displayText: currentText + " FPS"
                            }

                            Button {
                                text: "START"
                                enabled: !cameraControl.busy && !cameraControl.running
                                         && cameraDeviceBox.currentText.length > 0
                                onClicked: cameraControl.startPreview(
                                               cameraDeviceBox.currentText,
                                               cameraResolutionBox.selectedWidth,
                                               cameraResolutionBox.selectedHeight,
                                               Number(cameraFpsBox.currentText))
                            }

                            Button {
                                text: "STOP"
                                enabled: !cameraControl.busy && cameraControl.running
                                onClicked: cameraControl.stopPreview()
                            }

                            Button {
                                text: "REFRESH"
                                enabled: !cameraControl.busy && !cameraControl.running
                                onClicked: cameraControl.refreshDevices()
                            }

                            Item {
                                Layout.fillWidth: true
                            }

                            Label {
                                text: cameraControl.busy ? "WORKING"
                                                         : (cameraControl.running ? "RUNNING" : "STOPPED")
                                color: cameraControl.running ? root.green : root.secondaryText
                                font.pixelSize: 12
                                font.bold: true
                            }
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        Image {
                            anchors.fill: parent
                            anchors.margins: 2
                            source: cameraFrame.frameSource
                            fillMode: Image.PreserveAspectFit
                            cache: false
                            visible: cameraFrame.hasFrame
                        }

                        Column {
                            anchors.centerIn: parent
                            spacing: 8
                            visible: !cameraFrame.hasFrame

                            Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: cameraControl.lastError.length > 0 ? cameraControl.lastError
                                      : (cameraFrame.connected ? "WAITING FOR CAMERA FRAMES"
                                                               : "CAMERA SERVICE DISCONNECTED")
                                color: cameraControl.lastError.length > 0 ? root.red
                                                                         : root.secondaryText
                                font.pixelSize: 16
                                font.bold: true
                            }

                            Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: cameraFrame.connected ? "Shared memory connected" : "Retrying"
                                color: root.secondaryText
                                font.pixelSize: 12
                            }
                        }

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 42
                            color: "#d90e1214"
                            visible: cameraFrame.hasFrame

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 16
                                anchors.rightMargin: 16

                                Label {
                                    text: cameraFrame.connected ? "LIVE CAMERA" : "LAST FRAME"
                                    color: cameraFrame.connected ? root.green : root.amber
                                    font.pixelSize: 12
                                    font.bold: true
                                }

                                Item {
                                    Layout.fillWidth: true
                                }

                                Label {
                                    text: cameraFrame.frameWidth + " x " + cameraFrame.frameHeight
                                          + "   FRAME " + cameraFrame.sequence
                                    color: root.secondaryText
                                    font.pixelSize: 12
                                }
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
