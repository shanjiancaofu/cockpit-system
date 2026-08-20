import QtQuick
import QtQuick.Layouts
import "../components"

Rectangle {
    id: root
    objectName: "cockpitShell"

    color: palette.background

    QtObject {
        id: palette

        readonly property color background: "#090d12"
        readonly property color backgroundRaised: "#0d141c"
        readonly property color surface: "#111a24"
        readonly property color surfaceRaised: "#172330"
        readonly property color surfaceHover: "#1d2c3a"
        readonly property color border: "#263747"
        readonly property color textPrimary: "#f4f7fa"
        readonly property color textSecondary: "#91a1af"
        readonly property color textMuted: "#607384"
        readonly property color accent: "#52d6c8"
        readonly property color accentStrong: "#20b8aa"
        readonly property color blue: "#67a8ff"
        readonly property color green: "#58d68d"
        readonly property color amber: "#f5bd61"
        readonly property color red: "#ff6f7d"
    }

    Rectangle {
        width: parent.width * 0.62
        height: parent.height * 0.72
        anchors.right: parent.right
        anchors.top: parent.top
        color: "transparent"
        opacity: 0.42

        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "#00000000" }
            GradientStop { position: 1.0; color: "#20213f4a" }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 24
        anchors.rightMargin: 24
        anchors.topMargin: 12
        anchors.bottomMargin: 14
        spacing: 12

        StatusBar {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            palette: palette
            vehicleModel: vehicleState
            healthModel: serviceHealth
        }

        StackLayout {
            objectName: "pageStack"
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: hmiControl.currentView

            DashboardPage { palette: palette }
            CameraPage { palette: palette }
            DiagnosticsPage { palette: palette }
            MediaPage { palette: palette }
            VoicePage { palette: palette }
            SettingsPage { palette: palette }
        }

        AppDock {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: Math.min(840, root.width - 48)
            Layout.preferredHeight: 72
            palette: palette
            currentIndex: hmiControl.currentView
            onPageRequested: function(index) {
                hmiControl.currentView = index
            }
        }
    }
}
