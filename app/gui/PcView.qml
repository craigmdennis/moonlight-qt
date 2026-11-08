import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

import ComputerModel 1.0

import ComputerManager 1.0
import StreamingPreferences 1.0
import SystemProperties 1.0
import SdlGamepadKeyNavigation 1.0

CenteredGridView {
    property ComputerModel computerModel : createModel()
    // Read by main.qml to draw the backdrop behind the grid
    property url backdropSource

    id: pcGrid
    focus: true
    activeFocusOnTab: true
    topMargin: 20
    bottomMargin: 5
    cellWidth: 240; cellHeight: 360;
    objectName: qsTr("Computers")

    Component.onCompleted: {
        // Don't show any highlighted item until interacting with them.
        // We do this here instead of onActivated to avoid losing the user's
        // selection when backing out of a different page of the app.
        currentIndex = -1
    }

    // Note: Any initialization done here that is critical for streaming must
    // also be done in CliStartStreamSegue.qml, since this code does not run
    // for command-line initiated streams.
    StackView.onActivated: {
        // Setup signals on CM
        ComputerManager.computerAddCompleted.connect(addComplete)

        // Highlight the first item if a gamepad is connected
        if (currentIndex === -1 && SdlGamepadKeyNavigation.getConnectedGamepads() > 0) {
            currentIndex = 0
        }
    }

    StackView.onDeactivating: {
        ComputerManager.computerAddCompleted.disconnect(addComplete)
    }

    function pairingComplete(error)
    {
        // Close the PIN dialog
        pairDialog.close()

        // Display a failed dialog if we got an error
        if (error !== undefined) {
            errorDialog.text = error
            errorDialog.helpText = ""
            errorDialog.open()
        }
    }

    function addComplete(success, detectedPortBlocking)
    {
        if (!success) {
            errorDialog.text = qsTr("Unable to connect to the specified PC.")

            if (detectedPortBlocking) {
                errorDialog.text += "\n\n" + qsTr("This PC's Internet connection is blocking Moonlight. Streaming over the Internet may not work while connected to this network.")
            }
            else {
                errorDialog.helpText = qsTr("Click the Help button for possible solutions.")
            }

            errorDialog.open()
        }
    }

    function createModel()
    {
        var model = Qt.createQmlObject('import ComputerModel 1.0; ComputerModel {}', parent, '')
        model.initialize(ComputerManager)
        model.pairingCompleted.connect(pairingComplete)
        model.connectionTestCompleted.connect(testConnectionDialog.connectionTestComplete)
        return model
    }

    Row {
        anchors.centerIn: parent
        spacing: 5
        visible: pcGrid.count === 0

        BusyIndicator {
            id: searchSpinner
            visible: StreamingPreferences.enableMdns
            running: visible
        }

        Label {
            height: searchSpinner.height
            elide: Label.ElideRight
            text: StreamingPreferences.enableMdns ? qsTr("Searching for compatible hosts on your local network...")
                                                  : qsTr("Automatic PC discovery is disabled. Add your PC manually.")
            font.pointSize: 20
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.Wrap
        }
    }

    model: computerModel

    delegate: NavigableItemDelegate {
        // The cell is the hit area. The card is inset 8 pt inside it, so a tap that lands
        // slightly outside the visible card still counts (macOS touch guidance: enlarge
        // tap targets, not controls).
        width: pcGrid.cellWidth; height: pcGrid.cellHeight;
        padding: 0
        grid: pcGrid

        property alias pcContextMenu : pcContextMenuLoader.item

        // Selene state vocabulary: the same words and colours as the Apple TV badge
        // Wallpaper of this PC, shown behind the grid while the tile is pointed at or selected
        readonly property url backdrop: model.backdrop
        onBackdropChanged: updateBackdrop()
        onHighlightedChanged: updateBackdrop()
        onHoveredChanged: updateBackdrop()
        function updateBackdrop() {
            if (backdrop.toString() !== "" && (highlighted || hovered || pcGrid.backdropSource.toString() === "")) {
                pcGrid.backdropSource = backdrop
            }
        }
        Component.onCompleted: updateBackdrop()

        readonly property bool busy: model.statusUnknown || model.sleeping
        readonly property bool inUse: model.online && model.paired && model.busy
        readonly property string badgeText: !model.paired && model.online ? qsTr("Not Paired")
                                          : inUse ? qsTr("In Use")
                                          : model.online ? qsTr("Online") : qsTr("Offline")
        readonly property color badgeColor: !model.paired && model.online ? "#FFFFFF"
                                          : inUse ? "#BF5AF2"
                                          : model.online ? "#30D158" : "#8E8E93"
        // Apollo's Steam line (last played, currently playing) wins over the generic text
        readonly property string stateLine: model.sleeping ? qsTr("Sleeping…")
                                          : model.statusUnknown ? qsTr("Checking…")
                                          : !model.online ? (model.statusLine !== "" ? model.statusLine
                                                             : model.wakeable ? qsTr("Offline · Click to wake") : qsTr("Offline"))
                                          : !model.paired ? qsTr("Found on your network · Click to pair")
                                          : model.statusLine !== "" ? model.statusLine
                                          : qsTr("Ready to play")
        // A stable tint per computer, so tiles are told apart without game art
        readonly property color tint: Qt.hsla((Math.abs(hashName(model.name)) % 360) / 360, 0.35, 0.28, 1)

        function hashName(n) {
            var h = 0
            for (var i = 0; i < n.length; i++) h = (h * 31 + n.charCodeAt(i)) | 0
            return h
        }

        background: Item {
            Rectangle {
                id: card
                x: 8; y: 8
                width: parent.width - 16; height: parent.height - 16 - 30
                radius: 18
                color: tint
                border.width: highlighted ? 3 : 0
                border.color: palette.highlight
                scale: hovered || highlighted ? 1.02 : 1.0
                Behavior on scale { NumberAnimation { duration: 120 } }

                Image {
                    anchors.centerIn: parent
                    anchors.verticalCenterOffset: -20
                    source: "qrc:/res/desktop_windows-48px.svg"
                    sourceSize { width: 96; height: 96 }
                    opacity: 0.9
                }

                Rectangle {
                    anchors.fill: parent
                    radius: card.radius
                    gradient: Gradient {
                        GradientStop { position: 0.5; color: "transparent" }
                        GradientStop { position: 1.0; color: "#BF000000" }
                    }
                }

                Label {
                    id: pcNameText
                    text: model.name
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 14 }
                    color: "white"
                    font.pointSize: 17
                    font.bold: true
                    maximumLineCount: 2
                    wrapMode: Text.Wrap
                    elide: Text.ElideRight
                }

                // Badge: top right, as on the Apple TV tile
                Rectangle {
                    anchors { top: parent.top; right: parent.right; margins: 10 }
                    height: 26; width: badgeRow.width + 20
                    radius: height / 2
                    color: "#99000000"
                    Row {
                        id: badgeRow
                        anchors.centerIn: parent
                        spacing: 6
                        BusyIndicator {
                            visible: busy; running: busy
                            width: 16; height: 16
                        }
                        Rectangle {
                            visible: !busy
                            width: 9; height: 9; radius: 4.5
                            color: badgeColor
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Label {
                            visible: !busy
                            text: badgeText
                            color: "white"
                            font.pointSize: 11
                            font.weight: Font.DemiBold
                        }
                    }
                }
            }

            // One line under the card: what happened and what clicking does
            Label {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom; leftMargin: 12; rightMargin: 12; bottomMargin: 6 }
                text: stateLine
                elide: Text.ElideRight
                font.pointSize: 12
                opacity: 0.7
                horizontalAlignment: Text.AlignHCenter
            }
        }

        // Display a tooltip with the full name if it's truncated
        ToolTip.text: model.name
        ToolTip.delay: 1000
        ToolTip.timeout: 5000
        ToolTip.visible: (hovered || highlighted) && pcNameText.truncated

        Loader {
            id: pcContextMenuLoader
            asynchronous: true
            sourceComponent: NavigableMenu {
                id: pcContextMenu
                initiator: pcContextMenuLoader.parent
                MenuItem {
                    text: qsTr("PC Status: %1").arg(model.online ? qsTr("Online") : qsTr("Offline"))
                    font.bold: true
                    enabled: false
                }
                NavigableMenuItem {
                    text: qsTr("View All Apps")
                    onTriggered: {
                        var component = Qt.createComponent("AppView.qml")
                        var appView = component.createObject(stackView, {"computerIndex": index, "objectName": model.name, "showHiddenGames": true})
                        stackView.push(appView)
                    }
                    visible: model.online && model.paired
                }
                NavigableMenuItem {
                    text: qsTr("Wake PC")
                    onTriggered: computerModel.wakeComputer(index)
                    visible: !model.online && model.wakeable
                }
                NavigableMenuItem {
                    // Kept for every paired PC so the menu never reorders; disabled while the
                    // PC cannot be put to sleep (offline, or someone is streaming from it)
                    text: qsTr("Sleep PC")
                    onTriggered: computerModel.sleepComputer(index)
                    visible: model.paired
                    enabled: model.online && !model.busy
                }
                NavigableMenuItem {
                    text: qsTr("Test Network")
                    onTriggered: {
                        computerModel.testConnectionForComputer(index)
                        testConnectionDialog.open()
                    }
                }

                NavigableMenuItem {
                    text: qsTr("Rename PC")
                    onTriggered: {
                        renamePcDialog.pcIndex = index
                        renamePcDialog.originalName = model.name
                        renamePcDialog.open()
                    }
                }
                NavigableMenuItem {
                    text: qsTr("Delete PC")
                    onTriggered: {
                        deletePcDialog.pcIndex = index
                        deletePcDialog.pcName = model.name
                        deletePcDialog.open()
                    }
                }
                NavigableMenuItem {
                    text: qsTr("View Details")
                    onTriggered: {
                        showPcDetailsDialog.pcDetails = model.details
                        showPcDetailsDialog.open()
                    }
                }
            }
        }

        onClicked: {
            if (model.online) {
                if (!model.serverSupported) {
                    errorDialog.text = qsTr("The version of GeForce Experience on %1 is not supported by this build of Moonlight. You must update Moonlight to stream from %1.").arg(model.name)
                    errorDialog.helpText = ""
                    errorDialog.open()
                }
                else if (model.paired) {
                    // go to game view
                    var component = Qt.createComponent("AppView.qml")
                    var appView = component.createObject(stackView, {"computerIndex": index, "objectName": model.name})
                    stackView.push(appView)
                }
                else {
                    var pin = computerModel.generatePinString()

                    // Kick off pairing in the background
                    computerModel.pairComputer(index, pin)

                    // Display the pairing dialog
                    pairDialog.pin = pin
                    pairDialog.pcName = model.name
                    pairDialog.open()
                }
            } else if (!model.online) {
                // Using open() here because it may be activated by keyboard
                pcContextMenu.open()
            }
        }

        onPressAndHold: {
            // popup() ensures the menu appears under the mouse cursor
            if (pcContextMenu.popup) {
                pcContextMenu.popup()
            }
            else {
                // Qt 5.9 doesn't have popup()
                pcContextMenu.open()
            }
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.RightButton;
            onClicked: {
                parent.pressAndHold()
            }
        }

        Keys.onMenuPressed: {
            // We must use open() here so the menu is positioned on
            // the ItemDelegate and not where the mouse cursor is
            pcContextMenu.open()
        }

        Keys.onDeletePressed: {
            deletePcDialog.pcIndex = index
            deletePcDialog.pcName = model.name
            deletePcDialog.open()
        }
    }

    ErrorMessageDialog {
        id: errorDialog

        // Using Setup-Guide here instead of Troubleshooting because it's likely that users
        // will arrive here by forgetting to enable GameStream or not forwarding ports.
        helpUrl: "https://github.com/moonlight-stream/moonlight-docs/wiki/Setup-Guide"
    }

    PairingDialog {
        id: pairDialog
        onRejected: {
            // FIXME: We should interrupt pairing here
        }
    }

    NavigableMessageDialog {
        id: deletePcDialog
        // don't allow edits to the rest of the window while open
        property int pcIndex : -1
        property string pcName : ""
        text: qsTr("Are you sure you want to remove '%1'?").arg(pcName)
        standardButtons: Dialog.Yes | Dialog.No

        onAccepted: {
            computerModel.deleteComputer(pcIndex)
        }
    }

    NavigableMessageDialog {
        id: testConnectionDialog
        closePolicy: Popup.CloseOnEscape
        standardButtons: Dialog.Ok

        onAboutToShow: {
            testConnectionDialog.text = qsTr("Moonlight is testing your network connection to determine if any required ports are blocked.") + "\n\n" + qsTr("This may take a few seconds…")
            showSpinner = true
        }

        function connectionTestComplete(result, blockedPorts)
        {
            if (result === -1) {
                text = qsTr("The network test could not be performed because none of Moonlight's connection testing servers were reachable from this PC. Check your Internet connection or try again later.")
                imageSrc = "qrc:/res/baseline-warning-24px.svg"
            }
            else if (result === 0) {
                text = qsTr("This network does not appear to be blocking Moonlight. If you still have trouble connecting, check your PC's firewall settings.") + "\n\n" + qsTr("If you are trying to stream over the Internet, install the Moonlight Internet Hosting Tool on your gaming PC and run the included Internet Streaming Tester to check your gaming PC's Internet connection.")
                imageSrc = "qrc:/res/baseline-check_circle_outline-24px.svg"
            }
            else {
                text = qsTr("Your PC's current network connection seems to be blocking Moonlight. Streaming over the Internet may not work while connected to this network.") + "\n\n" + qsTr("The following network ports were blocked:") + "\n"
                text += blockedPorts
                imageSrc = "qrc:/res/baseline-error_outline-24px.svg"
            }

            // Stop showing the spinner and show the image instead
            showSpinner = false
        }
    }

    NavigableDialog {
        id: renamePcDialog
        property string label: qsTr("Enter the new name for this PC:")
        property string originalName
        property int pcIndex : -1;

        standardButtons: Dialog.Ok | Dialog.Cancel

        onOpened: {
            // Force keyboard focus on the textbox so keyboard navigation works
            editText.forceActiveFocus()
        }

        onClosed: {
            editText.clear()
        }

        onAccepted: {
            if (editText.text) {
                computerModel.renameComputer(pcIndex, editText.text)
            }
        }

        ColumnLayout {
            Label {
                text: renamePcDialog.label
                font.bold: true
            }

            TextField {
                id: editText
                placeholderText: renamePcDialog.originalName
                Layout.fillWidth: true
                focus: true

                Keys.onReturnPressed: {
                    renamePcDialog.accept()
                }

                Keys.onEnterPressed: {
                    renamePcDialog.accept()
                }
            }
        }
    }

    NavigableMessageDialog {
        id: showPcDetailsDialog
        property string pcDetails : "";
        text: showPcDetailsDialog.pcDetails
        imageSrc: "qrc:/res/baseline-help_outline-24px.svg"
        standardButtons: Dialog.Ok
    }

    ScrollBar.vertical: ScrollBar {}
}
