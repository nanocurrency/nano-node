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
            Layout.fillWidth: false
            Layout.fillHeight: true
        }

        MainView {
            Layout.fillHeight: true
            Layout.fillWidth: true
        }
    }
}
