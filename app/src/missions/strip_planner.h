#pragma once
#include <QVariantList>
#include <QVariantMap>
#include "camera_model.h"

namespace planner
{
    // params should contain:
    //  - polyline: QVariantList of { latitude, longitude, altitude }
    //  - gsd_m (optional) desired ground sampling distance (meters/pixel)
    //  - altitude_m (optional) flight altitude in meters; if absent, computed from gsd_m and camera
    //  - front_overlap (0..100)
    // cameraModel must be provided (loaded)
    QVariantList planStripMission(const QVariantMap &params, const CameraModel &cameraModel);
}
