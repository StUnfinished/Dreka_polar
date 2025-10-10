class CesiumWrapper {
    constructor(container) {
        // Your access token can be found at: https://cesium.com/ion/tokens.
        // Replace `your_access_token` with your Cesium ion access token.
        Cesium.Ion.defaultAccessToken = 'eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJqdGkiOiI3OGI3NGMwZi1mMjkwLTQyN2QtYWRiNS02YjIxZGI5MDFiNTAiLCJpZCI6MTk4NDM2LCJpYXQiOjE3NTgxNjE2MzR9.U9YY4NrCOCVOClLLvO0iMwWA71Q0W4R1GDd5MjqeFio';

        // Initialize the Cesium Viewer in the HTML element with the `cesiumContainer` ID.
        this.viewer = new Cesium.Viewer(container, {
            orderIndependentTranslucency: false,
            timeline: false,
            geocoder: false,
            selectionIndicator: false,
            infoBox: false,
            scene3DOnly: true,
            shouldAnimate: true,

            terrainProvider: Cesium.createWorldTerrain({
                requestVertexNormals: true,
                requestWaterMask: true
            })
        });

//        Alternate terrainProvider
//        this.viewer.terrainProvider = new Cesium.CesiumTerrainProvider({
//            url: 'https://api.maptiler.com/tiles/terrain-quantized-mesh/?key={key}' // get your own key at https://cloud.maptiler.com/
//        });

        // Disable depth test for showing entities under terrain
        this.viewer.scene.globe.depthTestAgainstTerrain = false;

        // Add Cesium OSM Buildings, a global 3D buildings layer.
        const buildingTileset = this.viewer.scene.primitives.add(Cesium.createOsmBuildings());
    }

    init() {
        var that = this;

        this.input = new Input(this.viewer);
        this.interaction = new Interaction(this.viewer, this.input);
        this.viewport = new Viewport(this.viewer);
        this.input.subscribe(InputTypes.ON_MOVE, (event, cartesian, modifier) => {
            return that.viewport.onMove(cartesian);
        });

        this.webChannel = new QWebChannel(qt.webChannelTransport, (channel) => {
            var rulerController = channel.objects.rulerController;
            if (rulerController) {
                const ruler = new Ruler(that.viewer, that.interaction);
                rulerController.cleared.connect(() => { ruler.clear(); });
                rulerController.rulerModeChanged.connect(mode => { ruler.setEnabled(mode) });
                ruler.subscribeDistance(distance => { rulerController.distance = distance; });
            }

            // TODO: grid and layers optional
            const gridView = new Grid(that.viewer, channel.objects.gridController);
            const layersView = new Layers(that.viewer, channel.objects.layersController);

            var viewportController = channel.objects.viewportController;
            if (viewportController) {
                viewportController.flyTo.connect((center, heading, pitch, duration) => {
                    that.viewport.flyTo(center, heading, pitch, duration);
                });

                viewportController.lookTo.connect((heading, pitch, duration) => {
                    that.viewport.lookTo(heading, pitch, duration);
                });

                that.viewport.subscribeCamera((heading, pitch, cameraPosition, centerPosition,
                                               pixelScale, changed) => {
                    viewportController.heading = heading;
                    viewportController.pitch = pitch;
                    viewportController.cameraPosition = cameraPosition;
                    viewportController.centerPosition = centerPosition;
                    viewportController.pixelScale = pixelScale;
                });

                that.viewport.subscribeCursor((cursorPosition) => {
                    viewportController.cursorPosition = cursorPosition;
                });
                viewportController.restore();
                that.viewport.tick();
            }

            // --- Route pattern / mission drawing helpers ---
            // Tools: Area (polygon/rectangle), Path (polyline), Path preview
            var routePatternController = channel.objects.routePatternController;
            var missionPlannerController = channel.objects.missionPlannerController; // optional
            var routePatternArea = new Area(that.viewer, that.interaction);
            var routePatternPolyline = new Path(that.viewer, Cesium.Color.CYAN);
            var routePatternPreview = new Path(that.viewer, Cesium.Color.GOLD);

            // When area tool changes (user drawing), push lat/lon/alt to registered controller
            routePatternArea.changedCallback = () => {
                var positions = [];
                routePatternArea.points.forEach(point => {
                    var cartographic = Cesium.Cartographic.fromCartesian(point.position);
                    positions.push({
                        latitude: Cesium.Math.toDegrees(cartographic.latitude),
                        longitude: Cesium.Math.toDegrees(cartographic.longitude),
                        altitude: cartographic.height
                    });
                });
                // Prefer routePatternController.setAreaPositions if available
                try {
                    if (routePatternController && routePatternController.setAreaPositions) {
                        routePatternController.setAreaPositions(positions);
                    } else if (missionPlannerController && missionPlannerController.onAreaDrawn) {
                        missionPlannerController.onAreaDrawn(JSON.stringify(positions));
                    } else if (channel && channel.objects && channel.objects.cesiumMap && channel.objects.cesiumMap.onAreaDrawnFromJs) {
                        // fallback: call cesiumMap bridge so CesiumMap.qml 发射 areaDrawn signal
                        channel.objects.cesiumMap.onAreaDrawnFromJs(JSON.stringify(positions));
                    }
                } catch (e) {
                    console.warn("Failed to call controller for area positions:", e);
                }
            };

            // If controller provides patternChanged/areaPositions hooks, wire them
            if (routePatternController) {
                try {
                    if (routePatternController.patternChanged) {
                        routePatternController.patternChanged.connect(() => {
                            routePatternArea.setEnabled(Boolean(routePatternController.pattern));
                            // populate existing area if backend has persisted positions
                            if (routePatternController.areaPositions) {
                                routePatternController.areaPositions(positions => {
                                    routePatternArea.setPositions(positions || []);
                                });
                            }
                        });
                    }
                    // preview path updates
                    if (routePatternController.pathPositionsChanged) {
                        routePatternController.pathPositionsChanged.connect(() => {
                            // try get pathPositions property or call function
                            var pathPositions = [];
                            try {
                                // if controller exposes method to get positions
                                if (routePatternController.pathPositions) {
                                    pathPositions = routePatternController.pathPositions;
                                } else if (routePatternController.pathPositionsArray) {
                                    routePatternController.pathPositionsArray(pos => { pathPositions = pos; });
                                }
                            } catch (e) { /* ignore */ }
                            routePatternPreview.setLatLngPositions(pathPositions || []);
                        });
                    }
                } catch (e) { console.warn("routePatternController hook wiring failed", e); }
            }

            // Input-based POI click handler (enabled/disabled via window.enablePoiClick)
            var poiClickEnabled = false;
            var poiClickSubscription = null;
            function enablePoiClickInternal(enable) {
                poiClickEnabled = !!enable;
                if (poiClickEnabled) {
                    if (poiClickSubscription === null) {
                        poiClickSubscription = that.input.subscribe(InputTypes.ON_CLICK, (event, cartesian, modifier) => {
                            if (!Cesium.defined(cartesian)) return false;
                            var cartographic = Cesium.Cartographic.fromCartesian(cartesian);
                            var pt = {
                                latitude: Cesium.Math.toDegrees(cartographic.latitude),
                                longitude: Cesium.Math.toDegrees(cartographic.longitude),
                                altitude: cartographic.height
                            };
                            try {
                                if (routePatternController && routePatternController.setPoi) {
                                    routePatternController.setPoi(pt);
                                } else if (routePatternController && routePatternController.setAreaPositions) {
                                    // fallback: send single-point array
                                    routePatternController.setAreaPositions([pt]);
                                } else if (missionPlannerController && missionPlannerController.onPoiClicked) {
                                    missionPlannerController.onPoiClicked(JSON.stringify(pt));
                                } else if (channel && channel.objects && channel.objects.cesiumMap && channel.objects.cesiumMap.onPoiClickedFromJs) {
                                    channel.objects.cesiumMap.onPoiClickedFromJs(JSON.stringify(pt));
                                }
                            } catch (e) {
                                console.warn("Failed to call controller for POI click:", e);
                            }
                            return true;
                        });
                    }
                } else {
                    if (poiClickSubscription !== null) {
                        that.input.unsubscribe(poiClickSubscription);
                        poiClickSubscription = null;
                    }
                }
            }

            // Expose JS functions for QML to call (via runJavaScript)
            // startRectangleDraw(enable) - toggles polygon/rectangle drawing tool
            window.startRectangleDraw = function(enable) {
                try {
                    if (!routePatternArea) return;
                    // try common API names safely
                    if (typeof routePatternArea.setRectangleMode === "function") {
                        routePatternArea.setRectangleMode(!!enable);
                    } else if (typeof routePatternArea.setMode === "function") {
                        // some implementations accept a mode string
                        if (enable) routePatternArea.setMode("rectangle"); else routePatternArea.setMode("none");
                    } else if (typeof routePatternArea.enable === "function") {
                        routePatternArea.enable(!!enable);
                    } else if (typeof routePatternArea.setEnabled === "function") {
                        routePatternArea.setEnabled(!!enable);
                    } else {
                        // best-effort fallback: try toggling visible/clearing
                        if (enable && typeof routePatternArea.show === "function") routePatternArea.show();
                        if (!enable && typeof routePatternArea.clear === "function") routePatternArea.clear();
                    }
                    if (!enable && typeof routePatternArea.clear === "function") routePatternArea.clear();
                } catch (e) {
                    console.warn("startRectangleDraw error:", e);
                }
            };
 
             // state for custom polyline draw
            var _polylineMode = false;
            var _polylineClickSub = null;
            var _polylineMoveSub = null;
            var _polylinePoints = [];
            var _polylineEntity = null;

            // startPolylineDraw(enable) - toggles polyline drawing (use internal click handling)
            window.startPolylineDraw = function(enable) {
                try {
                    // disable if already enabled
                    if (!enable) {
                        _polylineMode = false;
                        // unsubscribe click
                        if (_polylineClickSub !== null) {
                            that.input.unsubscribe(_polylineClickSub);
                            _polylineClickSub = null;
                        }
                        if (_polylineMoveSub !== null) {
                            that.input.unsubscribe(_polylineMoveSub);
                            _polylineMoveSub = null;
                        }
                        // finalize: send points to controller then clear preview
                        try {
                            var payload = _polylinePoints.map(p => ({ latitude: p.lat, longitude: p.lon, altitude: p.alt }));
                            if (routePatternController && routePatternController.setPolylinePositions) {
                                routePatternController.setPolylinePositions(payload);
                            } else if (missionPlannerController && missionPlannerController.onPolylineDrawn) {
                                missionPlannerController.onPolylineDrawn(JSON.stringify(payload));
                            } else if (channel && channel.objects && channel.objects.cesiumMap && channel.objects.cesiumMap.onPolylineDrawnFromJs) {
                                channel.objects.cesiumMap.onPolylineDrawnFromJs(JSON.stringify(payload));
                            }
                        } catch (e) { console.warn("finalize polyline callback failed", e); }
                        // clear temp
                        _polylinePoints = [];
                        if (_polylineEntity) {
                            that.viewer.entities.remove(_polylineEntity);
                            _polylineEntity = null;
                        }
                        return;
                    }

                    // enable drawing
                    _polylineMode = true;
                    _polylinePoints = [];

                    // create preview entity
                    if (!_polylineEntity) {
                        _polylineEntity = that.viewer.entities.add({
                            polyline: {
                                positions: new Cesium.CallbackProperty(function() {
                                    return (_polylinePoints.length > 0) ?
                                        Cesium.Cartesian3.fromDegreesArrayHeights(_polylinePoints.flatMap(p => [p.lon, p.lat, p.alt])) : [];
                                }, false),
                                width: 3,
                                material: Cesium.Color.CYAN.withAlpha(0.9),
                                clampToGround: false
                            }
                        });
                    }

                    // click subscription: add points
                    if (_polylineClickSub === null) {
                        _polylineClickSub = that.input.subscribe(InputTypes.ON_CLICK, (event, cartesian, modifier) => {
                            if (!Cesium.defined(cartesian)) return false;
                            var cart = Cesium.Cartographic.fromCartesian(cartesian);
                            var lat = Cesium.Math.toDegrees(cart.latitude);
                            var lon = Cesium.Math.toDegrees(cart.longitude);
                            var alt = cart.height || 0;
                            _polylinePoints.push({ lat: lat, lon: lon, alt: alt });
                            return true;
                        });
                    }

                    // optional move subscription to show preview point (no change to path, just UX)
                    if (_polylineMoveSub === null) {
                        _polylineMoveSub = that.input.subscribe(InputTypes.ON_MOVE, (event, cartesian, modifier) => {
                            // not strictly necessary; kept to update callback property if desired
                            return true;
                        });
                    }
                } catch (e) {
                    console.warn("startPolylineDraw error:", e);
                }
            };

            // setTopDownView(enabled) - adjust camera for orthographic/top-down look with safe altitude
            window.setTopDownView = function(enabled) {
                try {
                    var camera = that.viewer && that.viewer.camera;
                    var scene = that.viewer && that.viewer.scene;
                    if (!camera) {
                        console.warn("setTopDownView: no camera available");
                        return;
                    }

                    if (enabled) {
                        var centerLon = 0, centerLat = 0, targetHeight = 500;
                        var haveCenter = false;

                        // 1) 优先：如果有绘制区域，利用其 points 计算重心与包围半径，设置合适的高度
                        try {
                            if (typeof routePatternArea !== "undefined" && routePatternArea && Array.isArray(routePatternArea.points) && routePatternArea.points.length > 0) {
                                console.log("setTopDownView: using drawn area for centering");
                                var latSum = 0, lonSum = 0, altSum = 0, cnt = 0;
                                var cartesianPositions = [];
                                routePatternArea.points.forEach(pt => {
                                    try {
                                        if (!pt || !pt.position) return;
                                        cartesianPositions.push(pt.position);
                                        var c = Cesium.Cartographic.fromCartesian(pt.position);
                                        latSum += Cesium.Math.toDegrees(c.latitude);
                                        lonSum += Cesium.Math.toDegrees(c.longitude);
                                        altSum += c.height || 0;
                                        cnt++;
                                    } catch (ex) { /* skip invalid */ }
                                });
                                if (cnt > 0) {
                                    centerLat = latSum / cnt;
                                    centerLon = lonSum / cnt;
                                    var avgAlt = altSum / cnt;
                                    // bounding sphere radius (approx)
                                    var bs = Cesium.BoundingSphere.fromPoints(cartesianPositions);
                                    var radius = (bs && bs.radius) ? bs.radius : 0;
                                    // convert radius (meters) -> desired height (meters) using simple heuristic
                                    targetHeight = Math.max(200, radius * 2, 300);
                                    // avoid absurdly large altitude
                                    targetHeight = Math.min(targetHeight, 50000);
                                    haveCenter = true;
                                }
                            }
                        } catch (e) { /* ignore */ }

                        // 2) fallback: 屏幕中心投影到椭球，使用安全高度
                        if (!haveCenter) {
                            try {
                                var canvas = that.viewer.canvas;
                                if (scene && scene.globe && scene.globe.ellipsoid && canvas) {
                                    console.log("setTopDownView: using screen center for centering");
                                    var cx = (canvas.clientWidth || canvas.width) / 2;
                                    var cy = (canvas.clientHeight || canvas.height) / 2;
                                    var pick = camera.pickEllipsoid(new Cesium.Cartesian2(cx, cy), scene.globe.ellipsoid);
                                    if (Cesium.defined(pick)) {
                                        var carto = Cesium.Cartographic.fromCartesian(pick);
                                        centerLat = Cesium.Math.toDegrees(carto.latitude);
                                        centerLon = Cesium.Math.toDegrees(carto.longitude);
                                        targetHeight = Math.max(300, (carto.height || 0) + 300);
                                        haveCenter = true;
                                    }
                                }
                            } catch (e) { /* ignore */ }
                        }

                        // 3) 最后回退到全球某点（经度0纬度0）且较高视角(1000m)
                        if (!haveCenter) {
                            centerLat = 0; centerLon = 0; targetHeight = 1000;
                        }

                        var destination = Cesium.Cartesian3.fromDegrees(centerLon, centerLat, targetHeight);
                        camera.flyTo({
                            destination: destination,
                            orientation: {
                                pitch: Cesium.Math.toRadians(-90),
                                heading: 0,
                                roll: 0
                            },
                            duration: 0.6
                        });
                    } else {
                        camera.flyTo({
                            orientation: {
                                pitch: Cesium.Math.toRadians(-45),
                                heading: 0,
                                roll: 0
                            },
                            duration: 0.6
                        });
                    }
                } catch (e) {
                    console.warn("setTopDownView error:", e);
                }
            };
 
             // showPlannedRoute(waypoints) - display planned waypoints on map
             window.showPlannedRoute = function(waypoints) {
                 try {
                     // expect waypoints as array of { latitude, longitude, altitude } or objects with lat/lon
                     if (routePatternPreview && typeof routePatternPreview.clear === "function") routePatternPreview.clear();
                     if (routePatternPreview && typeof routePatternPreview.setLatLngPositions === "function") {
                         routePatternPreview.setLatLngPositions(waypoints || []);
                     } else if (routePatternPreview && typeof routePatternPreview.setPositions === "function") {
                         routePatternPreview.setPositions(waypoints || []);
                     } else {
                         // fallback: draw simple points as entities
                         var entities = that.viewer.entities;
                         if (entities && Array.isArray(waypoints)) {
                             // remove previous temporary ids
                             if (that._plannedEntities && that._plannedEntities.length) {
                                 that._plannedEntities.forEach(id => entities.removeById(id));
                             }
                             that._plannedEntities = [];
                             waypoints.forEach((wp, idx) => {
                                 var id = "planned_wp_" + Date.now() + "_" + idx;
                                 var ent = entities.add({
                                     id: id,
                                     position: Cesium.Cartesian3.fromDegrees(wp.longitude || wp.lon || 0, wp.latitude || wp.lat || 0, wp.altitude || wp.alt || 0),
                                     point: { pixelSize: 6, color: Cesium.Color.GOLD }
                                 });
                                 that._plannedEntities.push(id);
                             });
                         }
                     }
                 } catch (e) {
                     console.warn("showPlannedRoute error:", e);
                 }
             };
 
             // If controller wants to populate existing area on load
             if (routePatternController && routePatternController.areaPositions) {
                 try {
                     routePatternController.areaPositions(positions => {
                         if (positions && positions.length) routePatternArea.setPositions(positions);
                     });
                 } catch (e) { /* ignore */ }
             }

            // --- Missions / Vehicles wiring (existing code follows) ---
            var missionsMapController = channel.objects.missionsMapController;
            if (missionsMapController) {
                const routesView = new Routes(that.viewer, that.interaction);

                routesView.routeItemChangedCallback = (routeId, index, routeItemData) => {
                    missionsMapController.updateRouteItem(routeId, index, routeItemData);
                }

                missionsMapController.missions(missions => {
                    for (const mission of missions) {
                        missionsMapController.mission(mission.id, mission => { routesView.setRoute(mission.id, mission); });

                        missionsMapController.routeItems(mission.id, routeItems => {
                            for (var index = 0; index < routeItems.length; ++index) {
                                routesView.setRouteItem(mission.id, index, routeItems[index]);
                            }
                        });
                    }
                });

                missionsMapController.selectedMissionChanged.connect(missionId => { routesView.selectRoute(missionId); });
                routesView.selectRoute(missionsMapController.selectedMissionId);
                missionsMapController.highlightItem.connect(index => { routesView.highlightItem(index); });

                missionsMapController.missionAdded.connect(mission => { routesView.setRoute(mission.id, mission); });
                missionsMapController.missionChanged.connect(mission => { routesView.setRoute(mission.id, mission); });
                missionsMapController.missionRemoved.connect(missionId => { routesView.removeRoute(missionId); });

                missionsMapController.routeItemAdded.connect((routeId, index, data) => { routesView.setRouteItem(routeId, index, data); });
                missionsMapController.routeItemChanged.connect((routeId, index, data) => { routesView.setRouteItem(routeId, index, data); });
                missionsMapController.routeItemRemoved.connect((routeId, index) => { routesView.removeRouteItem(routeId, index); });

                missionsMapController.centerMission.connect(missionId => { routesView.centerRoute(missionId); });
                missionsMapController.centerRouteItem.connect((routeId, index) => { routesView.centerRouteItem(routeId, index); });

                var missionsMenuController = channel.objects.missionsMenuController;
                if (missionsMenuController) {
                    routesView.routeItemClickedCallback = (routeId, index, x, y) => {
                        missionsMenuController.invokeMenu(routeId, index, x, y);
                    }
                    that.viewport.subscribeCamera((heading, pitch, cameraPosition, centerPosition,
                        pixelScale, changed) => { if (changed) missionsMenuController.drop(); }
                    );
                }
            }

            var vehiclesMapController = channel.objects.vehiclesMapController;
            if (vehiclesMapController) {
                const vehiclesView = new Vehicles(that.viewer);

                vehiclesMapController.vehicles(vehicles => {
                    for (const vehicle of vehicles) {
                        vehiclesView.setVehicle(vehicle.id, vehicle);
                        vehiclesMapController.telemetry(vehicle.id, (telemetry) => {
                            vehiclesView.setTelemetry(vehicle.id, telemetry);
                        });
                    }
                });

                vehiclesMapController.vehicleAdded.connect(vehicle => { vehiclesView.setVehicle(vehicle.id, vehicle); });
                vehiclesMapController.vehicleChanged.connect(vehicle => { vehiclesView.setVehicle(vehicle.id, vehicle); });
                vehiclesMapController.vehicleRemoved.connect(vehicleId => { vehiclesView.removeVehicle(vehicleId); });
                vehiclesMapController.telemetryChanged.connect((vehicleId, data) => { vehiclesView.setTelemetry(vehicleId, data); });
                vehiclesMapController.trackingChanged.connect(() => { vehiclesView.setTracking(vehiclesMapController.tracking); });

                vehiclesMapController.selectedVehicleChanged.connect(vehicleId => { vehiclesView.selectVehicle(vehicleId); });
                vehiclesView.selectVehicle(vehiclesMapController.selectedVehicleId);

                vehiclesMapController.trackLengthChanged.connect(trackLength => { vehiclesView.setTrackLength(trackLength); });
                vehiclesView.setTrackLength(vehiclesMapController.trackLength);
            }

            var adsbController = channel.objects.adsbController;
            if (adsbController) {
                const adsb = new Adsb(that.viewer);
                adsbController.adsbChanged.connect((data) => { adsb.setData(data); });
            }
            var menuController = channel.objects.menuController;
            if (menuController) {
                that.input.subscribe(InputTypes.ON_CLICK, (event, cartesian, modifier) => {
                    if (Cesium.defined(modifier) || !Cesium.defined(cartesian))
                        return false;

                    var cartographic = Cesium.Cartographic.fromCartesian(cartesian);
                    var latitude = Cesium.Math.toDegrees(cartographic.latitude);
                    var longitude = Cesium.Math.toDegrees(cartographic.longitude);
                    var altitude = cartographic.height;
                    menuController.invoke(event.position.x, event.position.y,
                                          latitude, longitude, altitude);
                    return true;
                });
                that.viewport.subscribeCamera((heading, pitch, cameraPosition, centerPosition,
                                               pixelScale, changed) => {
                    if (changed)
                        menuController.drop();
                });
            }
        });
    }
}

const cesiumWrapper = new CesiumWrapper('cesiumContainer');

// Init data when terrainProvider get ready
var heightMaps = cesiumWrapper.viewer.terrainProvider;
var heightCheck = setInterval(function () {
    if (heightMaps.ready) {
        clearInterval(heightCheck);
        cesiumWrapper.init();
    }
}, 100);
