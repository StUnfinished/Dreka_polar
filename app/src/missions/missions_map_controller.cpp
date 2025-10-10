#include "missions_map_controller.h"

#include <QDebug>

#include "locator.h"

using namespace md::domain;
using namespace md::presentation;

MissionsMapController::MissionsMapController(QObject* parent) :
    QObject(parent),
    m_missions(md::app::Locator::get<IMissionsService>())
{
    qDebug() << "MissionsMapController::ctor m_missions ptr =" << m_missions;
    if (m_missions) {
        // 尝试连接服务的 missionAdded 信号（如果接口名字不同，请搜索并对齐）
        // 仅用于调试，实际项目可能已在其它地方完成连接
        // connect(m_missions, &IMissionsService::missionAdded, this, &MissionsMapController::onMissionAdded);
        qDebug() << "MissionsMapController::ctor - OK (m_missions present)";
    } else {
        qWarning() << "MissionsMapController::ctor - IMissionsService NOT found via Locator";
    }

    Q_ASSERT(m_missions);

    connect(m_missions, &IMissionsService::missionAdded, this,
            &MissionsMapController::onMissionAdded);
    connect(m_missions, &IMissionsService::missionRemoved, this,
            &MissionsMapController::onMissionRemoved);

    for (Mission* mission : m_missions->missions())
    {
        this->onMissionAdded(mission);
    }
}

QVariant MissionsMapController::selectedMissionId() const
{
    return m_selectedMissionId;
}

QJsonArray MissionsMapController::missions() const
{
    QJsonArray missions;
    for (Mission* mission : m_missions->missions())
    {
        missions += QJsonObject::fromVariantMap(mission->toVariantMap());
    }
    return missions;
}

QJsonObject MissionsMapController::mission(const QVariant& missionId) const
{
    Mission* mission = m_missions->mission(missionId);
    if (!mission)
        return QJsonObject();

    return QJsonObject::fromVariantMap(mission->toVariantMap());
}

QJsonArray MissionsMapController::routeItems(const QVariant& missionId) const
{
    Mission* mission = m_missions->mission(missionId);
    if (!mission)
        return QJsonArray();

    QJsonArray items;
    for (MissionRouteItem* item : mission->route()->items())
    {
        items.append(QJsonObject::fromVariantMap(item->toVariantMap()));
    }
    return items;
}

void MissionsMapController::selectMission(const QVariant& missionId)
{
    if (m_selectedMissionId == missionId)
        return;

    m_selectedMissionId = missionId;
    emit selectedMissionChanged(missionId);
}

void MissionsMapController::updateVisibility(const QVariant& missionId, bool visible)
{
    Mission* mission = m_missions->mission(missionId);
    if (!mission)
        return;

    mission->visible.set(visible);
    m_missions->saveMission(mission);
}

void MissionsMapController::updateRouteItem(const QVariant& missionId, int index,
                                            const QJsonObject& routeItemData)
{
    Mission* mission = m_missions->mission(missionId);
    if (!mission)
        return;

    MissionRouteItem* item = mission->route()->item(index);
    if (!item)
        return;

    item->fromVariantMap(routeItemData.toVariantMap());
}

void MissionsMapController::onMissionAdded(domain::Mission* mission)
{
    qDebug() << "MissionsMapController::onMissionAdded called for mission id =" << (mission ? mission->id() : QVariant());
    if (!mission) return;

    connect(mission->route, &MissionRoute::itemAdded, this,
            [this, mission](int index, MissionRouteItem* item) {
                qDebug() << "MissionsMapController::onMissionAdded - route itemAdded: missionId=" << mission->id()
                         << " index=" << index << " itemId=" << item->id();
                emit routeItemAdded(mission->route()->id(), index, item->toVariantMap());
            });
    connect(mission->route, &MissionRoute::itemChanged, this,
            [this, mission](int index, MissionRouteItem* item) {
                qDebug() << "MissionsMapController::onMissionAdded - route itemChanged: missionId=" << mission->id()
                         << " index=" << index << " itemId=" << item->id();
                emit routeItemChanged(mission->route()->id(), index, item->toVariantMap());
            });
    connect(mission->route, &MissionRoute::itemRemoved, this, [this, mission](int index) {
        qDebug() << "MissionsMapController::onMissionAdded - route itemRemoved: missionId=" << mission->id()
                 << " index=" << index;
        emit routeItemRemoved(mission->route()->id(), index);
    });
    connect(mission, &Mission::changed, this, [this, mission]() {
        qDebug() << "MissionsMapController::onMissionAdded - mission changed: missionId=" << mission->id();
        emit missionChanged(mission->toVariantMap());
    });

    qDebug() << "MissionsMapController::onMissionAdded - emitting missionAdded for missionId=" << mission->id();
    emit missionAdded(mission->toVariantMap());
}

void MissionsMapController::onMissionRemoved(domain::Mission* mission)
{
    disconnect(mission->route, nullptr, this, nullptr);
    emit missionRemoved(mission->id());
}
