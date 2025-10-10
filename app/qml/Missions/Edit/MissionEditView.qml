import QtQuick 2.12
import QtQuick.Layouts 1.12
import Industrial.Controls 1.0 as Controls
import Dreka.Missions 1.0

//Pane容器
Controls.Pane { 
    id: root

    property alias selectedMissionId : editController.missionId //define a property, for external access

    width: Controls.Theme.baseSize * 13

    MissionEditController { id: editController }   //core controller, control missions' status

    // main layout
    ColumnLayout {
        anchors.fill: parent
        spacing: Controls.Theme.spacing

        // first row on UI
        RowLayout {
            spacing: Controls.Theme.spacing

            Controls.Label {
                text: qsTr("Vehicle") + ":\t" + editController.vehicleName // vehicle name
            }

            Controls.Led { //vehicle online status
                color: editController.online ? Controls.Theme.colors.positive :
                                               Controls.Theme.colors.disabled
                Layout.alignment: Qt.AlignVCenter
            }

            // mission progress
            Item {
                visible: editController.operationProgress == -1 // if no mission, put a layout item
                Layout.fillWidth: true
            }

            // if mission, show a progress bar
            Controls.ProgressBar {
                id: progress
                visible: editController.operationProgress != -1
                flat: true
                from: 0
                to: 100
                value: editController.operationProgress
                implicitHeight: Controls.Theme.baseSize / 2
                Layout.fillWidth: true

                Controls.Button {
                    anchors.fill: parent
                    flat: true
                    tipText: qsTr("Cancel")
                    onClicked: editController.cancel()
                }
            }
            
            // missions control menubutton
            Controls.MenuButton {
                iconSource: "qrc:/icons/dots.svg"  // show '...'
                tipText: qsTr("Mission actions")
                flat: true
                leftCropped: true
                enabled: editController.online // enable only vehicle is online
                model: ListModel {
                    ListElement { text: qsTr("Upload"); property var action: () => { editController.upload() } }
                    ListElement { text: qsTr("Download"); property var action: () => { editController.download() } }
                    ListElement { text: qsTr("Clear"); property var action: () => { editController.clear() } }
                }
                onTriggered: modelData.action()
            }
        }

        // 2nd part on UI
        Controls.TabBar {
            id: tab
            flat: true
            Controls.TabButton { text: qsTr("Route"); flat: true }
            Controls.TabButton { text: qsTr("Fence"); flat: true; enabled: false }
            Controls.TabButton { text: qsTr("Rally"); flat: true; enabled: false }
            Layout.fillWidth: true
        }

        // content of tab
        StackLayout {
            currentIndex: tab.currentIndex

            MissionRouteView { // link to MissionRouteView.qml
                id: routeView
                selectedMissionId : editController.missionId
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
        }
    }
}
