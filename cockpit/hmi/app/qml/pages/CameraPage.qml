import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Item {
    id: root

    property var palette

    SurfaceCard {
        anchors.fill: parent
        palette: root.palette
        cardColor: root.palette.backgroundRaised

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 68
                color: root.palette.surface

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 18
                    anchors.rightMargin: 18
                    spacing: 10

                    ColumnLayout {
                        Layout.preferredWidth: 152
                        spacing: 2

                        Label {
                            text: "相机"
                            color: root.palette.textPrimary
                            font.pixelSize: 19
                            font.weight: Font.DemiBold
                        }

                        Label {
                            text: cameraControl.running ? "预览运行中" : "实时预览与拍照"
                            color: root.palette.textSecondary
                            font.pixelSize: 11
                        }
                    }

                    ComboBox {
                        id: cameraDeviceBox
                        Layout.preferredWidth: 220
                        model: cameraControl.devices
                        enabled: !cameraControl.busy && !cameraControl.running
                        displayText: currentText.length > 0 ? currentText : "未发现相机"
                    }

                    ComboBox {
                        id: cameraResolutionBox
                        Layout.preferredWidth: 132
                        model: ["640 × 480", "1280 × 720", "1920 × 1080"]
                        currentIndex: 1
                        enabled: !cameraControl.busy && !cameraControl.running

                        readonly property int selectedWidth: currentIndex === 0 ? 640
                                                               : (currentIndex === 1 ? 1280 : 1920)
                        readonly property int selectedHeight: currentIndex === 0 ? 480
                                                                : (currentIndex === 1 ? 720 : 1080)
                    }

                    ComboBox {
                        id: cameraFpsBox
                        Layout.preferredWidth: 92
                        model: [30, 60]
                        enabled: !cameraControl.busy && !cameraControl.running
                        displayText: currentText + " FPS"
                    }

                    Button {
                        text: cameraControl.running ? "停止" : "开始"
                        enabled: !cameraControl.busy && (cameraControl.running
                                 || cameraDeviceBox.currentText.length > 0)
                        onClicked: {
                            if (cameraControl.running) {
                                cameraControl.stopPreview()
                            } else {
                                cameraControl.startPreview(cameraDeviceBox.currentText,
                                                           cameraResolutionBox.selectedWidth,
                                                           cameraResolutionBox.selectedHeight,
                                                           Number(cameraFpsBox.currentText))
                            }
                        }
                    }

                    Button {
                        text: "拍照"
                        enabled: !cameraControl.busy && cameraControl.running
                                 && cameraFrame.hasFrame && cameraFrame.fresh
                        onClicked: cameraControl.takePhoto()
                    }

                    Button {
                        text: "刷新"
                        enabled: !cameraControl.busy && !cameraControl.running
                        onClicked: cameraControl.refreshDevices()
                    }

                    Item { Layout.fillWidth: true }

                    Rectangle {
                        Layout.preferredWidth: 8
                        Layout.preferredHeight: 8
                        radius: 4
                        color: cameraControl.running ? root.palette.green : root.palette.textMuted
                    }

                    Label {
                        text: cameraControl.busy ? "处理中" : (cameraControl.running ? "LIVE" : "STOPPED")
                        color: cameraControl.running ? root.palette.green : root.palette.textSecondary
                        font.pixelSize: 11
                        font.weight: Font.Bold
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
                    spacing: 12
                    visible: !cameraFrame.hasFrame

                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 72
                        height: 72
                        radius: 24
                        color: root.palette.surfaceRaised

                        Label {
                            anchors.centerIn: parent
                            text: "C"
                            color: root.palette.blue
                            font.pixelSize: 28
                            font.weight: Font.Bold
                        }
                    }

                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: cameraControl.lastError.length > 0 ? "相机暂不可用"
                              : (cameraFrame.connected ? "正在等待画面" : "相机服务未连接")
                        color: cameraControl.lastError.length > 0 ? root.palette.red
                                                                 : root.palette.textPrimary
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                    }

                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: cameraControl.lastError.length > 0 ? cameraControl.lastError
                              : (cameraFrame.connected ? "共享内存已连接" : "后台将自动重试")
                        color: root.palette.textSecondary
                        font.pixelSize: 12
                    }
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 46
                    color: "#e6090d12"
                    visible: cameraFrame.hasFrame

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 18
                        anchors.rightMargin: 18
                        spacing: 14

                        Label {
                            text: cameraFrame.connected && cameraFrame.fresh ? "实时画面"
                                  : (cameraFrame.connected ? "画面停滞" : "最后一帧")
                            color: cameraFrame.connected && cameraFrame.fresh
                                   ? root.palette.green : root.palette.amber
                            font.pixelSize: 12
                            font.weight: Font.Bold
                        }

                        Label {
                            Layout.fillWidth: true
                            visible: cameraControl.lastPhotoPath.length > 0
                            text: "照片已保存  " + cameraControl.lastPhotoPath
                            color: root.palette.textSecondary
                            font.pixelSize: 11
                            elide: Text.ElideMiddle
                        }

                        Label {
                            text: cameraFrame.frameWidth + " × " + cameraFrame.frameHeight
                                  + "  ·  FRAME " + cameraFrame.sequence
                            color: root.palette.textSecondary
                            font.pixelSize: 11
                        }
                    }
                }
            }
        }
    }
}
