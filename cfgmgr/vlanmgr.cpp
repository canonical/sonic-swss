#include <string.h>
#include "logger.h"
#include "producerstatetable.h"
#include "macaddress.h"
#include "vlanmgr.h"
#include "exec.h"
#include "tokenize.h"
#include "shellcmd.h"
#include "warm_restart.h"

using namespace std;
using namespace swss;

#define DOT1Q_BRIDGE_NAME   "Bridge"
#define VLAN_PREFIX         "Vlan"
#define SWITCH_STR       "switch"
#define LAG_PREFIX          "PortChannel"
#define DEFAULT_VLAN_ID     "1"
#define DEFAULT_MTU_STR     "9100"
#define VLAN_HLEN            4

extern MacAddress gMacAddress;

VlanMgr::VlanMgr(DBConnector *cfgDb, DBConnector *appDb, DBConnector *stateDb, const vector<string> &tableNames) :
        Orch(cfgDb, tableNames),
        m_cfgVlanTable(cfgDb, CFG_VLAN_TABLE_NAME),
        m_cfgVlanMemberTable(cfgDb, CFG_VLAN_MEMBER_TABLE_NAME),
        m_statePortTable(stateDb, STATE_PORT_TABLE_NAME),
        m_stateLagTable(stateDb, STATE_LAG_TABLE_NAME),
        m_stateVlanTable(stateDb, STATE_VLAN_TABLE_NAME),
        m_stateVlanMemberTable(stateDb, STATE_VLAN_MEMBER_TABLE_NAME),
        m_appFdbTableProducer(appDb, APP_FDB_TABLE_NAME),
        m_appSwitchTableProducer(appDb, APP_SWITCH_TABLE_NAME),
        m_appVlanTableProducer(appDb, APP_VLAN_TABLE_NAME),
        m_appVlanMemberTableProducer(appDb, APP_VLAN_MEMBER_TABLE_NAME),
        replayDone(false)
{
    SWSS_LOG_ENTER();

    if (WarmStart::isWarmStart())
    {
        vector<string> vlanKeys, vlanMemberKeys;

        /* cache all vlan and vlan member config */
        m_cfgVlanTable.getKeys(vlanKeys);
        m_cfgVlanMemberTable.getKeys(vlanMemberKeys);
        for (auto k : vlanKeys)
        {
            m_vlanReplay.insert(k);
        }
        for (auto k : vlanMemberKeys)
        {
            m_vlanMemberReplay.insert(k);
        }
        if (m_vlanReplay.empty())
        {
            replayDone = true;
            WarmStart::setWarmStartState("vlanmgrd", WarmStart::REPLAYED);
            SWSS_LOG_NOTICE("vlanmgr warmstart state set to REPLAYED");
            WarmStart::setWarmStartState("vlanmgrd", WarmStart::RECONCILED);
            SWSS_LOG_NOTICE("vlanmgr warmstart state set to RECONCILED");
        }
        const std::string cmds = std::string("")
          + IP_CMD + " link show " + DOT1Q_BRIDGE_NAME + " 2>/dev/null";

        std::string res;
        int ret = swss::exec(cmds, res);
        if (ret == 0)
        {
            // Don't reset vlan aware bridge upon swss docker warm restart.
            SWSS_LOG_INFO("vlanmgrd warm start, skipping bridge create");
            return;
        }
    }
    // Initialize Linux dot1q bridge and enable vlan filtering
    // The command should be generated as:
    // /bin/bash -c "/sbin/ip link del Bridge 2>/dev/null ;
    //               /sbin/ip link add Bridge up type bridge &&
    //               /sbin/ip link set Bridge mtu {{ mtu_size }} &&
    //               /sbin/ip link set Bridge address {{gMacAddress}} &&
    //               /sbin/bridge vlan del vid 1 dev Bridge self;
    //               /sbin/ip link del dummy 2>/dev/null;
    //               /sbin/ip link add dummy type dummy &&
    //               /sbin/ip link set dummy master Bridge"

    const std::string cmds = std::string("")
      + BASH_CMD + " -c \""
      + IP_CMD + " link del " + DOT1Q_BRIDGE_NAME + " 2>/dev/null; "
      + IP_CMD + " link add " + DOT1Q_BRIDGE_NAME + " up type bridge && "
      + IP_CMD + " link set " + DOT1Q_BRIDGE_NAME + " mtu " + DEFAULT_MTU_STR + " && "
      + IP_CMD + " link set " + DOT1Q_BRIDGE_NAME + " address " + gMacAddress.to_string() + " && "
      + BRIDGE_CMD + " vlan del vid " + DEFAULT_VLAN_ID + " dev " + DOT1Q_BRIDGE_NAME + " self; "
      + IP_CMD + " link del dev dummy 2>/dev/null; "
      + IP_CMD + " link add dummy type dummy && "
      + IP_CMD + " link set dummy master " + DOT1Q_BRIDGE_NAME + "\"";

    std::string res;
    EXEC_WITH_ERROR_THROW(cmds, res);

    // The generated command is:
    // /bin/echo 1 > /sys/class/net/Bridge/bridge/vlan_filtering
    const std::string echo_cmd = std::string("")
      + ECHO_CMD + " 1 > /sys/class/net/" + DOT1Q_BRIDGE_NAME + "/bridge/vlan_filtering";

    int ret = swss::exec(echo_cmd, res);
    /* echo will fail in virtual switch since /sys directory is read-only.
     * need to use ip command to setup the vlan_filtering which is not available in debian 8.
     * Once we move sonic to debian 9, we can use IP command by default
     * ip command available in Debian 9 to create a bridge with a vlan filtering:
     * /sbin/ip link add Bridge up type bridge vlan_filtering 1 */
    if (ret != 0)
    {
        const std::string echo_cmd_backup = std::string("")
          + IP_CMD + " link set " + DOT1Q_BRIDGE_NAME + " type bridge vlan_filtering 1";

        EXEC_WITH_ERROR_THROW(echo_cmd_backup, res);
    }

    /* vlan state notification from portsorch */
    m_VlanStateNotificationConsumer = new swss::NotificationConsumer(appDb, "VLANSTATE");
    auto vlanStatusNotificatier = new Notifier(m_VlanStateNotificationConsumer, this, "VLANSTATE");
    Orch::addExecutor(vlanStatusNotificatier);
}

