#include "spiral_planner.h"
#include <QtMath>
#include <QPolygonF>
#include <QVector>
#include <QPointF>
#include <QLineF>
#include <QDebug>

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

// intersect lines (p1->p2) and (p3->p4) as infinite lines.
// returns pair (intersects, point)
static std::pair<bool, QPointF> intersectLines(const QPointF &p1, const QPointF &p2, const QPointF &p3, const QPointF &p4)
{
    double x1 = p1.x(), y1 = p1.y();
    double x2 = p2.x(), y2 = p2.y();
    double x3 = p3.x(), y3 = p3.y();
    double x4 = p4.x(), y4 = p4.y();

    double denom = (x1 - x2)*(y3 - y4) - (y1 - y2)*(x3 - x4);
    if (qFuzzyIsNull(denom)) return {false, QPointF()};

    double xi = ((x1*y2 - y1*x2)*(x3 - x4) - (x1 - x2)*(x3*y4 - y3*x4)) / denom;
    double yi = ((x1*y2 - y1*x2)*(y3 - y4) - (y1 - y2)*(x3*y4 - y3*x4)) / denom;
    return {true, QPointF(xi, yi)};
}

// offset polygon edges inward by distance (offset>0) using edge normals and line intersections
static QPolygonF offsetPolygonInward(const QPolygonF &poly, double offset)
{
    int n = poly.size();
    QPolygonF out;
    if (n < 3) return out;

    // compute centroid as reference for inward direction
    QPointF centroid(0,0);
    for (const QPointF &p : poly) centroid += p;
    centroid /= double(n);

    // shifted edges represented by two points each
    QVector<QLineF> shifted;
    shifted.reserve(n);
    for (int i = 0; i < n; ++i) {
        QPointF a = poly[i];
        QPointF b = poly[(i + 1) % n];
        QPointF mid = QPointF((a.x() + b.x())*0.5, (a.y() + b.y())*0.5);
        // edge vector
        double dx = b.x() - a.x();
        double dy = b.y() - a.y();
        // normal (perp)
        QPointF normal(-dy, dx);
        double len = qSqrt(normal.x()*normal.x() + normal.y()*normal.y());
        if (len <= 1e-9) {
            shifted.append(QLineF(a, b)); // degenerate
            continue;
        }
        QPointF nrm = normal / len;
        // determine sign: inward should point towards centroid
        QPointF test = mid + nrm;
        double dot = (centroid.x() - mid.x()) * nrm.x() + (centroid.y() - mid.y()) * nrm.y();
        double sign = (dot > 0) ? 1.0 : -1.0;
        QPointF shift = nrm * (offset * sign);
        shifted.append(QLineF(a + shift, b + shift));
    }

    // intersect adjacent shifted lines to form new vertices
    for (int i = 0; i < n; ++i) {
        QLineF l1 = shifted[(i + n - 1) % n];
        QLineF l2 = shifted[i];
        auto res = intersectLines(QPointF(l1.x1(), l1.y1()), QPointF(l1.x2(), l1.y2()),
                                  QPointF(l2.x1(), l2.y1()), QPointF(l2.x2(), l2.y2()));
        if (res.first) {
            out.append(res.second);
        } else {
            // fallback: move original vertex along bisector
            QPointF prev = poly[(i + n - 1) % n];
            QPointF cur = poly[i];
            QPointF next = poly[(i + 1) % n];
            QPointF v1 = (prev - cur);
            QPointF v2 = (next - cur);
            double l1 = qSqrt(v1.x()*v1.x() + v1.y()*v1.y());
            double l2 = qSqrt(v2.x()*v2.x() + v2.y()*v2.y());
            if (l1 < 1e-6 || l2 < 1e-6) { out.append(cur); continue; }
            QPointF nv1 = v1 / l1;
            QPointF nv2 = v2 / l2;
            QPointF bis = (nv1 + nv2);
            double lb = qSqrt(bis.x()*bis.x() + bis.y()*bis.y());
            if (lb < 1e-6) {
                // nearly colinear -> shift along normal of edge
                QPointF edge = next - cur;
                QPointF edgeN(-edge.y(), edge.x());
                double le = qSqrt(edgeN.x()*edgeN.x() + edgeN.y()*edgeN.y());
                if (le < 1e-6) { out.append(cur); continue; }
                QPointF inN = edgeN / le;
                QPointF mid = (cur + next) * 0.5;
                double dot = (centroid.x() - mid.x()) * inN.x() + (centroid.y() - mid.y()) * inN.y();
                double sign = (dot > 0) ? 1.0 : -1.0;
                out.append(cur + inN * (offset * sign));
            } else {
                bis /= lb;
                // move along bisector by offset / sin(theta/2) approx
                double cosHalf = (nv1.x()*bis.x() + nv1.y()*bis.y());
                double move = offset / qMax(1e-6, cosHalf);
                out.append(cur + bis * move);
            }
        }
    }

    // remove consecutive duplicates and ensure closed polygon without near-zero edges
    QPolygonF clean;
    for (const QPointF &p : out) {
        if (clean.isEmpty() || (qAbs(clean.last().x() - p.x()) > 1e-6 || qAbs(clean.last().y() - p.y()) > 1e-6))
            clean.append(p);
    }
    if (clean.size() >= 3) return clean;
    return QPolygonF();
}

