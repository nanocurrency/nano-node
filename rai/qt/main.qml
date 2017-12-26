import QtQuick 2.9
import QtQuick.Controls 2.2
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

        Pane {
            Layout.fillWidth: false
            Layout.fillHeight: true

            background: Rectangle {
                color: "white"
            }

            ColumnLayout {
                anchors.fill: parent

                Label {
                    text: "RaiBlocks"
                    Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
                }

                ScrollView {
                    Layout.fillHeight: true
                    Layout.fillWidth: false
                    ListView {
                        model: 20
                        delegate: ItemDelegate {
                            text: "Item " + index
                        }
                    }
                }

                Button {
                    text: "Settings"
                }
            }
        }

        Item {
            Layout.fillHeight: true
            Layout.fillWidth: true

            TabBar {
                id: bar
                width: parent.width

                TabButton {
                    text: qsTr("Balance")
                }
                TabButton {
                    text: qsTr("Receive")
                }
                TabButton {
                    text: qsTr("Send")
                }
            }

            StackLayout {
                currentIndex: bar.currentIndex
                width: parent.width
                anchors {
                    top: bar.bottom
                    bottom: footer.top
                }

                Item {
                    id: tabBalance

                    anchors.fill: parent

                    Pane {
                        id: balanceHeader

                        anchors {
                            top: parent.top
                            left: parent.left
                            right: parent.right
                        }

                        ColumnLayout {
                            anchors.fill: parent

                            Label {
                                Layout.alignment: Qt.AlignHCenter
                                text: qsTr("Balance")
                            }
                            Label {
                                Layout.alignment: Qt.AlignHCenter
                                text: qsTr("3547 XRB")
                            }
                            Label {
                                Layout.alignment: Qt.AlignHCenter
                                text: qsTr("14235 USD")
                            }
                        }
                    }
                    ScrollView {
                        anchors {
                            top: balanceHeader.bottom
                            bottom: parent.bottom
                            left: parent.left
                            right: parent.right
                        }

                        clip: true

                        ListView {
                            model: 20
                            delegate: ItemDelegate {
                                text: "Item " + index
                            }
                        }
                    }
                }

                Pane {
                    id: tabReceive

                    ColumnLayout {
                        anchors.fill: parent

                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("Receive")
                        }
                        RowLayout {
                            Label {
                                Layout.fillWidth: true
                                text: "xrb_012en12ien12iien10i2en10i2en"
                            }
                            Button {
                                text: qsTr("Copy")
                            }
                        }
                        Rectangle {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.minimumHeight: 128
                            Layout.minimumWidth: 128
                            color: "black"
                        }
                    }
                }

                Pane {
                    id: tabSend

                    ColumnLayout {
                        anchors.fill: parent

                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("Send")
                        }
                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.maximumWidth: parent.width
                            wrapMode: Text.Wrap
                            text: qsTr("Sending to the wrong address will result in the loss of your funds, please ensure to check you have the corrent address")
                        }
                        TextField {
                            Layout.fillWidth: true
                            placeholderText: qsTr("Amount")
                        }
                        TextField {
                            Layout.fillWidth: true
                            placeholderText: qsTr("Address")
                        }
                        Button {
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("Send")
                        }
                    }
                }
            }

            Pane {
                id: footer

                anchors {
                    bottom: parent.bottom
                    left: parent.left
                    right: parent.right
                }

                GridLayout {
                    anchors.fill: parent

                    rows: 1
                    columns: 2

                    Label {
                        Layout.alignment: Qt.AlignLeft
                        anchors.left: parent.left
                        text: qsTr("Block 15965 (1984035)")
                    }
                    Label {
                        Layout.alignment: Qt.AlignRight
                        anchors.right: parent.right
                        text: qsTr("Version ") + RAIBLOCKS_VERSION_MAJOR + "." + RAIBLOCKS_VERSION_MINOR
                    }
                }
            }
        }
    }
}
