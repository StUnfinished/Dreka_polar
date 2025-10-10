#include "mission_pattern_controller.h"

#include <QDebug>
#include <QJsonDocument> // 新增：用于序列化 QVariant -> JSON

#include "locator.h"
#include "mission_traits.h"
#include "camera_model.h"
#include "area_planner.h"

using namespace md::domain;
using namespace md::presentation;

MissionPatternController::MissionPatternController(QObject* parent) :
    QObject(parent),
    m_missionsService(md::app::Locator::get<IMissionsService>())
{
    Q_ASSERT(m_missionsService);
}

QVariant MissionPatternController::missionId() const
{
    return m_mission ? m_mission->id() : QVariant();
}

QVariant MissionPatternController::pattern() const
{
    return m_pattern ? m_pattern->toVariantMap() : QVariant();
}

QJsonArray MissionPatternController::parameters() const
{
    if (!m_pattern)
        return QJsonArray();

    QJsonArray jsons;
    for (const ParameterType* parameter : m_pattern->type()->parameters.values())
    {
        jsons.append(QJsonObject::fromVariantMap(parameter->toVariantMap()));
    }
    return jsons;
}

QJsonObject MissionPatternController::parameterValues() const
{
    if (!m_pattern)
        return QJsonObject();

    return QJsonObject::fromVariantMap(m_pattern->parameters());
}

QJsonArray MissionPatternController::pathPositions() const
{
    if (!m_pattern)
        return QJsonArray();

    return QJsonArray::fromVariantList(m_pattern->path().toVariantList());
}

QJsonArray MissionPatternController::areaPositions() const
{
    if (!m_pattern)
        return QJsonArray();

    return QJsonArray::fromVariantList(m_pattern->area().toVariantList());
}

bool MissionPatternController::isReady() const
{
    if (!m_pattern)
        return false;

    return m_pattern->isReady();
}

void MissionPatternController::selectMission(const QVariant& missionId)
{
    if (this->missionId() == missionId)
        return;

    m_mission = m_missionsService->mission(missionId);
    emit missionChanged();

    if (m_pattern)
        this->cancel();
}

void MissionPatternController::createPattern(const QString& patternTypeId)
{
    if (!m_mission)
        return;

    if (m_pattern)
        m_pattern->deleteLater();

    m_pattern = m_missionsService->createRoutePattern(patternTypeId);

    if (m_pattern)
    {
        connect(m_pattern, &RoutePattern::pathPositionsChanged, this,
                &MissionPatternController::pathPositionsChanged);
        connect(m_pattern, &RoutePattern::changed, this,
                &MissionPatternController::parameterValuesChanged);

        if (m_mission->route()->count())
        {
            // Take initial altitude from entry point
            Geodetic entryPoint = m_mission->route()->lastItem()->position;
            if (m_pattern->hasParameter(mission::altitude.id))
                m_pattern->setParameter(mission::altitude.id, entryPoint.altitude());
        }
    }
    emit patternChanged();
    emit parameterValuesChanged();
    emit pathPositionsChanged();
}

void MissionPatternController::setParameter(const QString& parameterId, const QVariant& value)
{
    if (!m_pattern)
        return;

    m_pattern->setParameter(parameterId, value);
}

void MissionPatternController::setAreaPositions(const QVariantList& positions)
{
    if (!m_pattern)
        return;

    QVector<Geodetic> areaPositions;
    for (const QVariant& position : positions)
    {
        areaPositions.append(Geodetic(position.toMap()));
    }
    m_pattern->setArea(areaPositions);
}

void MissionPatternController::cancel()
{
    if (m_pattern)
    {
        m_pattern->deleteLater();
        m_pattern = nullptr;
    }

    emit patternChanged();
    emit parameterValuesChanged();
    emit pathPositionsChanged();
}

void MissionPatternController::apply()
{
    if (!m_pattern || !m_mission)
        return;

    for (MissionRouteItem* item : m_pattern->createItems())
    {
        m_mission->route()->addItem(item);
    }
    m_missionsService->saveMission(m_mission);

    this->cancel();
}

// 如果已有 generateAreaMission，请替换其实现；下面为示例实现：
void MissionPatternController::generateAreaMission(const QVariantMap &params)
{
    // 打印接收到的 params（序列化为 JSON，便于在控制台查看）
    try {
        QJsonDocument doc = QJsonDocument::fromVariant(QVariant(params));
        qDebug().noquote() << "MissionPatternController::generateAreaMission received params:" << doc.toJson(QJsonDocument::Compact);
    } catch (...) {
        qDebug() << "MissionPatternController::generateAreaMission received params (non-serializable):" << params;
    }

    CameraModel cam;
    // 优先从 params 中读取 camera 或 camera_file
    if (params.contains("camera_file")) {
        QString camFile = params.value("camera_file").toString();
        if (!cam.loadFromFile(camFile)) {
            qWarning() << "MissionPatternController: failed to load camera file:" << camFile << " — using defaults";
        } else {
            qDebug() << "Loaded camera from file:" << camFile;
        }
    } else if (params.contains("camera")) {
        cam.loadFromMap(params.value("camera").toMap());
        qDebug() << "Loaded camera from params.camera";
    } else {
        // 尝试加载默认相机文件（相对于可执行或资源目录，请根据项目调整路径）
        if (cam.loadFromFile(QStringLiteral(":/cameras/default_camera.json"))) {
            qDebug() << "Loaded default camera resource";
        } else {
            qDebug() << "Using CameraModel defaults";
        }
    }

    // 调用规划器并打印过程
    qDebug() << "Calling planner::planAreaMission(...)";
    QVariantList waypoints = planner::planAreaMission(params, cam);
    qDebug() << "planner::planAreaMission returned waypoints count =" << waypoints.size();

    // 尝试序列化并打印完整 waypoints JSON（可能较大）
    try {
        QJsonDocument wdoc = QJsonDocument::fromVariant(QVariant(waypoints));
        qDebug().noquote() << "Waypoints JSON:" << wdoc.toJson(QJsonDocument::Compact);
    } catch (...) {
        qDebug() << "Waypoints (non-serializable):" << waypoints;
    }

    // 发送回调信号给 QML / 前端
    QVariantMap summary;
    summary["waypoints_count"] = static_cast<int>(waypoints.size());
    if (params.contains("altitude_m")) summary["altitude_m"] = params.value("altitude_m");
    emit onAreaMissionGenerated(waypoints, summary);

    qDebug() << "MissionPatternController::generateAreaMission finished and signal emitted";
}
