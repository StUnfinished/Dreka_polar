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
    for (auto v : polyIn) {
        QVariantMap m = v.toMap();
        double lat = m.value("latitude", m.value("lat", 0.0)).toDouble();
        double lon = m.value("longitude", m.value("lon", 0.0)).toDouble();
        polyLatLon.append(QPointF(lat, lon));
    }

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
        gsd_m = rx;
    }

    double imageGroundWidth = gsd_m * double(cameraModel.imageWidthPx());
    double imageGroundHeight = gsd_m * double(cameraModel.imageHeightPx());

    double stripSpacing = imageGroundWidth * (1.0 - sideOverlap);
    double alongSpacing = imageGroundHeight * (1.0 - frontOverlap);

    if (stripSpacing <= 0.1) stripSpacing = imageGroundWidth * 0.2;
    if (alongSpacing <= 0.1) alongSpacing = imageGroundHeight * 0.2;

    QVector<QPointF> polyRot = rotatePolygon(polyXY, -headingRad);

    double minX = 1e18, maxX = -1e18, minY = 1e18, maxY = -1e18;
    for (auto &p : polyRot) {
        minX = qMin(minX, double(p.x()));
        maxX = qMax(maxX, double(p.x()));
        minY = qMin(minY, double(p.y()));
        maxY = qMax(maxY, double(p.y()));
    }

    QVector<QPointF> waypointsXY;
    for (double y = minY; y <= maxY + 1e-6; y += stripSpacing) {
        QVector<double> xs = scanlineIntersections(polyRot, y);
        for (int i = 0; i+1 < xs.size(); i += 2) {
            double x1 = xs[i], x2 = xs[i+1];
            double segLen = qAbs(x2 - x1);
            if (segLen < 1e-6) continue;
            int nSamples = qMax(1, int(qCeil(segLen / alongSpacing)));
            for (int s = 0; s <= nSamples; ++s) {
                double t = double(s) / double(nSamples);
                double x = x1 + t * (x2 - x1);
                double yy = y;
                waypointsXY.append(QPointF(x, yy));
            }
        }
    }

    const double tol = stripSpacing * 0.001 + 0.0001;
    QVector<double> ys;
    for (auto &p : waypointsXY) ys.append(p.y());
    std::sort(ys.begin(), ys.end());
    QVector<double> uniqY;
    for (double y : ys) {
        if (uniqY.isEmpty() || qAbs(y - uniqY.last()) > tol) uniqY.append(y);
    }
    QVector<QVector<QPointF>> lines;
    lines.resize(uniqY.size());
    for (auto &p : waypointsXY) {
        int best = 0; double bestd = qAbs(p.y() - uniqY[0]);
        for (int i=1;i<uniqY.size();++i){
            double d=qAbs(p.y()-uniqY[i]);
            if (d<bestd){bestd=d;best=i;}
        }
        lines[best].append(p);
    }

    QVector<QPointF> orderedXY;
    for (int i = 0; i < lines.size(); ++i) {
        auto &ln = lines[i];
        std::sort(ln.begin(), ln.end(), [](const QPointF &a, const QPointF &b){ return a.x() < b.x(); });
        if (i % 2 == 1) std::reverse(ln.begin(), ln.end());
        for (auto &p : ln) orderedXY.append(p);
    }

    for (auto &p : orderedXY) {
        double c = qCos(headingRad), s = qSin(headingRad);
        double xr = p.x()*c - p.y()*s;
        double yr = p.x()*s + p.y()*c;
        double lat, lon;
        xyToLatLon(lat0, lon0, xr, yr, lat, lon);
        QVariantMap wp;
        wp["latitude"] = lat;
        wp["longitude"] = lon;
        wp["altitude"] = altitudeM;
        result.append(wp);
    }

    return result;
}
} // namespace planner