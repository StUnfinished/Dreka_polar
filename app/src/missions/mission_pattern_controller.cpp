#include "mission_pattern_controller.h"

#include <QDebug>
#include <QJsonDocument> // 新增：用于序列化 QVariant -> JSON

#include "locator.h"
#include "mission_traits.h"
#include "camera_model.h"
#include "area_planner.h"
#include "strip_planner.h"
#include "poi_planner.h"

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

    // 在尝试把规划结果写入 mission 之前，优先从 params 中取 missionId 并 selectMission
    if (!m_mission && params.contains("missionId")) {
        QVariant mid = params.value("missionId");
        qDebug() << "generateAreaMission: selecting mission from params missionId =" << mid;
        selectMission(mid);
    }

    // 如果仍未设置 m_mission，尝试从服务获取第一个可用 mission（作为回退）
    if (!m_mission) {
        auto missions = m_missionsService->missions();
        if (!missions.isEmpty()) {
            QVariant firstId = missions.first()->id();
            qDebug() << "generateAreaMission: no mission selected in UI, falling back to first mission id =" << firstId;
            selectMission(firstId);
        } else {
            qDebug() << "generateAreaMission: no missions available in IMissionsService";
        }
    }

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

    // 新增：自动将规划结果加入当前任务（仅当 m_mission 已存在）
    if (!m_mission) {
        qWarning() << "MissionPatternController::generateAreaMission: no current mission selected - skipping addPlannedRouteToMission";
    } else {
        addPlannedRouteToMission(waypoints);
    }

    qDebug() << "MissionPatternController::generateAreaMission finished and signal emitted";
}

void MissionPatternController::addPlannedRouteToMission(const QVariantList& waypoints)
{

    if (!m_mission){
        qDebug() << "MissionPatternController::addPlannedRouteToMission failed: no current mission";
        return;
    }

    MissionRoute* route = m_mission->route();
    const MissionType* missionType = m_mission->type();

    qDebug() << "MissionPatternController::addPlannedRouteToMission - preparing to add" << waypoints.size() << "waypoints to mission" << m_mission->id();

    // 为避免在批量添加过程中触发 route->itemAdded 引发的重入/崩溃问题，
    // 在添加期间阻塞 route 信号，添加完成后再一次性保存 mission。
    // bool blocked = false;
    // if (route) {
    //     blocked = route->blockSignals(true);
    //     qDebug() << "Blocked route signals for batch add (previous state):" << blocked;
    // } else {
    //     qWarning() << "addPlannedRouteToMission: route is null for mission" << m_mission->id();
    //     return;
    // }

    for (int i = 0; i < waypoints.size(); ++i) {
        QVariantMap pos = waypoints[i].toMap();

        const MissionItemType* type = nullptr;  
        if (i == 0) {  
            type = missionType->homeItemType;  
        } else {  
            // 获取最后一个非HOME类型的航点（Waypoint类型）  
            for (auto itemType : missionType->itemTypes) {  
                if (itemType != missionType->homeItemType) {  
                    type = itemType;    // 不break,继续遍历,最后一个会覆盖前面的
                }  
            }  
        }  

        if (!type) {
            qWarning() << "addPlannedRouteToMission: no valid MissionItemType for index" << i << " skipping";
            continue;
        }

        QVariantMap parameters = type->defaultParameters();

        MissionRouteItem* item = new MissionRouteItem(type, type->shortName, utils::generateId(),
                                                      parameters, pos);
        item->setParameters(parameters);
        route->addItem(item);

        qDebug() << "addPlannedRouteToMission: created route item idx=" << i
                 << " id=" << item->id()
                 << " lat=" << pos.value("latitude") << " lon=" << pos.value("longitude");
        // 不在此处每条持久化（避免触发服务端/信号重入），统一在下方保存
        // m_missionsService->saveItem(route, item);
    }

    // 解除阻塞并一次性保存 mission（触发必要的 change 信号供 UI/Map 刷新）
    // route->blockSignals(blocked); // 恢复先前状态（通常 false）
    // qDebug() << "addPlannedRouteToMission: unblocked route signals, calling saveMission";
    m_missionsService->saveMission(m_mission);

    qDebug() << "MissionPatternController::addPlannedRouteToMission finished";
}

// add: clear all waypoints from current mission
void MissionPatternController::clearAllRouteItems()  
{  
    if (!m_mission) {  
        qDebug() << "MissionPatternController::clearAllRouteItems failed: no current mission";  
        return;  
    }  
  
    MissionRoute* route = m_mission->route();  
      
    qDebug() << "MissionPatternController::clearAllRouteItems - removing" << route->count() << "waypoints";  
      
    // 从后往前删除  
    while (route->count() > 0) {  
        route->removeItem(route->item(route->count() - 1));  
    }  
      
    m_missionsService->saveMission(m_mission);  
      
    qDebug() << "MissionPatternController::clearAllRouteItems finished";  
}