bool VlanMgr::addHostVlan(int vlan_id)
{
    SWSS_LOG_ENTER();

    // The command should be generated as:
    // /bin/bash -c "/sbin/bridge vlan add vid {{vlan_id}} dev Bridge self &&
    //               /sbin/ip link add link Bridge up name Vlan{{vlan_id}} address {{gMacAddress}} type vlan id {{vlan_id}}"
    const std::string cmds = std::string("")
      + BASH_CMD + " -c \""
      + BRIDGE_CMD + " vlan add vid " + std::to_string(vlan_id) + " dev " + DOT1Q_BRIDGE_NAME + " self && "
      + IP_CMD + " link add link " + DOT1Q_BRIDGE_NAME
               + " down"
               + " name " + VLAN_PREFIX + std::to_string(vlan_id)
               + " address " + gMacAddress.to_string()
               + " type vlan id " + std::to_string(vlan_id) + "\"";

    std::string res;
    EXEC_WITH_ERROR_THROW(cmds, res);

    return true;
}

bool VlanMgr::removeHostVlan(int vlan_id)
{
    SWSS_LOG_ENTER();

    // The command should be generated as:
    // /bin/bash -c "/sbin/ip link del Vlan{{vlan_id}} &&
    //               /sbin/bridge vlan del vid {{vlan_id}} dev Bridge self"
    const std::string cmds = std::string("")
      + BASH_CMD + " -c \""
      + IP_CMD + " link del " + VLAN_PREFIX + std::to_string(vlan_id) + " && "
      + BRIDGE_CMD + " vlan del vid " + std::to_string(vlan_id) + " dev " + DOT1Q_BRIDGE_NAME + " self\"";

    std::string res;
    EXEC_WITH_ERROR_THROW(cmds, res);

    return true;
}

