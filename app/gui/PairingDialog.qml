import QtQuick 2.9
import QtQuick.Controls 2.5
import QtQuick.Layouts 1.3

// Pairing sheet in the Selene style: the PIN is the one thing on the screen, in large
// digits, with a single line saying where to type it.
NavigableDialog {
    id: dialog

    property string pin: "0000"
    property string pcName: ""

    standardButtons: Dialog.Cancel
    closePolicy: Popup.CloseOnEscape

    ColumnLayout {
        spacing: 18

        Label {
            text: qsTr("Pair with %1").arg(dialog.pcName)
            font.pointSize: 18
            font.bold: true
            Layout.alignment: Qt.AlignHCenter
        }

        Row {
            spacing: 10
            Layout.alignment: Qt.AlignHCenter
            Accessible.role: Accessible.StaticText
            Accessible.name: qsTr("PIN %1").arg(dialog.pin.split("").join(" "))

            Repeater {
                model: dialog.pin.split("")
                delegate: Rectangle {
                    width: 52; height: 68
                    radius: 12
                    color: "#29FFFFFF"
                    Label {
                        anchors.centerIn: parent
                        text: modelData
                        font.pointSize: 32
                        font.bold: true
                        font.family: "Menlo"
                    }
                }
            }
        }

        Label {
            text: qsTr("Enter this PIN in Apollo or Sunshine on the PC. This closes when pairing is done.")
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            opacity: 0.7
            Layout.preferredWidth: 380
            Layout.alignment: Qt.AlignHCenter
        }
    }
}