void MissionPatternController::generateStripMission(const QVariantMap &params)
{
    // 打印接收到的 params
    try {
        QJsonDocument doc = QJsonDocument::fromVariant(QVariant(params));
        qDebug().noquote() << "MissionPatternController::generateStripMission received params:" << doc.toJson(QJsonDocument::Compact);
    } catch (...) {
        qDebug() << "MissionPatternController::generateStripMission received params (non-serializable):" << params;
    }

    CameraModel cam;
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
        if (cam.loadFromFile(QStringLiteral(":/cameras/default_camera.json"))) {
            qDebug() << "Loaded default camera resource";
        } else {
            qDebug() << "Using CameraModel defaults";
        }
    }

    qDebug() << "Calling planner::planStripMission(...)";
    QVariantList waypoints = planner::planStripMission(params, cam);
    qDebug() << "planner::planStripMission returned waypoints count =" << waypoints.size();

    // 选择 mission（优先 params 中的 missionId）
    if (!m_mission && params.contains("missionId")) {
        QVariant mid = params.value("missionId");
        qDebug() << "generateStripMission: selecting mission from params missionId =" << mid;
        selectMission(mid);
    }

    if (!m_mission) {
        auto missions = m_missionsService->missions();
        if (!missions.isEmpty()) {
            QVariant firstId = missions.first()->id();
            qDebug() << "generateStripMission: no mission selected in UI, falling back to first mission id =" << firstId;
            selectMission(firstId);
        } else {
            qDebug() << "generateStripMission: no missions available in IMissionsService";
        }
    }

    // 打印 waypoints（可选）
    try {
        QJsonDocument wdoc = QJsonDocument::fromVariant(QVariant(waypoints));
        qDebug().noquote() << "Strip waypoints JSON:" << wdoc.toJson(QJsonDocument::Compact);
    } catch (...) {
        qDebug() << "Strip waypoints (non-serializable):" << waypoints;
    }

    // 回调前端
    QVariantMap summary;
    summary["waypoints_count"] = static_cast<int>(waypoints.size());
    if (params.contains("altitude_m")) summary["altitude_m"] = params.value("altitude_m");
    emit onAreaMissionGenerated(waypoints, summary);

    // 自动加入当前 mission（如果已选）
    if (!m_mission) {
        qWarning() << "MissionPatternController::generateStripMission: no current mission selected - skipping addPlannedRouteToMission";
    } else {
        addPlannedRouteToMission(waypoints);
    }

    qDebug() << "MissionPatternController::generateStripMission finished and signal emitted";
}

void MissionPatternController::generatePoiMission(const QVariantMap &params)
{
    try {
        QJsonDocument doc = QJsonDocument::fromVariant(QVariant(params));
        qDebug().noquote() << "MissionPatternController::generatePoiMission received params:" << doc.toJson(QJsonDocument::Compact);
    } catch (...) {
        qDebug() << "MissionPatternController::generatePoiMission received params (non-serializable):" << params;
    }

    CameraModel cam;
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
        if (cam.loadFromFile(QStringLiteral(":/cameras/default_camera.json"))) {
            qDebug() << "Loaded default camera resource";
        } else {
            qDebug() << "Using CameraModel defaults";
        }
    }

    qDebug() << "Calling planner::planPoiMission(...)";
    QVariantList waypoints = planner::planPoiMission(params, cam);
    qDebug() << "planner::planPoiMission returned waypoints count =" << waypoints.size();

    if (waypoints.isEmpty()) {
        emit onAreaMissionFailed(QStringLiteral("Poi planner returned no waypoints"));
        return;
    }

    // ensure mission selection
    if (!m_mission && params.contains("missionId")) {
        selectMission(params.value("missionId"));
    }
    if (!m_mission) {
        auto missions = m_missionsService->missions();
        if (!missions.isEmpty()) selectMission(missions.first()->id());
    }

    QVariantMap summary;
    summary["waypoints_count"] = static_cast<int>(waypoints.size());
    if (params.contains("radius")) summary["radius_m"] = params.value("radius");
    emit onAreaMissionGenerated(waypoints, summary);

    if (!m_mission) {
        qWarning() << "MissionPatternController::generatePoiMission: no current mission selected - skipping addPlannedRouteToMission";
    } else {
        addPlannedRouteToMission(waypoints);
    }

    qDebug() << "MissionPatternController::generatePoiMission finished";
}