bool VlanMgr::setHostVlanAdminState(int vlan_id, const string &admin_status)
{
    SWSS_LOG_ENTER();

    // The command should be generated as:
    // /sbin/ip link set Vlan{{vlan_id}} {{admin_status}}
    ostringstream cmds;
    cmds << IP_CMD " link set " VLAN_PREFIX + std::to_string(vlan_id) + " " << shellquote(admin_status);

    std::string res;
    EXEC_WITH_ERROR_THROW(cmds.str(), res);

    return true;
}

bool VlanMgr::setHostVlanMtu(int vlan_id, uint32_t mtu)
{
    SWSS_LOG_ENTER();

    // The command should be generated as:
    // /sbin/ip link set Vlan{{vlan_id}} mtu {{mtu}}
    const std::string cmds = std::string("")
      + IP_CMD + " link set " + VLAN_PREFIX + std::to_string(vlan_id) + " mtu " + std::to_string(mtu);

    std::string res;
    int ret = swss::exec(cmds, res);
    if (ret == 0)
    {
        return true;
    }

    /* VLAN mtu should not be larger than member mtu */
    return false;
}

bool VlanMgr::setHostVlanMac(int vlan_id, const string &mac)
{
    SWSS_LOG_ENTER();

    // The command should be generated as:
    // /sbin/ip link set Vlan{{vlan_id}} address {{mac}}
    ostringstream cmds;
    cmds << IP_CMD " link set " VLAN_PREFIX + std::to_string(vlan_id) + " address " << shellquote(mac);

    std::string res;
    EXEC_WITH_ERROR_THROW(cmds.str(), res);

    return true;
}

bool VlanMgr::addHostVlanMember(int vlan_id, const string &port_alias, const string& tagging_mode)
{
    SWSS_LOG_ENTER();

    std::string tagging_cmd;
    if (tagging_mode == "untagged" || tagging_mode == "priority_tagged")
    {
        tagging_cmd = "pvid untagged";
    }

    // The command should be generated as:
    // /bin/bash -c "/sbin/ip link set {{port_alias}} master Bridge &&
    //               /sbin/bridge vlan del vid 1 dev {{ port_alias }} &&
    //               /sbin/bridge vlan add vid {{vlan_id}} dev {{port_alias}} {{tagging_mode}}"
    const std::string key = std::string("") + "Vlan1|" + port_alias;
    if (isVlanMemberStateOk(key)) {
        const std::string cmds = std::string("")
          + BASH_CMD + " -c \""
          + IP_CMD + " link set " + port_alias + " master " + DOT1Q_BRIDGE_NAME + " && "
          + BRIDGE_CMD + " vlan add vid " + std::to_string(vlan_id) + " dev " + port_alias + " " + tagging_cmd + "\"";
    
        std::string res;
        EXEC_WITH_ERROR_THROW(cmds, res);
    }
    else
    {
        const std::string cmds = std::string("")
          + BASH_CMD + " -c \""
          + IP_CMD + " link set " + port_alias + " master " + DOT1Q_BRIDGE_NAME + " && "
          + BRIDGE_CMD + " vlan del vid 1 " + " dev " + port_alias + " " + "untagged" + " && "
          + BRIDGE_CMD + " vlan add vid " + std::to_string(vlan_id) + " dev " + port_alias + " " + tagging_cmd + "\"";
    
        std::string res;
        EXEC_WITH_ERROR_THROW(cmds, res);
    }
    return true;
}