// sample points along polygon edges with spacing (meters) in projected XY coordinates
static QVector<QPointF> samplePolygonPerimeter(const QPolygonF &poly, double spacing)
{
    QVector<QPointF> samples;
    int n = poly.size();
    if (n < 2) return samples;
    for (int i = 0; i < n; ++i) {
        QPointF a = poly[i];
        QPointF b = poly[(i + 1) % n];
        double dx = b.x() - a.x();
        double dy = b.y() - a.y();
        double segLen = qSqrt(dx*dx + dy*dy);
        if (segLen < 1e-6) continue;
        int steps = qMax(1, int(qCeil(segLen / spacing)));
        for (int s = 0; s < steps; ++s) {
            double t = double(s) / double(steps);
            samples.append(QPointF(a.x() + dx * t, a.y() + dy * t));
        }
    }
    return samples;
}


namespace planner
{

QVariantList planSpiralMission(const QVariantMap &params, const CameraModel &cameraModel)
{
    QVariantList result;
    if (!params.contains("polygon")) {
        qWarning() << "planSpiralMission: missing polygon parameter.";
        return result;
    }

    QVariantList polyIn = params.value("polygon").toList();
    if (polyIn.size() < 3) {
        qWarning() << "planSpiralMission: polygon has too few points.";
        return result;
    }

    // --- 1. 读取输入并计算平均地面高度 ---
    QVector<QPointF> polyLatLon;
    double sumAlt = 0.0;
    int altCount = 0;
    for (auto &v : polyIn) {
        QVariantMap m = v.toMap();
        double lat = m.value("latitude", 0.0).toDouble();
        double lon = m.value("longitude", 0.0).toDouble();
        double alt = m.value("altitude", 0.0).toDouble();
        polyLatLon.append(QPointF(lat, lon));
        if (!qFuzzyCompare(alt + 1.0, 1.0)) { sumAlt += alt; altCount++; }
    }
    double avgGroundAlt = (altCount > 0) ? sumAlt / altCount : 0.0;
    double lat0 = polyLatLon[0].x();
    double lon0 = polyLatLon[0].y();

    // --- 2. 投影到局部 XY 平面 ---
    QPolygonF polyXY;
    for (auto &p : polyLatLon) {
        double x, y;
        latLonToXY(lat0, lon0, p.x(), p.y(), x, y);
        polyXY.append(QPointF(x, y));
    }

    // --- 3. 参数提取 ---
    double gsd_m = params.value("gsd_m", 0.05).toDouble();
    double sideOverlap = params.value("side_overlap", 70.0).toDouble() / 100.0;
    double altitudeM = params.value("altitude_m", 0.0).toDouble();
    QString spiralDirection = params.value("spiral_direction", "inward").toString().toLower(); // inward/outward

    if (altitudeM <= 0.0 && gsd_m > 0.0)
        altitudeM = gsd_m * cameraModel.focalLengthMm() * double(cameraModel.imageWidthPx()) / cameraModel.sensorWidthMm();
    if (altitudeM <= 0.0)
        altitudeM = params.value("default_altitude_m", 120.0).toDouble();

    if (gsd_m <= 0.0) {
        double rx, ry;
        cameraModel.groundResolutionAtAltitude(altitudeM, rx, ry);
        gsd_m = rx;
    }

    // --- 4. 计算环间间距 ---
    double swathWidth = (cameraModel.sensorWidthMm() / cameraModel.focalLengthMm()) * altitudeM;
    if (swathWidth <= 0.0)
        swathWidth = gsd_m * cameraModel.imageWidthPx();
    double ringSpacing = swathWidth * (1.0 - sideOverlap);
    if (ringSpacing < 0.5)
        ringSpacing = swathWidth * 0.5;

    qDebug() << "[planSpiralMission] ringSpacing =" << ringSpacing;

    // --- 5. 生成多层环 ---
    QVector<QPolygonF> rings;
    QPolygonF current = polyXY;
    const int maxRings = 300;
    int ringCount = 0;
    double minAreaThreshold = qMax(1.0, ringSpacing * ringSpacing);

    while (current.size() >= 3 && ringCount < maxRings) {
        rings.append(current);
        QPolygonF next = offsetPolygonInward(current, ringSpacing);
        if (next.size() < 3) break;

        double areaCurrent = qAbs(current.boundingRect().width() * current.boundingRect().height());
        double areaNext = qAbs(next.boundingRect().width() * next.boundingRect().height());
        if (areaNext < minAreaThreshold) break;
        if (qFuzzyCompare(areaNext + 1.0, areaCurrent + 1.0)) break;

        current = next;
        ringCount++;
    }

    if (rings.isEmpty()) {
        qWarning() << "planSpiralMission: no rings generated.";
        return result;
    }

    // --- 6. 按方向排序 ---
    bool outward = (spiralDirection == "outward");
    QVector<QPolygonF> orderedRings = rings;
    if (outward)
        std::reverse(orderedRings.begin(), orderedRings.end());

   // --- 7. 构建简化版螺旋航线（每环仅保留 5 个关键点） ---
QVector<QPointF> stitchedXY;
QPointF lastXY(1e12, 1e12);
bool haveLast = false;

const int pointsPerRing = 5; // 每环保留的点数（含闭合点）

for (int ri = 0; ri < orderedRings.size(); ++ri) {
    QPolygonF ring = orderedRings[ri];
    if (ring.isEmpty()) continue;

    // 去除重复首尾点（若 polygon 自动闭合）
    if ((ring.first() - ring.last()).manhattanLength() < 1e-6)
        ring.removeLast();

    int totalPts = ring.size();
    if (totalPts < pointsPerRing) {
        // 若点数太少直接保留全部顶点
        for (const QPointF &p : ring)
            stitchedXY.append(p);
        continue;
    }

    // === 保留点选取规则 ===
    QVector<int> selectedIdx;
    selectedIdx.append(0); // 起点 A1
    selectedIdx.append(totalPts / 5);  // 约 20% 位置
    selectedIdx.append(2 * totalPts / 5);
    selectedIdx.append(3 * totalPts / 5);
    selectedIdx.append(totalPts - 1);  // 环终点（接近起点）

    QVector<QPointF> foldPts;
    for (int idx : selectedIdx)
        foldPts.append(ring[idx]);

    // === 确保起点/终点一致方向 ===
    // 若不是第一个环，则将当前环旋转，使其起点最靠近上一环末点
    if (haveLast && !foldPts.isEmpty()) {
        int bestIdx = 0;
        double bestDist2 = 1e18;
        for (int i = 0; i < foldPts.size(); ++i) {
            double dx = foldPts[i].x() - lastXY.x();
            double dy = foldPts[i].y() - lastXY.y();
            double d2 = dx*dx + dy*dy;
            if (d2 < bestDist2) { bestDist2 = d2; bestIdx = i; }
        }

        QVector<QPointF> rotated;
        for (int k = 0; k < foldPts.size(); ++k)
            rotated.append(foldPts[(bestIdx + k) % foldPts.size()]);
        foldPts = rotated;
    }

    // 偶数环反转（蛇形连续连接）
    if (ri % 2 == 1)
        std::reverse(foldPts.begin(), foldPts.end());

    // 添加该环到总路径
    for (const QPointF &p : foldPts) {
        stitchedXY.append(p);
        lastXY = p;
        haveLast = true;
    }
}


    qDebug() << "[planSpiralMission] fold waypoints count:" << stitchedXY.size();

    // --- 8. 输出最终航点 ---
    for (const QPointF &xy : stitchedXY) {
        double lat, lon;
        xyToLatLon(lat0, lon0, xy.x(), xy.y(), lat, lon);
        QVariantMap wp;
        wp["latitude"] = lat;
        wp["longitude"] = lon;
        wp["altitude"] = avgGroundAlt + altitudeM;
        result.append(wp);
    }

    return result;
}




} // namespace planner