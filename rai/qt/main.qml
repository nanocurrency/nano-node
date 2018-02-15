import QtQuick 2.5
import QtQuick.Controls 1.4
import QtQuick.Layouts 1.2

ApplicationWindow {
    id: root

    visible: true
    minimumWidth: 480
    minimumHeight: 480
    title: qsTr("RaiBlocks Wallet")

// FIXME: Qt 5.9 doesn't provide palette to customize bg color, find replacement
//    palette.window: "#40B299"

    RowLayout {
        spacing: 0
        anchors.fill: parent

        SideBar {
            id: sidebar

            Layout.fillWidth: false
            Layout.fillHeight: true
            Layout.preferredWidth: parent.width / 3
            Layout.maximumHeight: parent.height

            onSettingsClicked: {
                btnSettingsEnabled = false
                stackView.push(settingsComponent, StackView.Immediate)
            }
        }

        StackView {
            id: stackView
            Layout.fillHeight: true
            Layout.fillWidth: true
            initialItem: MainView {
                anchors.fill: parent
            }
        }

        Component {
            id: settingsComponent

            Settings {
                id: settings
                anchors.fill: parent
                onGoBack: {
                    sidebar.btnSettingsEnabled = true
                    stackView.pop(StackView.Immediate)
                }
            }
        }
    }
}