bool VlanMgr::removeHostVlanMember(int vlan_id, const string &port_alias)
{
    SWSS_LOG_ENTER();

    // The command should be generated as:
    // /bin/bash -c '/sbin/bridge vlan del vid {{vlan_id}} dev {{port_alias}} &&
    //               ( /sbin/bridge vlan show dev {{port_alias}} | /bin/grep -q None;
    //               ret=$?; if [ $ret -eq 0 ]; then
    //               /sbin/ip link set {{port_alias}} nomaster;
    //               elif [ $ret -eq 1 ]; then exit 0;
    //               else exit $ret; fi )'

    // When port is not member of any VLAN, it shall be detached from Dot1Q bridge!
    ostringstream cmds, inner;
    inner << BRIDGE_CMD " vlan del vid " + std::to_string(vlan_id) + " dev " << shellquote(port_alias) << " && ( "
      BRIDGE_CMD " vlan show dev " << shellquote(port_alias) << " | "
      GREP_CMD " -q None; ret=$?; if [ $ret -eq 0 ]; then "
      IP_CMD " link set " << shellquote(port_alias) << " nomaster; "
      "elif [ $ret -eq 1 ]; then exit 0; "
      "else exit $ret; fi )";
    cmds << BASH_CMD " -c " << shellquote(inner.str());

    std::string res;
    EXEC_WITH_ERROR_THROW(cmds.str(), res);

    return true;
}

bool VlanMgr::isVlanMacOk()
{
    return !!gMacAddress;
}

void VlanMgr::doSwitchTask(Consumer &consumer)
{
    SWSS_LOG_ENTER();
    auto it = consumer.m_toSync.begin();
    
    while (it != consumer.m_toSync.end())
    {
        KeyOpFieldsValuesTuple t = it->second;
    
        string key = kfvKey(t);
        string op = kfvOp(t);
    
        /* Ensure the key starts with "switch" otherwise ignore */
        if (key != SWITCH_STR)
        {
            SWSS_LOG_ERROR("Invalid key format %s", key.c_str());
            it = consumer.m_toSync.erase(it);
            continue;
        }
    
        SWSS_LOG_DEBUG("key:switch");
    
        for (auto i : kfvFieldsValues(t))
        {
            if (fvField(i) == "fdb_aging_time")
            {
                int agingTime = 0;
                SWSS_LOG_DEBUG("attribute:fdb_aging_time");
                if (op == SET_COMMAND)
                {
                    SWSS_LOG_DEBUG("operation:set");
                    agingTime = atoi(fvValue(i).c_str());
                    if(agingTime < 0)
                    {
                        SWSS_LOG_ERROR("Invalid fdb_aging_time %s", fvValue(i).c_str());
                        break;
                    }
                    SWSS_LOG_DEBUG("value:%s",fvValue(i).c_str());
                }
                else if (op == DEL_COMMAND)
                {
                    SWSS_LOG_DEBUG("operation:del");
                    agingTime=0;
                }
                else
                {
                    SWSS_LOG_ERROR("Unknown operation type %s", op.c_str());
                    break;
                }
    
                vector<FieldValueTuple> fvVector;
                FieldValueTuple aging_time("fdb_aging_time", to_string(agingTime));
                fvVector.push_back(aging_time);
                m_appSwitchTableProducer.set(key, fvVector);
                break;
            }
        }
    
        it = consumer.m_toSync.erase(it);
    }
}
    
void VlanMgr::doFdbTask(Consumer &consumer)
{
    if (!isVlanMacOk())
    {
        SWSS_LOG_DEBUG("VLAN mac not ready, delaying VLAN task");
        return;
    }
    
    auto it = consumer.m_toSync.begin();
    
    while (it != consumer.m_toSync.end())
    {
        KeyOpFieldsValuesTuple t = it->second;
    
        /* format: <VLAN_name>|<MAC_address> */
        vector<string> keys = tokenize(kfvKey(t), config_db_key_delimiter, 1);
        /* keys[0] is vlan as (Vlan10) and keys[1] is mac as (00-00-00-00-00-00) */
        string op = kfvOp(t);
    
        /* Ensure the key starts with "Vlan" otherwise ignore */
        if (strncmp(keys[0].c_str(), VLAN_PREFIX, 4))
        {
            SWSS_LOG_ERROR("Invalid key format. No 'Vlan' prefix: %s", keys[0].c_str());
            it = consumer.m_toSync.erase(it);
            continue;
        }
    
        int vlan_id;
        vlan_id = stoi(keys[0].substr(4));
        if ((vlan_id <= 0) || (vlan_id > 4095))
        {
            SWSS_LOG_ERROR("Invalid key format. Vlan is out of range: %s", keys[0].c_str());
            it = consumer.m_toSync.erase(it);
            continue;
        }
    
        MacAddress mac = MacAddress(keys[1]);
    
        string key = VLAN_PREFIX + to_string(vlan_id);
        key += DEFAULT_KEY_SEPARATOR;
        key += mac.to_string();
    
        if (op == SET_COMMAND)
        {
            string port;
            for (auto i : kfvFieldsValues(t))
            {
                if (fvField(i) == "port")
                {
                    port = fvValue(i);
                    break;
                }
            }
    
            vector<FieldValueTuple> fvVector;
            FieldValueTuple p("port", port);
            fvVector.push_back(p);
            FieldValueTuple t("type", "static");
            fvVector.push_back(t);
    
            m_appFdbTableProducer.set(key, fvVector);
        }
        else if (op == DEL_COMMAND)
        {
            m_appFdbTableProducer.del(key);
        }
        else
        {
            SWSS_LOG_ERROR("Unknown operation type %s", op.c_str());
        }
        it = consumer.m_toSync.erase(it);
    }
}
    
