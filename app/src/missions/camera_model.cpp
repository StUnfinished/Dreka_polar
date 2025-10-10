#include "camera_model.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

CameraModel::CameraModel() {}

bool CameraModel::loadFromFile(const QString &filePath)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QByteArray b = f.readAll();
    QJsonParseError err;
    QJsonDocument d = QJsonDocument::fromJson(b, &err);
    if (err.error != QJsonParseError::NoError) return false;
    if (!d.isObject()) return false;
    QJsonObject o = d.object();
    QVariantMap m = o.toVariantMap();
    return loadFromMap(m);
}

bool CameraModel::loadFromMap(const QVariantMap &map)
{
    bool ok = true;
    if (map.contains("focal_length_mm")) m_focalLengthMm = map.value("focal_length_mm").toDouble(&ok);
    if (map.contains("sensor_width_mm")) m_sensorWidthMm = map.value("sensor_width_mm").toDouble(&ok);
    if (map.contains("sensor_height_mm")) m_sensorHeightMm = map.value("sensor_height_mm").toDouble(&ok);
    if (map.contains("image_width_px")) m_imageWidthPx = map.value("image_width_px").toInt();
    if (map.contains("image_height_px")) m_imageHeightPx = map.value("image_height_px").toInt();
    return ok;
}

void CameraModel::groundResolutionAtAltitude(double altitudeM, double &resX_m_per_px, double &resY_m_per_px) const
{
    // GSD_x = (H * sensor_width_mm) / (focal_length_mm * image_width_px)
    resX_m_per_px = (altitudeM * m_sensorWidthMm) / (m_focalLengthMm * double(m_imageWidthPx));
    resY_m_per_px = (altitudeM * m_sensorHeightMm) / (m_focalLengthMm * double(m_imageHeightPx));
}

QVariantMap CameraModel::toMap() const
{
    QVariantMap m;
    m["focal_length_mm"] = m_focalLengthMm;
    m["sensor_width_mm"] = m_sensorWidthMm;
    m["sensor_height_mm"] = m_sensorHeightMm;
    m["image_width_px"] = m_imageWidthPx;
    m["image_height_px"] = m_imageHeightPx;
    return m;
}