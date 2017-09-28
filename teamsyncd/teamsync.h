#ifndef __TEAMSYNC__
#define __TEAMSYNC__

#include <map>
#include <string>
#include <memory>
#include "dbconnector.h"
#include "producerstatetable.h"
#include "selectable.h"
#include "select.h"
#include "netmsg.h"
#include <team.h>

namespace swss {

class TeamSync : public NetMsg
{
public:
    TeamSync(DBConnector *db, Select *select);

    /*
     * Listens to RTM_NEWLINK and RTM_DELLINK to undestand if there is a new
     * team device
     */
    virtual void onMsg(int nlmsg_type, struct nl_object *obj);

    class TeamPortSync : public Selectable
    {
    public:
        enum { MAX_IFNAME = 64 };
        TeamPortSync(const std::string &lagName, int ifindex,
                     ProducerStateTable *lagMemberTable);
        ~TeamPortSync();

        virtual void addFd(fd_set *fd);
        virtual bool isMe(fd_set *fd);
        virtual int readCache();
        virtual void readMe();
        void syncMember(const std::string& intfName);

    protected:
        int onChange();
        static int teamdHandler(struct team_handle *th, void *arg,
                                team_change_type_mask_t type_mask);
        static const struct team_change_handler gPortChangeHandler;
    private:
        ProducerStateTable *m_lagMemberTable;
        struct team_handle *m_team;
        struct teamdctl *m_tdc;
        std::string m_lagName;
        int m_ifindex;
        std::map<std::string, bool> m_lagMembers; /* map[ifname] = status (enabled|disabled) */
    };

protected:
    void addLag(const std::string &lagName, int ifindex, bool admin_state,
                bool oper_state, unsigned int mtu);
    void removeLag(const std::string &lagName);

private:
    Select *m_select;
    ProducerStateTable m_lagTable;
    ProducerStateTable m_lagMemberTable;
    std::map<std::string, std::shared_ptr<TeamPortSync> > m_teamPorts;
};

}

#endif
