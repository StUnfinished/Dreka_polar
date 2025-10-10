#pragma once
#include <QVariantMap>
#include <QString>

class CameraModel
{
public:
    CameraModel();

    bool loadFromFile(const QString &filePath);
    bool loadFromMap(const QVariantMap &map);

    double focalLengthMm() const { return m_focalLengthMm; }
    double sensorWidthMm() const { return m_sensorWidthMm; }
    double sensorHeightMm() const { return m_sensorHeightMm; }
    int imageWidthPx() const { return m_imageWidthPx; }
    int imageHeightPx() const { return m_imageHeightPx; }

    // 给定飞行高度 H（米），返回 x,y 方向的地面分辨率（m / pixel）
    void groundResolutionAtAltitude(double altitudeM, double &resX_m_per_px, double &resY_m_per_px) const;

    QVariantMap toMap() const;

private:
    double m_focalLengthMm = 35.0;
    double m_sensorWidthMm = 36.0;
    double m_sensorHeightMm = 24.0;
    int m_imageWidthPx = 4000;
    int m_imageHeightPx = 3000;
};