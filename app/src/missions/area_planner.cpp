#include "area_planner.h"
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

static QVector<QPointF> rotatePolygon(const QVector<QPointF> &poly, double angleRad)
{
    QVector<QPointF> out;
    double c = qCos(angleRad), s = qSin(angleRad);
    for (auto &p : poly) {
        double rx = p.x()*c - p.y()*s;
        double ry = p.x()*s + p.y()*c;
        out.append(QPointF(rx, ry));
    }
    return out;
}

static QVector<double> scanlineIntersections(const QVector<QPointF> &poly, double y)
{
    QVector<double> xs;
    int n = poly.size();
    for (int i = 0; i < n; ++i) {
        QPointF a = poly[i];
        QPointF b = poly[(i+1)%n];
        if ((a.y() <= y && b.y() > y) || (b.y() <= y && a.y() > y)) {
            double t = (y - a.y()) / (b.y() - a.y());
            double x = a.x() + t * (b.x() - a.x());
            xs.append(x);
        }
    }
    std::sort(xs.begin(), xs.end());
    return xs;
}

QVariantList planAreaMission(const QVariantMap &params, const CameraModel &cameraModel)
{
    QVariantList result;
    if (!params.contains("polygon")) return result;
    QVariantList polyIn = params.value("polygon").toList();
    if (polyIn.size() < 3) return result;

    double frontOverlap = params.value("front_overlap", 70.0).toDouble() / 100.0;
    double sideOverlap  = params.value("side_overlap", 60.0).toDouble() / 100.0;
    double headingDeg   = params.value("heading", 0.0).toDouble();
    double headingRad   = qDegreesToRadians(headingDeg);

    QVector<QPointF> polyLatLon;
    double sumAlt = 0.0;
    int altCount = 0;

    // ✅ 读取多边形顶点（经纬度 + 高程）
    for (auto v : polyIn) {
        QVariantMap m = v.toMap();
        double lat = m.value("latitude", m.value("lat", 0.0)).toDouble();
        double lon = m.value("longitude", m.value("lon", 0.0)).toDouble();
        double alt = m.value("altitude", m.value("alt", 0.0)).toDouble();
        polyLatLon.append(QPointF(lat, lon));
        if (alt != 0.0) {
            sumAlt += alt;
            altCount++;
        }
    }

    // ✅ 若多边形顶点包含高程，计算平均地表高度
    double avgGroundAlt = (altCount > 0) ? (sumAlt / altCount) : 0.0;

    double lat0 = polyLatLon[0].x();
    double lon0 = polyLatLon[0].y();

    QVector<QPointF> polyXY;
    for (auto &p : polyLatLon) {
        double x, y;
        latLonToXY(lat0, lon0, p.x(), p.y(), x, y);
        polyXY.append(QPointF(x, y));
    }

    double altitudeM = params.value("altitude_m", 0.0).toDouble();
    double gsd_m = params.value("gsd_m", 0.0).toDouble();
    if (altitudeM <= 0.0) {
        if (gsd_m > 0.0) {  // if GSD is given, calculate altitude
            double focal_mm = cameraModel.focalLengthMm();
            double sensor_w_mm = cameraModel.sensorWidthMm();
            int img_w_px = cameraModel.imageWidthPx();
            altitudeM = gsd_m * focal_mm * double(img_w_px) / sensor_w_mm;
        } else {
            altitudeM = params.value("default_altitude_m", 120.0).toDouble();  // if GSD not given, use default flight altitude 
        }
    }

    if (gsd_m <= 0.0) {  // is GSD not given, calculate GSD at default altitude
        double rx, ry;
        cameraModel.groundResolutionAtAltitude(altitudeM, rx, ry);
        gsd_m = rx;
    }

    double imageGroundWidth = gsd_m * double(cameraModel.imageWidthPx());
    double stripSpacing = imageGroundWidth * (1.0 - sideOverlap);
    if (stripSpacing <= 0.1) stripSpacing = imageGroundWidth * 0.2;

    // 坐标旋转
    QVector<QPointF> polyRot = rotatePolygon(polyXY, -headingRad);

    double minY = 1e18, maxY = -1e18;
    for (auto &p : polyRot) {
        minY = qMin(minY, double(p.y()));
        maxY = qMax(maxY, double(p.y()));
    }

    // 提取每条航线的起点和终点
    QVector<QPair<QPointF, QPointF>> linePairs;
    for (double y = minY; y <= maxY + 1e-6; y += stripSpacing) {
        QVector<double> xs = scanlineIntersections(polyRot, y);
        for (int i = 0; i + 1 < xs.size(); i += 2) {
            QPointF p1(xs[i], y);
            QPointF p2(xs[i + 1], y);
            linePairs.append(qMakePair(p1, p2));
        }
    }

    // 按顺序反转奇数条线（蛇形）
    for (int i = 0; i < linePairs.size(); ++i) {
        if (i % 2 == 1)
            std::swap(linePairs[i].first, linePairs[i].second);
    }

    // ✅ 计算最终航点（叠加地表平均高程）
    for (auto &line : linePairs) {
        QPointF p1 = line.first;
        QPointF p2 = line.second;
        double c = qCos(headingRad), s = qSin(headingRad);

        double xr1 = p1.x()*c - p1.y()*s;
        double yr1 = p1.x()*s + p1.y()*c;
        double xr2 = p2.x()*c - p2.y()*s;
        double yr2 = p2.x()*s + p2.y()*c;

        double lat1, lon1, lat2, lon2;
        xyToLatLon(lat0, lon0, xr1, yr1, lat1, lon1);
        xyToLatLon(lat0, lon0, xr2, yr2, lat2, lon2);

        double finalAlt = altitudeM + avgGroundAlt;

        QVariantMap wp1, wp2;
        wp1["latitude"] = lat1;
        wp1["longitude"] = lon1;
        wp1["altitude"] = finalAlt;
        wp2["latitude"] = lat2;
        wp2["longitude"] = lon2;
        wp2["altitude"] = finalAlt;

        result.append(wp1);
        result.append(wp2);
    }

    return result;
}


} // namespace planner