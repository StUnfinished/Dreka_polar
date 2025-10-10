import QtQuick 2.12
import QtQuick.Layouts 1.12
import Industrial.Controls 1.0 as Controls
import Dreka.Missions 1.0

// show on ROUTE tab
ColumnLayout {
    id: root

    property alias selectedMissionId: routeController.missionId // current mission
    readonly property alias count: routeController.count // waypoint count

    property int selectedIndex: -1 // current selected waypoint
    
    // selected waypoint global sync
    onSelectedIndexChanged: missions.selectedRouteItemIndex = selectedIndex
    Connections {
        target: missions
        onSelectItem: selectedIndex = inRouteIndex // waypoint index
    }

    // control mission waypoint
    MissionRouteController {
        id: routeController
        onSelectItem: {
            if (selectedIndex == index)
                selectedIndex = -1;
            selectedIndex = index;
        }
    }
    
    // waypoint edit bar
    RowLayout {
        spacing: 0

        // first button
        Controls.Button {
            tipText: qsTr("Left")
            iconSource: "qrc:/icons/left.svg"
            flat: true
            rightCropped: true
            enabled: selectedIndex > 0 // enable when waypoint count is more than 0
            onClicked: selectedIndex--
            onPressAndHold: selectedIndex = 0
        }

        // if waypoint is empty
        Controls.Label {
            text: qsTr("No route items")
            horizontalAlignment: Text.AlignHCenter
            visible: !count
            Layout.fillWidth: true
        }

        // waypoint list(include all route item)
        ListView {
            id: list
            visible: count
            boundsBehavior: Flickable.StopAtBounds 
            orientation: ListView.Horizontal // horizontal layout
            spacing: 1
            clip: true
            model: routeController.routeItems
            currentIndex: selectedIndex
            delegate: MissionRouteItem { // dispaly with MissionRouteItem
                anchors.verticalCenter: parent.verticalCenter
                routeItem: modelData
                inRouteIndex: index
                onSelectRequest: selectedIndex = index  // update selectedIndex
            }
            highlight: Item {
                //x: list.currentItem ? list.currentItem.x : 0
                width: list.currentItem ? list.currentItem.width : 0

                Rectangle {
                    width: parent.width
                    anchors.bottom: parent.bottom
                    height: Controls.Theme.underline
                    color: Controls.Theme.colors.highlight
                }
            }
            Layout.fillWidth: true
            Layout.fillHeight: true
        }

        // last button
        Controls.Button {
            tipText: qsTr("Right")
            iconSource: "qrc:/icons/right.svg"
            leftCropped: true
            flat: true
            enabled: selectedIndex + 1 < count
            onClicked: selectedIndex++
            onPressAndHold: selectedIndex = count - 1
        }
    }

    // content on waypoint edition, link to MissionRouteItemEdit.qml
    MissionRouteItemEdit {
        id: itemEditView
        missionId: routeController.missionId // current mission
        inRouteIndex: root.selectedIndex // current celected waypoint
        Layout.fillWidth: true
        Layout.fillHeight: true
    }
}
