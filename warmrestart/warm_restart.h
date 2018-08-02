#ifndef SWSS_WARM_RESTART_H
#define SWSS_WARM_RESTART_H

#include <string>
#include "dbconnector.h"
#include "redisclient.h"

namespace swss {

class WarmStart
{
public:
	enum WarmStartState
	{
	    INIT,
	    RESTORED,
	    RECONCILED,
	};

    typedef std::map<WarmStartState, std::string>  WarmStartStateNameMap;
    static const WarmStartStateNameMap warmStartStateNameMap;

    static WarmStart &getInstance();

    static bool checkWarmStart(const std::string &app_name, const std::string &docker_name = "swss");
    static bool isWarmStart();
    static void setWarmStartState(const std::string &app_name, WarmStartState state);
private:
	std::shared_ptr<swss::DBConnector>          m_stateDb;
	std::shared_ptr<swss::DBConnector>          m_cfgDb;
	std::shared_ptr<swss::RedisClient>          m_stateRedisClient;
	std::shared_ptr<swss::RedisClient>          m_cfgRedisClient;
	bool enabled;
};

}

#endif