void VlanMgr::doVlanTask(Consumer &consumer)
{
    if (!isVlanMacOk())
    {
        SWSS_LOG_DEBUG("VLAN mac not ready, delaying VLAN task");
        return;
    }
    auto it = consumer.m_toSync.begin();

    while (it != consumer.m_toSync.end())
    {
        auto &t = it->second;

        string key = kfvKey(t);

        /* Ensure the key starts with "Vlan" otherwise ignore */
        if (strncmp(key.c_str(), VLAN_PREFIX, 4))
        {
            SWSS_LOG_ERROR("Invalid key format. No 'Vlan' prefix: %s", key.c_str());
            it = consumer.m_toSync.erase(it);
            continue;
        }

        int vlan_id;
        try
        {
            vlan_id = stoi(key.substr(4));
        }
        catch (...)
        {
            SWSS_LOG_ERROR("Invalid key format. Not a number after 'Vlan' prefix: %s", key.c_str());
            it = consumer.m_toSync.erase(it);
            continue;
        }

        string vlan_alias, port_alias;
        string op = kfvOp(t);

        if (op == SET_COMMAND)
        {
            string admin_status;
            string mtu = DEFAULT_MTU_STR;
            string mac = gMacAddress.to_string();
            vector<FieldValueTuple> fvVector;
            string members;

            /*
             * If state is already set for this vlan, but it doesn't exist in m_vlans set,
             * just add it to m_vlans set and remove the request to skip disrupting Linux vlan.
             * Will hit this scenario for docker warm restart.
             *
             * Otherwise, it is new VLAN create or VLAN attribute update like admin_status/mtu change,
             * proceed with regular processing.
             */
            if (isVlanStateOk(key) && m_vlans.find(key) == m_vlans.end())
            {
                m_vlans.insert(key);
                m_vlanReplay.erase(kfvKey(t));
                it = consumer.m_toSync.erase(it);
                SWSS_LOG_DEBUG("%s already created", kfvKey(t).c_str());
                continue;
            }

            /* Add host VLAN when it has not been created. */
            if (m_vlans.find(key) == m_vlans.end())
            {
                addHostVlan(vlan_id);
            }
            m_vlanReplay.erase(kfvKey(t));

            /* set up host env .... */
            for (auto i : kfvFieldsValues(t))
            {
				/* Set the admin state of the VLAN to DOWN in kernel
				 * when VLAN is created.
				 */
#if 0
                /* Set vlan admin status */
                if (fvField(i) == "admin_status")
                {
                    admin_status = fvValue(i);
                    setHostVlanAdminState(vlan_id, admin_status);
                    fvVector.push_back(i);
                }
                /* Set vlan mtu */
                else if (fvField(i) == "mtu")
#else
    
                if (fvField(i) == "mtu")
#endif
                {
                    mtu = fvValue(i);
                    /*
                     * TODO: support host VLAN mtu setting.
                     * Host VLAN mtu should be set only after member configured
                     * and VLAN state is not UNKNOWN.
                     */
                    SWSS_LOG_DEBUG("%s mtu %s: Host VLAN mtu setting to be supported.", key.c_str(), mtu.c_str());
                }
                else if (fvField(i) == "members@") {
                    members = fvValue(i);
                }
                else if (fvField(i) == "mac")
                {
                    mac = fvValue(i);
                    setHostVlanMac(vlan_id, mac);
                }
            }
            /* fvVector should not be empty */
            if (fvVector.empty())
            {
                FieldValueTuple a("admin_status",  "up");
                fvVector.push_back(a);
            }

            FieldValueTuple m("mtu", mtu);
            fvVector.push_back(m);

            FieldValueTuple mc("mac", mac);
            fvVector.push_back(mc);

            m_appVlanTableProducer.set(key, fvVector);
            m_vlans.insert(key);

            fvVector.clear();
            FieldValueTuple s("state", "ok");
            fvVector.push_back(s);
            m_stateVlanTable.set(key, fvVector);

            it = consumer.m_toSync.erase(it);

            /*
             * Members configured together with VLAN in untagged mode.
             * This is to be compatible with access VLAN configuration from minigraph.
             */
            if (!members.empty())
            {
                processUntaggedVlanMembers(key, members);
            }
        }
        else if (op == DEL_COMMAND)
        {
            if (m_vlans.find(key) != m_vlans.end())
            {
                removeHostVlan(vlan_id);
                m_vlans.erase(key);
                m_appVlanTableProducer.del(key);
                m_stateVlanTable.del(key);
            }
            else
            {
                SWSS_LOG_ERROR("%s doesn't exist", key.c_str());
            }
            SWSS_LOG_DEBUG("%s", (dumpTuple(consumer, t)).c_str());
            it = consumer.m_toSync.erase(it);
        }
        else
        {
            SWSS_LOG_ERROR("Unknown operation type %s", op.c_str());
            SWSS_LOG_DEBUG("%s", (dumpTuple(consumer, t)).c_str());
            it = consumer.m_toSync.erase(it);
        }
    }
    if (!replayDone && m_vlanReplay.empty() &&
        m_vlanMemberReplay.empty() &&
        WarmStart::isWarmStart())
    {
        replayDone = true;
        WarmStart::setWarmStartState("vlanmgrd", WarmStart::REPLAYED);
        SWSS_LOG_NOTICE("vlanmgr warmstart state set to REPLAYED");
        WarmStart::setWarmStartState("vlanmgrd", WarmStart::RECONCILED);
        SWSS_LOG_NOTICE("vlanmgr warmstart state set to RECONCILED");
    }
}

