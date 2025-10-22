#pragma once
#include <QVariantList>
#include <QVariantMap>
#include "camera_model.h"

namespace planner
{
    // params should contain:
    //  - poi: QVariantMap { latitude, longitude, altitude } (altitude = ground height)
    //  - radius: maximum radius (meters)
    //  - gsd_m (optional) desired ground sampling distance (m/px)
    //  - altitude_m (optional) flight altitude above ground (m) - if absent computed from gsd_m and camera
    //  - front_overlap (0..100)
    //  - side_overlap (0..100)
    QVariantList planPoiMission(const QVariantMap &params, const CameraModel &cameraModel);
}