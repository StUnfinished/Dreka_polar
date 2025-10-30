#pragma once
#include <QVariantList>
#include <QVariantMap>
#include "camera_model.h"

namespace planner
{
    // params should contain:
    //  - polygon: QVariantList of { latitude, longitude, altitude }
    //  - gsd_m (optional) desired ground sampling distance (meters/pixel)
    //  - altitude_m (optional) flight altitude in meters; if absent, computed from gsd_m and camera
    //  - front_overlap (0..100)
    //  - side_overlap (0..100)
    //  - surrounding_type: "expansion" or "contraction"
    // cameraModel must be provided (loaded)
    QVariantList planSpiralMission(const QVariantMap &params, const CameraModel &cameraModel);
}