bool VlanMgr::isMemberStateOk(const string &alias)
{
    vector<FieldValueTuple> temp;

    if (!alias.compare(0, strlen(LAG_PREFIX), LAG_PREFIX))
    {
        if (m_stateLagTable.get(alias, temp))
        {
            SWSS_LOG_DEBUG("%s is ready", alias.c_str());
            return true;
        }
    }
    else if (m_statePortTable.get(alias, temp))
    {
        SWSS_LOG_DEBUG("%s is ready", alias.c_str());
        return true;
    }
    SWSS_LOG_DEBUG("%s is not ready", alias.c_str());
    return false;
}

bool VlanMgr::isVlanStateOk(const string &alias)
{
    vector<FieldValueTuple> temp;

    if (!alias.compare(0, strlen(VLAN_PREFIX), VLAN_PREFIX))
    {
        if (m_stateVlanTable.get(alias, temp))
        {
            SWSS_LOG_DEBUG("%s is ready", alias.c_str());
            return true;
        }
    }
    SWSS_LOG_DEBUG("%s is not ready", alias.c_str());
    return false;
}

bool VlanMgr::isVlanMemberStateOk(const string &vlanMemberKey)
{
    vector<FieldValueTuple> temp;

    if (m_stateVlanMemberTable.get(vlanMemberKey, temp))
    {
        SWSS_LOG_DEBUG("%s is ready", vlanMemberKey.c_str());
        return true;
    }
    return false;
}

