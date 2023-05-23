#pragma once

#include "dbconnector.h"
#include "orch.h"
#include "producerstatetable.h"

#include <map>
#include <set>
#include <string>

namespace swss {

#define SFLOW_ERROR_SPEED_STR "error"

struct SflowPortInfo
{
    bool        local_rate_cfg;
    bool        local_admin_cfg;
    std::string speed;
    std::string rate;
    std::string admin;
};

/* Port to Local config map  */
typedef std::map<std::string, SflowPortInfo> SflowPortConfMap;

class SflowMgr : public Orch
{
public:
    SflowMgr(DBConnector *cfgDb, DBConnector *appDb, const std::vector<std::string> &tableNames);

    using Orch::doTask;
private:
    Table                  m_cfgSflowTable;
    Table                  m_cfgSflowSessionTable;
    ProducerStateTable     m_appSflowTable;
    ProducerStateTable     m_appSflowSessionTable;
    SflowPortConfMap       m_sflowPortConfMap;
    bool                   m_intfAllConf;
    bool                   m_gEnable;

    void doTask(Consumer &consumer);
    void sflowHandleService(bool enable);
    void sflowUpdatePortInfo(Consumer &consumer);
    void sflowHandleSessionAll(bool enable);
    void sflowHandleSessionLocal(bool enable);
    void sflowCheckAndFillValues(std::string alias, std::vector<FieldValueTuple> &values, std::vector<FieldValueTuple> &fvs);
    void sflowGetPortInfo(std::vector<FieldValueTuple> &fvs, SflowPortInfo &local_info);
    void sflowGetGlobalInfo(std::vector<FieldValueTuple> &fvs, const std::string& alias);
    std::string findSamplingRate(const std::string& speed);
};

}
