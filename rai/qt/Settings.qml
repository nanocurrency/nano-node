import QtQuick 2.9
import QtQuick.Controls 2.3
import QtQuick.Layouts 1.3

import net.raiblocks 1.0

Pane {
    signal goBack()

    ColumnLayout {
        anchors.fill: parent

        GroupBox {
            id: gbAdhocKey
            Layout.alignment: Qt.AlignHCenter

            ColumnLayout {
                anchors.fill: parent

                TextField {
                    id: tfAdhocKey
                    placeholderText: qsTr("Adhoc Key")
                    validator: RegExpValidator {
                        regExp: /^[a-fA-F0-9]{64}$/
                    }
                }
                Button {
                    id: btnAdhocKey
                    text: qsTr("Import adhoc key")
                    onClicked: rai_accounts.insertAdhocKey(tfAdhocKey.text)
                    enabled: tfAdhocKey.acceptableInput
                }
            }
            Connections {
                target: rai_accounts
                onInsertAdhocKeyFinished: {
                    if (success) {
                        tfAdhocKey.clear()
                    }
                    gbAdhocKey.state = success ? "success" : "warning"
                }
            }
            Timer {
                id: timerAdhocKey
                interval: 2000
                onTriggered: gbAdhocKey.state = "normal"
            }
            state: "normal"
            states: [
                State {
                    name: "normal"
                },
                State {
                    name: "warning"
                    PropertyChanges {
                        target: tfAdhocKey
                        text: qsTr("Invalid key")
                        color: "red"
                    }
                    PropertyChanges {
                        target: btnAdhocKey
                        enabled: false
                    }
                    PropertyChanges {
                        target: timerAdhocKey
                        running: true
                    }
                },
                State {
                    name: "success"
                    PropertyChanges {
                        target: tfAdhocKey
                        text: qsTr("Key was imported")
                        color: "green"
                    }
                    PropertyChanges {
                        target: btnAdhocKey
                        enabled: false
                    }
                    PropertyChanges {
                        target: timerAdhocKey
                        running: true
                    }
                }
            ]
        }

        GroupBox {
            Layout.alignment: Qt.AlignHCenter
            ColumnLayout {
                Label {
                    text: qsTr("Preferred unit:")
                }
                RadioButton {
                    checked: rai_wallet.renderingRatio === RenderingRatio.XRB
                    text: "XRB = 10^30 raw"
                    onClicked: rai_wallet.renderingRatio = RenderingRatio.XRB
                }
                RadioButton {
                    checked: rai_wallet.renderingRatio === RenderingRatio.MilliXRB
                    text: "mXRB = 10^27 raw"
                    onClicked: rai_wallet.renderingRatio = RenderingRatio.MilliXRB
                }
                RadioButton {
                    checked: rai_wallet.renderingRatio === RenderingRatio.MicroXRB
                    text: "uXRB = 10^24 raw"
                    onClicked: rai_wallet.renderingRatio = RenderingRatio.MicroXRB
                }
            }
        }

        Button {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Back")
            onClicked: goBack()
        }
    }
}
