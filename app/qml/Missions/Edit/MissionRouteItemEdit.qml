import QtQuick 2.12
import QtQuick.Layouts 1.12
import Industrial.Controls 1.0 as Controls
import Dreka.Missions 1.0


Flickable {
    id: root

    property alias missionId: waypointEdit.missionId
    property alias inRouteIndex: waypointEdit.inRouteIndex
    readonly property alias routeItem: waypointEdit.routeItem
    readonly property bool exist: waypointEdit.exist

    flickableDirection: Flickable.VerticalFlick
    boundsBehavior: Flickable.StopAtBounds
    clip: true

    // contentHeight 动态绑定
    contentHeight: contentColumn.implicitHeight
    implicitHeight: contentColumn.implicitHeight

    Component.onCompleted: missions.selectedRouteItemIndex = Qt.binding(() => { return inRouteIndex; })
    Component.onDestruction: missions.selectedRouteItemIndex = -1

    // 父 ColumnLayout 包含两个子组件
    ColumnLayout {
        id: contentColumn
        width: parent.width
        spacing: 10

        // =========================
        // 1️⃣ 航点编辑部分
        // =========================
        WaypointEdit {
            id: waypointEdit
            Layout.fillWidth: true
        }

        // =========================
        // 2️⃣ 区域任务生成部分
        // =========================
        MissionPlanner {
            id: areaPlanner
            Layout.fillWidth: true
        }
    }
}
