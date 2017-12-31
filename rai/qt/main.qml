import QtQuick 2.9
import QtQuick.Controls 2.3
import QtQuick.Layouts 1.3

ApplicationWindow {
    id: root

    visible: true
    minimumWidth: 480
    minimumHeight: 480
    title: qsTr("RaiBlocks Wallet")

    palette.window: "#40B299"

    RowLayout {
        spacing: 0
        anchors.fill: parent

        SideBar {
            id: sidebar

            Layout.fillWidth: false
            Layout.fillHeight: true

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
