#ifndef MISSION_PATTERN_CONTROLLER_H
#define MISSION_PATTERN_CONTROLLER_H

#include "i_missions_service.h"

#include <QObject>
#include <QJsonArray>
#include <QVariant> // 添加 QVariant, QVariantList, QVariantMap 支持
#include <QString>

namespace md::presentation
{
class MissionPatternController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariant missionId READ missionId WRITE selectMission NOTIFY missionChanged)
    Q_PROPERTY(QVariant pattern READ pattern NOTIFY patternChanged)
    Q_PROPERTY(QJsonArray parameters READ parameters NOTIFY patternChanged)
    Q_PROPERTY(QJsonObject parameterValues READ parameterValues NOTIFY parameterValuesChanged)
    Q_PROPERTY(QJsonArray pathPositions READ pathPositions NOTIFY pathPositionsChanged)
    Q_PROPERTY(bool ready READ isReady NOTIFY pathPositionsChanged)

public:
    explicit MissionPatternController(QObject* parent = nullptr);

    QVariant missionId() const;
    QVariant pattern() const;
    QJsonArray parameters() const;
    QJsonObject parameterValues() const;
    QJsonArray pathPositions() const;
    bool isReady() const;

    Q_INVOKABLE QJsonArray areaPositions() const;

    // 新增：供 QML/JS/C++ 调用的生成航线接口（与实现签名一致）
    Q_INVOKABLE void generateAreaMission(const QVariantMap &params);

    // Add: clear all route items from current mission
    Q_INVOKABLE void clearAllRouteItems();

public slots:
    void selectMission(const QVariant& missionId);
    void createPattern(const QString& patternTypeId);
    void setParameter(const QString& parameterId, const QVariant& value);
    void setAreaPositions(const QVariantList& positions);
    void cancel();
    void apply();

signals:
    void patternChanged();
    void missionChanged();
    void parameterValuesChanged();
    void pathPositionsChanged();

    // 新增：生成结果回调信号（QML 侧 Connections 监视）
    void onAreaMissionGenerated(const QVariantList &waypoints, const QVariantMap &summary);
    void onAreaMissionFailed(const QString &reason);

private:
    void addPlannedRouteToMission(const QVariantList& waypoints);
    
    domain::IMissionsService* const m_missionsService;
    domain::Mission* m_mission = nullptr;
    domain::RoutePattern* m_pattern = nullptr;
};
} // namespace md::presentation

#endif // MISSION_PATTERN_CONTROLLER_H