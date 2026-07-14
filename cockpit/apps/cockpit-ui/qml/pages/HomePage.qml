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
            currentIndex: hmiControl.currentView
            onCurrentIndexChanged: hmiControl.currentView = currentIndex

            TabButton {
                text: "DASHBOARD"
            }

            TabButton {
                text: "CAMERA"
            }

            TabButton {
                text: "DIAGNOSTICS"
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
                            anchors.margins: 22
                            spacing: 14

                            RowLayout {
                                Layout.fillWidth: true

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2

                                    Label {
                                        text: "SERVICE HEALTH"
                                        color: root.secondaryText
                                        font.pixelSize: 14
                                        font.bold: true
                                    }

                                    Label {
                                        text: serviceHealth.summaryText
                                        color: root.secondaryText
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                }

                                Rectangle {
                                    Layout.preferredWidth: 12
                                    Layout.preferredHeight: 12
                                    radius: 6
                                    color: serviceHealth.worstState === "OK" ? root.green
                                           : (serviceHealth.worstState === "DEGRADED" ? root.amber
                                              : (serviceHealth.worstState === "FAULTED" ? root.red
                                                 : root.secondaryText))
                                }
                            }

                            ListView {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                interactive: false
                                spacing: 8
                                model: serviceHealth

                                delegate: Rectangle {
                                    width: ListView.view.width
                                    height: 74
                                    radius: 6
                                    color: root.surfaceRaised
                                    border.color: healthState === "FAULTED" ? root.red
                                                  : (healthState === "DEGRADED" ? root.amber
                                                     : root.borderColor)

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 12
                                        anchors.rightMargin: 12
                                        spacing: 10

                                        Rectangle {
                                            Layout.preferredWidth: 9
                                            Layout.preferredHeight: 9
                                            radius: 5
                                            color: healthState === "OK" ? root.green
                                                   : (healthState === "DEGRADED" ? root.amber
                                                      : (healthState === "FAULTED" ? root.red
                                                         : root.secondaryText))
                                        }

                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 2

                                            RowLayout {
                                                Layout.fillWidth: true
                                                spacing: 8

                                                Label {
                                                    Layout.fillWidth: true
                                                    text: displayName
                                                    color: root.primaryText
                                                    font.pixelSize: 13
                                                    font.bold: true
                                                    elide: Text.ElideRight
                                                }

                                                Label {
                                                    text: healthState
                                                    color: healthState === "OK" ? root.green
                                                           : (healthState === "DEGRADED" ? root.amber
                                                              : (healthState === "FAULTED" ? root.red
                                                                 : root.secondaryText))
                                                    font.pixelSize: 11
                                                    font.bold: true
                                                }
                                            }

                                            Label {
                                                Layout.fillWidth: true
                                                text: lastError.length > 0 ? lastError : message
                                                color: lastError.length > 0 ? root.amber : root.secondaryText
                                                font.pixelSize: 11
                                                elide: Text.ElideRight
                                            }

                                            Label {
                                                Layout.fillWidth: true
                                                visible: lastProblemAtMs > 0
                                                text: "Last " + lastProblemState + " "
                                                      + Qt.formatDateTime(new Date(lastProblemAtMs), "HH:mm:ss")
                                                      + " | " + lastProblemReason
                                                color: root.amber
                                                font.pixelSize: 10
                                                elide: Text.ElideRight
                                            }
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 1
                                color: root.borderColor
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 12

                                Label {
                                    text: "GEAR"
                                    color: root.secondaryText
                                    font.pixelSize: 11
                                    font.bold: true
                                }

                                Label {
                                    text: vehicleState.gear.toString()
                                    color: root.primaryText
                                    font.pixelSize: 26
                                    font.bold: true
                                }

                                Item {
                                    Layout.fillWidth: true
                                }

                                Label {
                                    text: "BATTERY"
                                    color: root.secondaryText
                                    font.pixelSize: 11
                                    font.bold: true
                                }

                                Label {
                                    text: vehicleState.socPercent + "%"
                                    color: vehicleState.socPercent > 20 ? root.green : root.amber
                                    font.pixelSize: 24
                                    font.bold: true
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
                                text: "PHOTO"
                                enabled: !cameraControl.busy && cameraControl.running
                                         && cameraFrame.hasFrame && cameraFrame.fresh
                                onClicked: cameraControl.takePhoto()
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
                                    text: cameraFrame.connected && cameraFrame.fresh
                                          ? "LIVE CAMERA"
                                          : (cameraFrame.connected ? "CAMERA STALLED" : "LAST FRAME")
                                    color: cameraFrame.connected && cameraFrame.fresh
                                           ? root.green : root.amber
                                    font.pixelSize: 12
                                    font.bold: true
                                }

                                Label {
                                    visible: cameraControl.lastPhotoPath.length > 0
                                    text: cameraControl.lastPhotoPath
                                    color: root.secondaryText
                                    font.pixelSize: 11
                                    elide: Text.ElideMiddle
                                    Layout.maximumWidth: 360
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

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 8
                color: root.surface
                border.color: root.borderColor

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 22
                    spacing: 14

                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            Layout.fillWidth: true
                            text: "HEALTH TRANSITIONS"
                            color: root.primaryText
                            font.pixelSize: 16
                            font.bold: true
                        }

                        Label {
                            text: serviceHealth.recentTransitions.length + " / 32"
                            color: root.secondaryText
                            font.pixelSize: 12
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: root.borderColor
                    }

                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 6
                        model: serviceHealth.recentTransitions

                        delegate: Rectangle {
                            required property var modelData
                            width: ListView.view.width
                            height: 58
                            radius: 6
                            color: root.surfaceRaised
                            border.color: modelData.toState === "FAULTED" ? root.red
                                          : (modelData.toState === "DEGRADED" ? root.amber
                                                                             : root.borderColor)

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 14
                                anchors.rightMargin: 14
                                spacing: 14

                                Label {
                                    Layout.preferredWidth: 110
                                    text: modelData.displayName
                                    color: root.primaryText
                                    font.pixelSize: 13
                                    font.bold: true
                                    elide: Text.ElideRight
                                }

                                Label {
                                    Layout.preferredWidth: 190
                                    text: modelData.fromState + "  ->  " + modelData.toState
                                    color: modelData.toState === "FAULTED" ? root.red
                                           : (modelData.toState === "DEGRADED" ? root.amber
                                                                               : root.green)
                                    font.pixelSize: 12
                                    font.bold: true
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.reason
                                    color: root.secondaryText
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }

                                Label {
                                    Layout.preferredWidth: 72
                                    text: Qt.formatDateTime(new Date(modelData.changedAtMs), "HH:mm:ss")
                                    color: root.secondaryText
                                    font.pixelSize: 11
                                }
                            }
                        }

                        Label {
                            anchors.centerIn: parent
                            visible: serviceHealth.recentTransitions.length === 0
                            text: "NO STATE CHANGES"
                            color: root.secondaryText
                            font.pixelSize: 14
                            font.bold: true
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
