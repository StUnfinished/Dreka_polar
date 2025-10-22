#include "poi_planner.h"
#include <QtMath>
#include <QPointF>

static constexpr double EARTH_RADIUS_M = 6378137.0;

static void latLonToXY(double lat0, double lon0, double lat, double lon, double &x, double &y)
{
    double dLat = qDegreesToRadians(lat - lat0);
    double dLon = qDegreesToRadians(lon - lon0);
    double latRad = qDegreesToRadians(lat0);
    y = dLat * EARTH_RADIUS_M;
    x = dLon * EARTH_RADIUS_M * qCos(latRad);
}

static void xyToLatLon(double lat0, double lon0, double x, double y, double &lat, double &lon)
{
    lat = lat0 + qRadiansToDegrees(y / EARTH_RADIUS_M);
    lon = lon0 + qRadiansToDegrees(x / (EARTH_RADIUS_M * qCos(qDegreesToRadians(lat0))));
}

namespace planner
{

QVariantList planPoiMission(const QVariantMap &params, const CameraModel &cameraModel)
{
    QVariantList result;
    if (!params.contains("poi")) return result;
    QVariantMap poi = params.value("poi").toMap();
    double centerLat = poi.value("latitude", poi.value("lat", 0.0)).toDouble();
    double centerLon = poi.value("longitude", poi.value("lon", 0.0)).toDouble();
    double groundAlt = poi.value("altitude", poi.value("alt", 0.0)).toDouble();

    double maxRadius = params.value("radius", 50.0).toDouble();
    if (maxRadius <= 0.0) return result;

    double frontOverlap = params.value("front_overlap", 70.0).toDouble() / 100.0;
    double sideOverlap = params.value("side_overlap", 70.0).toDouble() / 100.0;

    double altitudeM = params.value("altitude_m", 0.0).toDouble();
    double gsd_m = params.value("gsd_m", 0.0).toDouble();

    // compute altitude from gsd if needed (reuse logic from strip planner)
    if (altitudeM <= 0.0 && gsd_m > 0.0) {
        double focal_mm = cameraModel.focalLengthMm();
        double sensor_w_mm = cameraModel.sensorWidthMm();
        int img_w_px = cameraModel.imageWidthPx();
        altitudeM = gsd_m * focal_mm * double(img_w_px) / sensor_w_mm;
    }
    if (altitudeM <= 0.0) {
        altitudeM = params.value("default_altitude_m", 120.0).toDouble();
    }

    // determine gsd if not provided
    if (gsd_m <= 0.0) {
        double rx, ry;
        cameraModel.groundResolutionAtAltitude(altitudeM, rx, ry);
        gsd_m = rx;
    }

    // sensor footprint widths
    double focal_mm = cameraModel.focalLengthMm();
    double sensor_w_mm = cameraModel.sensorWidthMm();
    int img_w_px = cameraModel.imageWidthPx();
    int img_h_px = cameraModel.imageHeightPx();

    // ground swath width (meters) approximated by sensor_w_mm / focal_mm * altitude
    double swathWidth = (sensor_w_mm / focal_mm) * altitudeM;
    if (swathWidth <= 0.0) swathWidth = gsd_m * img_w_px;

    // lateral spacing between concentric rings to achieve side overlap
    double lateralStep = swathWidth * (1.0 - sideOverlap);
    if (lateralStep <= 0.0) lateralStep = swathWidth * 0.5;

    // decide rings: from small positive radius up to maxRadius
    QVector<double> ringRadii;
    if (maxRadius < lateralStep) {
        ringRadii.append(maxRadius);
    } else {
        // create rings at distances lateralStep, 2*lateralStep, ... <= maxRadius
        for (double r = lateralStep; r <= maxRadius + 1e-6; r += lateralStep) {
            ringRadii.append(r);
        }
    }

    // ensure at least one ring (fallback)
    if (ringRadii.isEmpty()) ringRadii.append(qMin(10.0, maxRadius));

    // along-track spacing derived from gsd and image height and front overlap
    double alongImageLength = gsd_m * double(img_h_px);
    double alongSpacing = alongImageLength * (1.0 - frontOverlap);
    if (alongSpacing <= 0.0) alongSpacing = gsd_m * 5.0;

    // compute for each ring a set of points (lat/lon/alt)
    for (int i = 0; i < ringRadii.size(); ++i) {
        double r = ringRadii[i];

        // Set the most number of waypoints of each ring to 12, min 6
        int steps = qMin(12, qMax(6, int(qCeil(2.0 * M_PI * r / (swathWidth * (1.0 - frontOverlap))))));
        double angStep = (2.0 * M_PI) / double(steps);

        for (int s = 0; s < steps; ++s) {
            double theta = s * angStep;
            double x = r * qCos(theta);
            double y = r * qSin(theta);

            double lat, lon;
            xyToLatLon(centerLat, centerLon, x, y, lat, lon);

            QVariantMap wp;
            wp["latitude"]  = lat;
            wp["longitude"] = lon;
            wp["altitude"]  = groundAlt + altitudeM;
            result.append(wp);
        }
    }

    // Optional :reverse the order of waypoints to have a different starting point
    // std::reverse(result.begin(), result.end());

    // for (double r : ringRadii) {
    //     // circumference
    //     double circ = 2.0 * M_PI * r;
    //     // angular step (radians)
    //     double angStep = (alongSpacing > 0.0 && r > 0.5) ? (alongSpacing / r) : (M_PI / 8.0);
    //     if (angStep <= 0.0) angStep = M_PI / 8.0;
    //     // ensure at least 12 points
    //     int steps = qMax(12, int(qCeil((2.0 * M_PI) / angStep)));
    //     double actualAng = (2.0 * M_PI) / double(steps);

    //     for (int s = 0; s < steps; ++s) {
    //         double theta = s * actualAng;
    //         double x = r * qCos(theta);
    //         double y = r * qSin(theta);
    //         double lat, lon;
    //         xyToLatLon(centerLat, centerLon, x, y, lat, lon);
    //         QVariantMap wp;
    //         wp["latitude"] = lat;
    //         wp["longitude"] = lon;
    //         wp["altitude"] = groundAlt + altitudeM;
    //         result.append(wp);
    //     }
    // }

    // optional: ensure start/end continuity by ordering rings outer-to-inner or vice versa
    // currently rings are outer-to-inner as constructed (small to large) — if prefer outer-first, reverse
    // no further deduplication here

    return result;
}

} // namespace planner