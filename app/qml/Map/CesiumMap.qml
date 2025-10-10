import QtQuick 2.12
import QtQuick.Layouts 1.12
import QtWebEngine 1.3
import QtWebChannel 1.0
import Industrial.Controls 1.0 as Controls
import Dreka 1.0

Item {
    id: root
    anchors.fill: parent

    Component.onCompleted: {
        try {
            if (webChannel) {
                webChannel.registerObject("cesiumMap", root);
                console.log("CesiumMap: registered 'cesiumMap' on webChannel");
            } else {
                console.warn("CesiumMap: webChannel not found on Component.onCompleted");
            }
        } catch (e) {
            console.warn("CesiumMap: failed to register cesiumMap on webChannel:", e);
        }
    }

    readonly property alias centerPosition: mapControl.centerPosition

    readonly property real controlHeight: mapControl.height + Controls.Theme.margins * 2

    function registerController(id, controller) {
        webChannel.registerObject(id, controller);
    }

    WebEngineView {
        id: webView
        anchors.fill: parent
        url: "file:" + applicationDirPath + "/web/index.html"
        webChannel: WebChannel { id: webChannel }
        onJavaScriptConsoleMessage: console.log(message)
    }

    // Signals emitted when user finishes drawing interaction in Cesium
    signal areaDrawn(var positions)      // polygon: [{latitude, longitude, altitude}, ...]
    signal polylineDrawn(var positions)  // polyline points
    signal poiClicked(var position)      // single point

    // API called from other QML to control map interaction
    function enableTopDownView(enable) {
        // ask Cesium to switch to top-down; CesiumWrapper.js should implement setTopDownView(enabled)
        if (webView) webView.runJavaScript("if(window.setTopDownView) { window.setTopDownView(" + (enable ? "true" : "false") + "); }");
    }

    function setCursorPlus(enable) {
        // 改为通过在页面内修改 CSS cursor 来控制鼠标样式（避免 QML WebEngineView 无 cursor 属性的问题）
        try {
            if (!webView) return;
            var cursorStyle = enable ? "crosshair" : "default";
            // runJavaScript 在 QtWebEngine 中异步执行；容错处理以防页面未就绪
            webView.runJavaScript("try { document.body.style.cursor = '" + cursorStyle + "'; } catch(e) {};");
        } catch (e) {
            console.warn("setCursorPlus error:", e);
        }
    }

    function enableRectangleDraw(enable) {
        // CesiumWrapper should provide startRectangleDraw(enable) and call back via window.bridgeAreaDrawn(...)
        if (webView) webView.runJavaScript("if(window.startRectangleDraw) { window.startRectangleDraw(" + (enable ? "true" : "false") + "); }");
    }

    function enablePolylineDraw(enable) {
        if (webView) webView.runJavaScript("if(window.startPolylineDraw) { window.startPolylineDraw(" + (enable ? "true" : "false") + "); }");
    }

    function enablePoiClick(enable) {
        if (webView) webView.runJavaScript("if(window.enablePoiClick) { window.enablePoiClick(" + (enable ? "true" : "false") + "); }");
    }

    function showPlannedRoute(waypoints) {
        if (webView) {
            var payload = JSON.stringify(waypoints)
            webView.runJavaScript("if(window.showPlannedRoute) { window.showPlannedRoute(" + payload + "); }");
        }
    }

    // bridge functions called from Cesium JS (CesiumWrapper) via Qt WebChannel
    // Cesium JS should call these by invoking the QWebChannel object's methods:
    // e.g. qtObject.invokeMethod('onAreaDrawnFromJs', JSON.stringify(points))
    function onAreaDrawnFromJs(json) {
        try {
            var pts = JSON.parse(json)
            root.areaDrawn(pts)
        } catch (e) {
            console.warn("onAreaDrawnFromJs parse failed", e)
        }
    }
    function onPolylineDrawnFromJs(json) {
        try {
            var pts = JSON.parse(json)
            root.polylineDrawn(pts)
        } catch (e) {
            console.warn("onPolylineDrawnFromJs parse failed", e)
        }
    }
    function onPoiClickedFromJs(json) {
        try {
            var pt = JSON.parse(json)
            root.poiClicked(pt)
        } catch (e) {
            console.warn("onPoiClickedFromJs parse failed", e)
        }
    }

    // expose convenience getters if needed by QML
    function getLastDrawnPolygon() { return null } // placeholder

    MapCross {
        anchors.centerIn: parent
        visible: mapControl.crossMode
    }

    MapControlView {
        id: mapControl
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.margins: Controls.Theme.margins
    }
}