/*
 * members is grouped in format like
 * "Ethernet1,Ethernet2,Ethernet3,Ethernet4,Ethernet5,Ethernet6,
 * Ethernet7,Ethernet8,Ethernet9,Ethernet10,Ethernet11,Ethernet12,
 * Ethernet13,Ethernet14,Ethernet15,Ethernet16,Ethernet17,Ethernet18,
 * Ethernet19,Ethernet20,Ethernet21,Ethernet22,Ethernet23,Ethernet24"
 */
void VlanMgr::processUntaggedVlanMembers(string vlan, const string &members)
{

    auto consumer_it = m_consumerMap.find(CFG_VLAN_MEMBER_TABLE_NAME);
    if (consumer_it == m_consumerMap.end())
    {
        SWSS_LOG_ERROR("Failed to find tableName:%s", CFG_VLAN_MEMBER_TABLE_NAME);
        return;
    }
    auto& consumer = static_cast<Consumer &>(*consumer_it->second);

    vector<string> vlanMembers = tokenize(members, ',');

    for (auto vlanMember : vlanMembers)
    {
        string member_key = vlan + CONFIGDB_KEY_SEPARATOR + vlanMember;

        /* Directly put it into consumer.m_toSync map */
        if (consumer.m_toSync.find(member_key) == consumer.m_toSync.end())
        {
            vector<FieldValueTuple> fvVector;
            FieldValueTuple t("tagging_mode", "untagged");
            fvVector.push_back(t);
            KeyOpFieldsValuesTuple tuple = make_tuple(member_key, SET_COMMAND, fvVector);
            consumer.addToSync(tuple);
            SWSS_LOG_DEBUG("%s", (dumpTuple(consumer, tuple)).c_str());
        }
        /*
         * There is pending task from consumer pipe, in this case just skip it.
         */
        else
        {
            SWSS_LOG_WARN("Duplicate key %s found in table:%s", member_key.c_str(), CFG_VLAN_MEMBER_TABLE_NAME);
            continue;
        }
    }

    doTask(consumer);
    return;
}

