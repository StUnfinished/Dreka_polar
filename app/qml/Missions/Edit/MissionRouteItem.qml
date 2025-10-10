import QtQuick 2.12
import QtQuick.Layouts 1.12
import Industrial.Controls 1.0 as Controls
import Dreka.Missions 1.0

Item {
    id: root

    property var routeItem
    property int inRouteIndex

    signal selectRequest()

    implicitWidth: label.width + Controls.Theme.margins
    implicitHeight: Controls.Theme.baseSize

    // 鼠标悬停高亮
    Rectangle {
        id: hover
        anchors.fill: parent
        color: Controls.Theme.colors.highlight
        opacity: 0.20
        visible: mouseArea.containsMouse
    }

    // 选中高亮
    Controls.Label {
        id: label
        anchors.centerIn: parent
        font.pixelSize: Controls.Theme.auxFontSize
        text: routeItem.name + " " + (inRouteIndex != 0 ? inRouteIndex : "")
    }

    // 点击选中
    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        onClicked: selectRequest()
    }
}
