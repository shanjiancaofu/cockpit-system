import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    property var palette
    property var vehicleModel
    property var healthModel
    property date now: new Date()

    color: "transparent"

    Timer {
        interval: 1000
        running: true
        repeat: true
        onTriggered: root.now = new Date()
    }

    RowLayout {
        anchors.fill: parent
        spacing: 14

        Label {
            text: "COCKPIT"
            color: root.palette.textPrimary
            font.pixelSize: 18
            font.weight: Font.DemiBold
            font.letterSpacing: 2.4
        }

        Rectangle {
            Layout.preferredWidth: 1
            Layout.preferredHeight: 18
            color: root.palette.border
        }

        Rectangle {
            Layout.preferredWidth: 8
            Layout.preferredHeight: 8
            radius: 4
            color: root.vehicleModel.fresh ? root.palette.green
                                           : (root.vehicleModel.connected ? root.palette.amber
                                                                          : root.palette.red)
        }

        Label {
            text: root.vehicleModel.fresh ? "车辆在线"
                                          : (root.vehicleModel.connected ? "数据延迟" : "车辆离线")
            color: root.palette.textSecondary
            font.pixelSize: 12
        }

        Item { Layout.fillWidth: true }

        Rectangle {
            Layout.preferredWidth: 8
            Layout.preferredHeight: 8
            radius: 4
            color: root.healthModel.worstState === "OK" ? root.palette.green
                   : (root.healthModel.worstState === "DEGRADED" ? root.palette.amber
                      : (root.healthModel.worstState === "FAULTED" ? root.palette.red
                                                                    : root.palette.textMuted))
        }

        Label {
            text: root.healthModel.worstState === "OK" ? "系统正常" : "服务需检查"
            color: root.palette.textSecondary
            font.pixelSize: 12
        }

        Label {
            text: Qt.formatDateTime(root.now, "MM月dd日")
            color: root.palette.textSecondary
            font.pixelSize: 12
            Layout.leftMargin: 12
        }

        Label {
            text: Qt.formatDateTime(root.now, "HH:mm")
            color: root.palette.textPrimary
            font.pixelSize: 20
            font.weight: Font.DemiBold
        }
    }
}
