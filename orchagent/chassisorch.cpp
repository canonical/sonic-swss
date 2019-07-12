#include "chassisorch.h"
#include "routeorch.h"

ChassisOrch::ChassisOrch(
    DBConnector* cfgDb,
    DBConnector* applDb, 
    const std::vector<std::string>& tableNames, 
    VNetRouteOrch* vNetRouteOrch) :
    Orch(cfgDb, tableNames),
    m_passThroughRouteTable(applDb, APP_PASS_THROUGH_ROUTE_TABLE_NAME),
    m_vNetRouteOrch(vNetRouteOrch)
{
}

void ChassisOrch::handleMirrorSessionMessage(const std::string & op, const IpAddress& ip)
{
    SWSS_LOG_ENTER();

    if (op == SET_COMMAND)
    {
        m_vNetRouteOrch->attach(this, ip);
    }
    else
    {
        m_vNetRouteOrch->detach(this, ip);
    }
}

void ChassisOrch::update(SubjectType type, void* ctx)
{
    VNetNextHopUpdate* updateInfo = reinterpret_cast<VNetNextHopUpdate*>(ctx);
    if (updateInfo->op == SET_COMMAND)
    {
        addRouteToPassThroughRouteTable(*updateInfo);
    }
    else
    {
        deleteRoutePassThroughRouteTable(*updateInfo);
    }
}

void ChassisOrch::addRouteToPassThroughRouteTable(const VNetNextHopUpdate& update)
{
    SWSS_LOG_ENTER();

    std::vector<FieldValueTuple> fvVector;
    fvVector.emplace_back("redistribute", "true");
    fvVector.emplace_back("next_vrf_name", update.vnet);
    fvVector.emplace_back("next_hop_ip", update.nexthop.ips.to_string());
    fvVector.emplace_back("ifname", update.nexthop.ifname);
    fvVector.emplace_back("source", "CHASSIS_ORCH");
    const std::string everflow_route = update.destination.to_string() + "/" + (update.destination.isV4() ? "32" : "128");
    m_passThroughRouteTable.set(everflow_route, fvVector);
}

void ChassisOrch::deleteRoutePassThroughRouteTable(const VNetNextHopUpdate& update)
{
    SWSS_LOG_ENTER();
    const std::string everflow_route = update.destination.to_string() + "/" + (update.destination.isV4() ? "32" : "128");
    m_passThroughRouteTable.del(everflow_route);
}

void ChassisOrch::doTask(Consumer &consumer)
{
    SWSS_LOG_ENTER();

    const std::string & tableName = consumer.getTableName();
    auto it = consumer.m_toSync.begin();
    while (it != consumer.m_toSync.end())
    {
        if (tableName == CFG_PASS_THROUGH_ROUTE_TABLE_NAME)
        {
            auto t = it->second;
            const std::string & op = kfvOp(t);
            const std::string & ip = kfvKey(t);
            handleMirrorSessionMessage(op, ip);
        }
        else
        {
            SWSS_LOG_ERROR("Unknown table : %s", tableName.c_str());
        }
        it = consumer.m_toSync.erase(it);
    }

}

