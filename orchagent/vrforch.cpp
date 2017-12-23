#include <cassert>
#include <string>
#include <vector>
#include <unordered_map>

#include "sai.h"
#include "macaddress.h"
#include "orch.h"
#include "request_parser.h"
#include "vrforch.h"

VRFOrch::VRFOrch(DBConnector *db, const std::string& tableName) : Orch(db, tableName)
{
    SWSS_LOG_ENTER();
}

void VRFOrch::doTask(Consumer &consumer)
{
    SWSS_LOG_ENTER();

    auto it = consumer.m_toSync.begin();
    while (it != consumer.m_toSync.end())
    {
        VRFRequest request;
        if (!request.Parse(it))
        {
            ++it;
        }
        // Make all the stuff and erase
        it = consumer.m_toSync.erase(it);
    }
}


bool VRFOrch::AddVRF(const VRFRequest& request)
{
    return true;
}

bool VRFOrch::DeleteVRF(const VRFRequest& request)
{
    return true;
}
