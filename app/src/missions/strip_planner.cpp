#include "strip_planner.h"
#include <QtMath>
#include <QPointF>
#include <algorithm>

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

QVariantList planStripMission(const QVariantMap &params, const CameraModel &cameraModel)
{
    QVariantList result;
    if (!params.contains("polyline")) return result;
    QVariantList polyIn = params.value("polyline").toList();
    if (polyIn.size() < 2) return result;

    // 参数：前向重叠仍然保留（但在本算法中不用于采样，只用于兼容）
    double frontOverlap = params.value("front_overlap", 70.0).toDouble() / 100.0;

    // 阈值：判断为“转折”的最小角度（度），默认为 5 度
    double turnThresholdDeg = params.value("turn_threshold_deg", 5.0).toDouble();
    double turnThresholdRad = qDegreesToRadians(turnThresholdDeg);

    QVector<QPointF> polyLatLon;
    double sumAlt = 0.0;
    int altCount = 0;

    for (auto v : polyIn) {
        QVariantMap m = v.toMap();
        double lat = m.value("latitude", m.value("lat", 0.0)).toDouble();
        double lon = m.value("longitude", m.value("lon", 0.0)).toDouble();
        double alt = m.value("altitude", m.value("alt", 0.0)).toDouble();
        polyLatLon.append(QPointF(lat, lon));
        if (m.contains("altitude") || m.contains("alt") ) {
            sumAlt += alt;
            altCount++;
        }
    }

    if (polyLatLon.size() < 2) return result;

    double avgGroundAlt = (altCount > 0) ? (sumAlt / altCount) : 0.0;

    // 基准经纬（用于平面投影）
    double lat0 = polyLatLon[0].x();
    double lon0 = polyLatLon[0].y();

    QVector<QPointF> polyXY;
    for (auto &p : polyLatLon) {
        double x, y;
        latLonToXY(lat0, lon0, p.x(), p.y(), x, y);
        polyXY.append(QPointF(x, y));
    }

    // 计算飞行高度（优先 altitude_m，若 <=0 则尝试用 GSD 计算）
    double altitudeM = params.value("altitude_m", 0.0).toDouble();
    double gsd_m = params.value("gsd_m", 0.0).toDouble();

    if (altitudeM <= 0.0) {
        if (gsd_m > 0.0) {
            double focal_mm = cameraModel.focalLengthMm();
            double sensor_w_mm = cameraModel.sensorWidthMm();
            int img_w_px = cameraModel.imageWidthPx();
            altitudeM = gsd_m * focal_mm * double(img_w_px) / sensor_w_mm;
        } else {
            altitudeM = params.value("default_altitude_m", 120.0).toDouble();
        }
    }

    if (gsd_m <= 0.0) {
        double rx, ry;
        cameraModel.groundResolutionAtAltitude(altitudeM, rx, ry);
        gsd_m = rx; // fallback 使用横向分辨率
    }

    // 我们只在转折点放置航点：包含起点与终点，及中间角度变化显著的点
    int n = polyXY.size();

    auto angleOfSegment = [&](int idx)->double {
        // 计算 idx -> idx+1 的朝向（弧度）
        if (idx < 0 || idx + 1 >= n) return 0.0;
        double dx = polyXY[idx+1].x() - polyXY[idx].x();
        double dy = polyXY[idx+1].y() - polyXY[idx].y();
        return qAtan2(dy, dx); // [-pi, pi]
    };

    // Helper: add waypoint at polyLatLon[i]
    auto addWaypointAtIndex = [&](int i) {
        double lat = polyLatLon[i].x();
        double lon = polyLatLon[i].y();
        double finalAlt = altitudeM + avgGroundAlt;
        QVariantMap wp;
        wp["latitude"] = lat;
        wp["longitude"] = lon;
        wp["altitude"] = finalAlt;
        result.append(wp);
    };

    // 始点始终加入
    addWaypointAtIndex(0);

    // 中间点：判断前后段角度差
    for (int i = 1; i + 1 < n; ++i) {
        double a1 = angleOfSegment(i - 1);
        double a2 = angleOfSegment(i);
        // 计算最小夹角差 absolute
        double diff = qAbs(a2 - a1);
        // 规范到 [0, pi]
        while (diff > M_PI) diff = qAbs(diff - 2.0 * M_PI);
        // 仅当夹角超过阈值时，保留该折点
        if (diff >= turnThresholdRad) {
            addWaypointAtIndex(i);
        }
    }

    // 终点加入（如果终点与最后保留点重复，可以考虑去重，但这里简单加入）
    if (n > 1) addWaypointAtIndex(n - 1);

    // 可选：去重（如果相邻点经纬非常接近），以防重复加入
    auto almostEqual = [](double a, double b, double eps = 1e-8) { return qAbs(a - b) < eps; };
    QVariantList cleaned;
    for (int i = 0; i < result.size(); ++i) {
        QVariantMap cur = result[i].toMap();
        if (!cleaned.isEmpty()) {
            QVariantMap prev = cleaned.last().toMap();
            if (almostEqual(prev["latitude"].toDouble(), cur["latitude"].toDouble(), 1e-7) &&
                almostEqual(prev["longitude"].toDouble(), cur["longitude"].toDouble(), 1e-7)) {
                continue; // 跳过重复点
            }
        }
        cleaned.append(cur);
    }

    return cleaned;
}
} // namespace planner