void VlanMgr::doVlanMemberTask(Consumer &consumer)
{
    auto it = consumer.m_toSync.begin();
    while (it != consumer.m_toSync.end())
    {
        auto &t = it->second;

        string key = kfvKey(t);

        /* Ensure the key starts with "Vlan" otherwise ignore */
        if (strncmp(key.c_str(), VLAN_PREFIX, 4))
        {
            SWSS_LOG_ERROR("Invalid key format. No 'Vlan' prefix: %s", key.c_str());
            it = consumer.m_toSync.erase(it);
            continue;
        }

        key = key.substr(4);
        size_t found = key.find(CONFIGDB_KEY_SEPARATOR);
        int vlan_id;
        string vlan_alias, port_alias;
        if (found != string::npos)
        {
            vlan_id = stoi(key.substr(0, found));
            port_alias = key.substr(found+1);
        }
        else
        {
            SWSS_LOG_ERROR("Invalid key format. No member port is presented: %s",
                           kfvKey(t).c_str());
            it = consumer.m_toSync.erase(it);
            continue;
        }

        vlan_alias = VLAN_PREFIX + to_string(vlan_id);
        string op = kfvOp(t);

       // TODO:  store port/lag/VLAN data in local data structure and perform more validations.
        if (op == SET_COMMAND)
        {
             if (isVlanMemberStateOk(kfvKey(t)))
             {
                SWSS_LOG_DEBUG("%s already set", kfvKey(t).c_str());
                m_vlanMemberReplay.erase(kfvKey(t));
                it = consumer.m_toSync.erase(it);
                continue;
             }

            /* Don't proceed if member port/lag is not ready yet */
            if (!isMemberStateOk(port_alias) || !isVlanStateOk(vlan_alias))
            {
                SWSS_LOG_DEBUG("%s not ready, delaying", kfvKey(t).c_str());
                it++;
                continue;
            }
            string tagging_mode = "untagged";

            for (auto i : kfvFieldsValues(t))
            {
                if (fvField(i) == "tagging_mode")
                {
                    tagging_mode = fvValue(i);
                }
            }

            if (tagging_mode != "untagged" &&
                tagging_mode != "tagged"   &&
                tagging_mode != "priority_tagged")
            {
                SWSS_LOG_ERROR("Wrong tagging_mode '%s' for key: %s", tagging_mode.c_str(), kfvKey(t).c_str());
                it = consumer.m_toSync.erase(it);
                continue;
            }

            if (addHostVlanMember(vlan_id, port_alias, tagging_mode))
            {
                key = VLAN_PREFIX + to_string(vlan_id);
                key += DEFAULT_KEY_SEPARATOR;
                key += port_alias;
                m_appVlanMemberTableProducer.set(key, kfvFieldsValues(t));

                vector<FieldValueTuple> fvVector;
                FieldValueTuple s("state", "ok");
                fvVector.push_back(s);
                m_stateVlanMemberTable.set(kfvKey(t), fvVector);

                m_vlanMemberReplay.erase(kfvKey(t));
            }
        }
        else if (op == DEL_COMMAND)
        {
            if (isVlanMemberStateOk(kfvKey(t)))
            {
                removeHostVlanMember(vlan_id, port_alias);
                key = VLAN_PREFIX + to_string(vlan_id);
                key += DEFAULT_KEY_SEPARATOR;
                key += port_alias;
                m_appVlanMemberTableProducer.del(key);
                m_stateVlanMemberTable.del(kfvKey(t));
            }
            else
            {
                SWSS_LOG_DEBUG("%s doesn't exist", kfvKey(t).c_str());
            }
            SWSS_LOG_DEBUG("%s", (dumpTuple(consumer, t)).c_str());
        }
        else
        {
            SWSS_LOG_ERROR("Unknown operation type %s", op.c_str());
        }
        /* Other than the case of member port/lag is not ready, no retry will be performed */
        it = consumer.m_toSync.erase(it);
    }
    if (!replayDone && m_vlanMemberReplay.empty() &&
        WarmStart::isWarmStart())
    {
        replayDone = true;
        WarmStart::setWarmStartState("vlanmgrd", WarmStart::REPLAYED);
        SWSS_LOG_NOTICE("vlanmgr warmstart state set to REPLAYED");
        WarmStart::setWarmStartState("vlanmgrd", WarmStart::RECONCILED);
        SWSS_LOG_NOTICE("vlanmgr warmstart state set to RECONCILED");

    }
}

void VlanMgr::doTask(Consumer &consumer)
{
    SWSS_LOG_ENTER();

    string table_name = consumer.getTableName();

    if (table_name == CFG_VLAN_TABLE_NAME)
    {
        doVlanTask(consumer);
    }
    else if (table_name == CFG_VLAN_MEMBER_TABLE_NAME)
    {
        doVlanMemberTask(consumer);
    }
    else if (table_name == CFG_FDB_TABLE_NAME)
    {
        doFdbTask(consumer);
    }
    else if (table_name == CFG_SWITCH_TABLE_NAME)
    {
        SWSS_LOG_DEBUG("Table:SWITCH");
        doSwitchTask(consumer);
    }
    else
    {
        SWSS_LOG_ERROR("Unknown config table %s ", table_name.c_str());
        throw runtime_error("VlanMgr doTask failure.");
    }
}

void VlanMgr::doTask(NotificationConsumer &consumer)
{
	std::string op;
	std::string data;
	std::vector<swss::FieldValueTuple> values;

	consumer.pop(op, data, values);

	if (&consumer != m_VlanStateNotificationConsumer)
	{
		return;
	}

	int vlan_id = stoi(data.substr(4));

	SWSS_LOG_NOTICE("vlanmgr received port status notification state %s vlan %s id %d",
	                 op.c_str(), data.c_str(), vlan_id);

    if (isVlanStateOk(data)) {
        setHostVlanAdminState(vlan_id, op);
    }
    else
    {
        SWSS_LOG_ERROR("received state update for vlan %s not existing", data.c_str());
    }
}
