import QtQuick 2.12
import QtQuick.Layouts 1.12
import Industrial.Controls 1.0 as Controls
import Dreka.Missions 1.0

ColumnLayout {
    id: plannerRoot
    width: parent.width
    spacing: 8

    // 注册一个后端控制器供 Cesium/JS 使用（用于 webChannel 回调）
    // 使用已有的 MissionPatternController（项目中已在其它 QML 中使用）
    MissionPatternController { id: missionPlannerController }

    Component.onCompleted: {
        console.log("MissionPlanner Component.onCompleted, initial mapObj:", plannerRoot.mapObj)

        // 尝试在父对象树中查找 map / CesiumMap 对象（回退方案）
        function findMapObject(start) {
            var cur = start
            var depth = 0
            while (cur && depth < 20) {
                // 判断候选对象是否看起来像 Map/CesiumMap（有 registerController 或 showPlannedRoute 等方法）
                try {
                    if (typeof cur.registerController === "function"
                        || typeof cur.showPlannedRoute === "function"
                        || typeof cur.enableRectangleDraw === "function"
                        || typeof cur.enablePolylineDraw === "function") {
                        return cur
                    }
                } catch (e) { /* ignore */ }
                cur = cur.parent
                depth++
            }
            return null
        }

        // 如果当前 mapObj 无效，尝试查找并赋值
        if (!plannerRoot.mapObj) {
            var found = findMapObject(plannerRoot)
            if (found) {
                plannerRoot.mapObj = found
                console.log("MissionPlanner: found mapObj by traversal:", plannerRoot.mapObj)
            } else {
                console.warn("MissionPlanner: mapObj not found by traversal")
            }
        } else {
            // 如果已存在，尝试再次确认 registerController 可用
            console.log("MissionPlanner: existing mapObj methods:",
                        typeof plannerRoot.mapObj.registerController,
                        typeof plannerRoot.mapObj.enableRectangleDraw,
                        typeof plannerRoot.mapObj.showPlannedRoute)
        }

        // 尝试注册 controller 到 map 的 webChannel（如果 registerController 可用）
        try {
            if (plannerRoot.mapObj && typeof plannerRoot.mapObj.registerController === "function") {
                plannerRoot.mapObj.registerController("missionPlannerController", missionPlannerController)
                console.log("Registered missionPlannerController on mapObj webChannel")
            } else {
                console.warn("mapObj.registerController not available yet; mapObj:", plannerRoot.mapObj)
            }
        } catch (e) {
            console.warn("registerController error:", e)
        }
    }

    // 当前选择的任务类型，默认预设内容
    property string selectedMissionType: qsTr("Flight Route Planning")

    // hold last drawn geometry
    property var drawnPolygon: []
    property var drawnPolyline: []
    property var drawnPoi: null

    // reference to map object in parent scope (尝试兼容不同命名)
    property var mapObj: (typeof map !== "undefined" ? map : (typeof mapView !== "undefined" ? mapView : null))

    // 顶部下拉框，选择测图类型
    Controls.ComboBox {
        id: missionTypeBox
        model: [qsTr("Flight Route Planning"), qsTr("Area Mapping"), qsTr("Strip Mapping"), qsTr("POI Mapping")]
        currentIndex: 0   // 默认选中第一个
        Layout.fillWidth: true

        onActivated: {
            plannerRoot.selectedMissionType = model[index]
            console.log("Selected Model:", plannerRoot.selectedMissionType)

            // 每次切换时，先清理所有绘制模式
            if (plannerRoot.mapObj) {
                if (plannerRoot.mapObj.enableRectangleDraw) plannerRoot.mapObj.enableRectangleDraw(false)
                if (plannerRoot.mapObj.enablePolylineDraw) plannerRoot.mapObj.enablePolylineDraw(false)
                if (plannerRoot.mapObj.enablePoiClick) plannerRoot.mapObj.enablePoiClick(false)
            }

            // 根据任务类型启用绘制工具
            if (plannerRoot.selectedMissionType === qsTr("Area Mapping")) {
                if (plannerRoot.mapObj) {
                    plannerRoot.mapObj.enableTopDownView(true)
                    plannerRoot.mapObj.setCursorPlus(true)
                    plannerRoot.mapObj.enableRectangleDraw(true)
                }
            } else if (plannerRoot.selectedMissionType === qsTr("Strip Mapping")) {
                if (plannerRoot.mapObj) {
                    plannerRoot.mapObj.enableTopDownView(true)
                    plannerRoot.mapObj.setCursorPlus(true)
                    plannerRoot.mapObj.enablePolylineDraw(true)
                }
            } else if (plannerRoot.selectedMissionType === qsTr("POI Mapping")) {
                if (plannerRoot.mapObj) {
                    plannerRoot.mapObj.enableTopDownView(true)
                    plannerRoot.mapObj.setCursorPlus(true)
                    plannerRoot.mapObj.enablePoiClick(true)
                }
            } else {
                // Flight Route Planning 或其它情况 → 恢复默认
                if (plannerRoot.mapObj) {
                    plannerRoot.mapObj.enableTopDownView(false)
                    plannerRoot.mapObj.setCursorPlus(false)
                }
            }
        }
    }

    // ================= Area Mapping =================
    ColumnLayout {
        visible: plannerRoot.selectedMissionType === qsTr("Area Mapping")
        spacing: 4

        Controls.TextField { id: gsdField1; placeholderText: "GSD (m)"; text: "0.05" }

        RowLayout {
            Controls.SpinBox { id: headingField1; from: 0; to: 360; value: 0; Layout.preferredWidth: 80 }
            Controls.Label { text: "°" }
        }

        RowLayout {
            Controls.SpinBox { id: frontOverlap1; from: 0; to: 100; value: 70; Layout.preferredWidth: 80 }
            Controls.Label { text: "%" }
        }

        RowLayout {
            Controls.SpinBox { id: sideOverlap1; from: 0; to: 100; value: 60; Layout.preferredWidth: 80 }
            Controls.Label { text: "%" }
        }
    }

    // ================= Strip Mapping =================
    ColumnLayout {
        visible: plannerRoot.selectedMissionType === qsTr("Strip Mapping")
        spacing: 4

        Controls.TextField { id: gsdField2; placeholderText: "GSD (m)"; text: "0.05" }

        RowLayout {
            Controls.SpinBox { id: frontOverlap2; from: 0; to: 100; value: 75; Layout.preferredWidth: 80 }
            Controls.Label { text: "%" }
        }
    }

    // ================= POI Mapping =================
    ColumnLayout {
        visible: plannerRoot.selectedMissionType === qsTr("POI Mapping")
        spacing: 4

        Controls.TextField { id: gsdField3; placeholderText: "GSD (m)"; text: "0.05" }

        RowLayout {
            Controls.SpinBox { id: radiusField; from: 10; to: 1000; value: 50; Layout.preferredWidth: 80 }
            Controls.Label { text: "m" }
        }

        RowLayout {
            Controls.SpinBox { id: frontOverlap3; from: 0; to: 100; value: 80; Layout.preferredWidth: 80 }
            Controls.Label { text: "%" }
        }

        RowLayout {
            Controls.SpinBox { id: sideOverlap3; from: 0; to: 100; value: 70; Layout.preferredWidth: 80 }
            Controls.Label { text: "%" }
        }
    }

    // ================= 生成按钮 =================
    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        Controls.Button {
            text: qsTr("Generate Route Mission")
            Layout.fillWidth: true
            onClicked: {
                var params = {}

                if (plannerRoot.selectedMissionType === qsTr("Area Mapping")) {
                    params = {
                        gsd_m: parseFloat(gsdField1.text),
                        heading: headingField1.value,
                        front_overlap: frontOverlap1.value,
                        side_overlap: sideOverlap1.value,
                        pattern: "area",
                        polygon: plannerRoot.drawnPolygon
                    }
                } else if (plannerRoot.selectedMissionType === qsTr("Strip Mapping")) {
                    params = {
                        gsd_m: parseFloat(gsdField2.text),
                        front_overlap: frontOverlap2.value,
                        pattern: "strip",
                        polyline: plannerRoot.drawnPolyline
                    }
                } else if (plannerRoot.selectedMissionType === qsTr("POI Mapping")) {
                    params = {
                        gsd_m: parseFloat(gsdField3.text),
                        radius: radiusField.value,
                        front_overlap: frontOverlap3.value,
                        side_overlap: sideOverlap3.value,
                        pattern: "poi",
                        poi: plannerRoot.drawnPoi
                    }
                }

                // 新增：在 QML 控制台打印要发送的参数（便于调试）
                try {
                    console.log("Generate Route Mission - params:", JSON.stringify(params))
                } catch (e) {
                    console.log("Generate Route Mission - params (non-serializable)", params)
                }

                // 附加 missionId 到 params，优先使用 missionsMapController.selectedMissionId
                try {
                    var mid = null
                    if (typeof missionsMapController !== "undefined") {
                        mid = missionsMapController.selectedMissionId
                        console.log("Using missionsMapController.selectedMissionId for missionId:", mid)
                    }
                    if (!mid && typeof missionPlannerController !== "undefined") {
                        mid = missionPlannerController.missionId
                        console.log("Fallback to missionPlannerController.missionId:", mid)
                    }
                    if (mid) params.missionId = mid
                    else console.warn("No missionId available when calling generateAreaMission (will attempt backend fallback)")
                } catch (e) {
                    console.warn("Failed to detect missionId for generateAreaMission:", e)
                }

                // 调用后端 controller（如果存在）
                if (typeof missionPlannerController !== "undefined") {
                    // 打印调用动作
                    console.log("Calling missionPlannerController.generateAreaMission(...)")
                    missionPlannerController.generateAreaMission(params)
                } else {
                    console.warn("missionPlannerController not found")
                }
            }
        }

        // Controls.Button {
        //     text: qsTr("Clear Drawings")
        //     Layout.preferredWidth: 100
        //     onClicked: plannerRoot.clearDrawings()
        // }
        Controls.Button {
            text: qsTr("Exit Draw Mode")
            Layout.preferredWidth: 100
            onClicked: plannerRoot.closeDrawMode()
        }

        // Controls.Button {
        //     // 开发调试按钮：向 mapObj 发起模拟 areaDrawn 回调（仅在开发时使用）
        //     visible: true // 上线时改为 false 或移除
        //     text: "DBG Emit Area"
        //     Layout.preferredWidth: 110
        //     onClicked: {
        //         var sample = [
        //             { latitude: 37.7749, longitude: -122.4194, altitude: 10 },
        //             { latitude: 37.7759, longitude: -122.4194, altitude: 10 },
        //             { latitude: 37.7759, longitude: -122.4184, altitude: 10 }
        //         ]
        //         console.log("DBG: emitting sample area to mapObj (or calling onAreaDrawnFromJs)...", sample)
        //         try {
        //             // 优先尝试通过 mapObj 的 QML 桥接函数
        //             if (plannerRoot.mapObj && typeof plannerRoot.mapObj.onAreaDrawnFromJs === "function") {
        //                 plannerRoot.mapObj.onAreaDrawnFromJs(JSON.stringify(sample))
        //                 console.log("DBG: called mapObj.onAreaDrawnFromJs")
        //             } else if (typeof mapView !== "undefined" && mapView.runJavaScript) {
        //                 // fallback: 直接调用页面 JS 的桥接函数（如果存在）
        //                 mapView.runJavaScript("if(window.channel && channel.objects && channel.objects.cesiumMap && channel.objects.cesiumMap.onAreaDrawnFromJs) channel.objects.cesiumMap.onAreaDrawnFromJs('" + JSON.stringify(sample) + "');")
        //                 console.log("DBG: attempted mapView.runJavaScript fallback")
        //             } else {
        //                 console.warn("DBG: no available path to emit sample area")
        //             }
        //         } catch (e) {
        //             console.warn("DBG emit error:", e)
        //         }
        //     }
        // }
    }

    // ================= Map interaction callbacks =================
    // 监听 mapObj 的自定义信号（需要在 Map/CesiumMap.qml 中实现这些信号）
    Connections {
        target: plannerRoot.mapObj

        // polygon: [{latitude, longitude, altitude}, ...]
        onAreaDrawn: function(positions) {
            try {
                plannerRoot.drawnPolygon = positions ? positions : []
                console.log("Area drawn, points:", plannerRoot.drawnPolygon.length, plannerRoot.drawnPolygon)
                // 如果需要也可以直接将位置设置到后端 controller（可选）
                try { missionPlannerController.setAreaPositions(plannerRoot.drawnPolygon) } catch (e) {}
            } catch (e) {
                console.warn("onAreaDrawn handler error:", e)
            }
        }

        onPolylineDrawn: function(positions) {
            try {
                plannerRoot.drawnPolyline = positions ? positions : []
                console.log("Polyline drawn, points:", plannerRoot.drawnPolyline.length, plannerRoot.drawnPolyline)
            } catch (e) {
                console.warn("onPolylineDrawn handler error:", e)
            }
        }

        onPoiClicked: function(position) {
            try {
                plannerRoot.drawnPoi = position ? position : null
                console.log("POI clicked:", plannerRoot.drawnPoi)
            } catch (e) {
                console.warn("onPoiClicked handler error:", e)
            }
        }
    }

    // ================= 结果回调 =================
    Connections {
        target: missionPlannerController
        function onAreaMissionGenerated(waypoints, summary) {
            console.log("Waypoints generated:", waypoints.length, summary)
            if (typeof missionRouteController !== "undefined") {
                missionRouteController.setRouteItems(waypoints)
            }
            if (plannerRoot.mapObj && plannerRoot.mapObj.showPlannedRoute) {
                plannerRoot.mapObj.showPlannedRoute(waypoints)
            } else if (typeof mapView !== "undefined") {
                mapView.runJavaScript("showPlannedRoute(" + JSON.stringify(waypoints) + ")")
            }
        }
        function onAreaMissionFailed(reason) {
            console.warn("Plan failed:", reason)
        }
    }

    // function clearDrawings() {
    //     // 清理本地保存
    //     plannerRoot.drawnPolygon = []
    //     plannerRoot.drawnPolyline = []
    //     plannerRoot.drawnPoi = null

    //     // 关闭地图上的绘制模式并恢复光标/视角
    //     if (plannerRoot.mapObj) {
    //         try {
    //             // if (plannerRoot.mapObj.enableRectangleDraw) plannerRoot.mapObj.enableRectangleDraw(false)
    //             // if (plannerRoot.mapObj.enablePolylineDraw) plannerRoot.mapObj.enablePolylineDraw(false)
    //             // if (plannerRoot.mapObj.enablePoiClick) plannerRoot.mapObj.enablePoiClick(false)
    //             // if (plannerRoot.mapObj.setCursorPlus) plannerRoot.mapObj.setCursorPlus(false)
    //             // if (plannerRoot.mapObj.enableTopDownView) plannerRoot.mapObj.enableTopDownView(false)
    //             // 如果地图支持清除规划显示，调用清除（传空数组）
    //             if (plannerRoot.mapObj.showPlannedRoute) plannerRoot.mapObj.showPlannedRoute([])
    //             else if (typeof mapView !== "undefined") mapView.runJavaScript("showPlannedRoute([])")
    //         } catch (e) { console.warn("clearDrawings error:", e) }
    //     }
    // }

    function closeDrawMode() {
        // 关闭绘制模式但保留已绘制内容
        if (plannerRoot.mapObj) {
            try {
                if (plannerRoot.mapObj.enableRectangleDraw) plannerRoot.mapObj.enableRectangleDraw(false)
                if (plannerRoot.mapObj.enablePolylineDraw) plannerRoot.mapObj.enablePolylineDraw(false)
                if (plannerRoot.mapObj.enablePoiClick) plannerRoot.mapObj.enablePoiClick(false)
                if (plannerRoot.mapObj.setCursorPlus) plannerRoot.mapObj.setCursorPlus(false)
                if (plannerRoot.mapObj.enableTopDownView) plannerRoot.mapObj.enableTopDownView(false)
            } catch (e) { console.warn("closeDrawMode error:", e) }
        }
    }
}