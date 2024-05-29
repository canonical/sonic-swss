#include "ut_helpers_fpmsyncd.h"
#include "gtest/gtest.h"
#include <gmock/gmock.h>
#include "mock_table.h"
#include <net/if.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include "ipaddress.h"

#define private public // Need to modify internal cache
#include "fpmlink.h"
#include "routesync.h"
#undef private

using namespace swss;
using namespace testing;

/*
Test Fixture
*/
namespace ut_fpmsyncd
{
    struct FpmSyncdSRv6LocalSIDsTest : public ::testing::Test
    {
        std::shared_ptr<swss::DBConnector> m_app_db;
        std::shared_ptr<swss::RedisPipeline> pipeline;
        std::shared_ptr<RouteSync> m_routeSync;
        std::shared_ptr<FpmLink> m_fpmLink;
        std::shared_ptr<swss::Table> m_srv6LocalSidTable;

        virtual void SetUp() override
        {
            testing_db::reset();

            m_app_db = std::make_shared<swss::DBConnector>("APPL_DB", 0);

            /* Construct dependencies */

            /*  1) RouteSync */
            pipeline = std::make_shared<swss::RedisPipeline>(m_app_db.get());
            m_routeSync = std::make_shared<RouteSync>(pipeline.get());

            /* 2) FpmLink */
            m_fpmLink = std::make_shared<FpmLink>(m_routeSync.get());

            /* 3) SRV6_MY_SID_TABLE in APP_DB */
            m_srv6LocalSidTable = std::make_shared<swss::Table>(m_app_db.get(), APP_SRV6_MY_SID_TABLE_NAME);
        }

        virtual void TearDown() override
        {
        }
    };
}

namespace ut_fpmsyncd
{
    /* Test Receiving a route containing an SRv6 Local SID nexthop bound to the End behavior */
    TEST_F(FpmSyncdSRv6LocalSIDsTest, RecevingRouteWithSRv6LocalSIDEnd)
    {
        ASSERT_NE(m_routeSync, nullptr);

        /* Create a Netlink object containing an SRv6 Local SID */
        IpAddress _localsid = IpAddress("fc00:0:1:1::");
        uint8_t _block_len = 32;
        uint8_t _node_len = 16;
        uint8_t _func_len = 16;
        uint8_t _arg_len = 0;
        uint32_t _action = SRV6_LOCALSID_ACTION_END;

        struct nlmsg *nl_obj = create_srv6_localsid_nlmsg(RTM_NEWSRV6LOCALSID, &_localsid, _block_len, _node_len, _func_len, _arg_len, _action);
        if (!nl_obj)
            printf("Error\n\n");

        /* Send the Netlink object to the FpmLink */
        m_fpmLink->processRawMsg(&nl_obj->n);

        /* Check that fpmsyncd created the correct entries in APP_DB */
        std::string action;
        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "action", action), true);
        ASSERT_EQ(action, "end");

        /* Destroy the Netlink object and free the memory */
        free_nlobj(nl_obj);


        nl_obj = create_srv6_localsid_nlmsg(RTM_DELSRV6LOCALSID, &_localsid, _block_len, _node_len, _func_len, _arg_len, _action);
        if (!nl_obj)
            printf("Error\n\n");

        /* Send the Netlink object to the FpmLink */
        m_fpmLink->processRawMsg(&nl_obj->n);

        /* Check that fpmsyncd created the correct entries in APP_DB */
        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "action", action), false);

        /* Destroy the Netlink object and free the memory */
        free_nlobj(nl_obj);
    }

    /* Test Receiving a route containing an SRv6 Local SID nexthop bound to the End.X behavior */
    TEST_F(FpmSyncdSRv6LocalSIDsTest, RecevingRouteWithSRv6LocalSIDEndX)
    {
        ASSERT_NE(m_routeSync, nullptr);

        /* Create a Netlink object containing an SRv6 Local SID */
        IpAddress _localsid = IpAddress("fc00:0:1:1::");
        uint8_t _block_len = 32;
        uint8_t _node_len = 16;
        uint8_t _func_len = 16;
        uint8_t _arg_len = 0;
        uint32_t _action = SRV6_LOCALSID_ACTION_END_X;
        IpAddress _adj = IpAddress("2001:db8:1::1");

        struct nlmsg *nl_obj = create_srv6_localsid_nlmsg(RTM_NEWSRV6LOCALSID, &_localsid, _block_len, _node_len, _func_len, _arg_len, _action, NULL, &_adj);
        if (!nl_obj)
            printf("Error\n\n");

        /* Send the Netlink object to the FpmLink */
        m_fpmLink->processRawMsg(&nl_obj->n);

        /* Check that fpmsyncd created the correct entries in APP_DB */
        std::string action;
        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "action", action), true);
        ASSERT_EQ(action, "end.x");

        std::string adj;
        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "adj", adj), true);
        ASSERT_EQ(adj, "2001:db8:1::1");

        /* Destroy the Netlink object and free the memory */
        free_nlobj(nl_obj);


        nl_obj = create_srv6_localsid_nlmsg(RTM_DELSRV6LOCALSID, &_localsid, _block_len, _node_len, _func_len, _arg_len, _action, NULL, &_adj);
        if (!nl_obj)
            printf("Error\n\n");

        /* Send the Netlink object to the FpmLink */
        m_fpmLink->processRawMsg(&nl_obj->n);

        /* Check that fpmsyncd created the correct entries in APP_DB */
        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "action", action), false);

        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "adj", adj), false);

        /* Destroy the Netlink object and free the memory */
        free_nlobj(nl_obj);
    }

    /* Test Receiving a route containing an SRv6 Local SID nexthop bound to the End.T behavior */
    TEST_F(FpmSyncdSRv6LocalSIDsTest, RecevingRouteWithSRv6LocalSIDEndT)
    {
        ASSERT_NE(m_routeSync, nullptr);

        /* Create a Netlink object containing an SRv6 Local SID */
        IpAddress _localsid = IpAddress("fc00:0:1:1::");
        uint8_t _block_len = 32;
        uint8_t _node_len = 16;
        uint8_t _func_len = 16;
        uint8_t _arg_len = 0;
        uint32_t _action = SRV6_LOCALSID_ACTION_END_T;
        char *_vrf = "Vrf10";

        struct nlmsg *nl_obj = create_srv6_localsid_nlmsg(RTM_NEWSRV6LOCALSID, &_localsid, _block_len, _node_len, _func_len, _arg_len, _action, _vrf);
        if (!nl_obj)
            printf("Error\n\n");

        /* Send the Netlink object to the FpmLink */
        m_fpmLink->processRawMsg(&nl_obj->n);

        /* Check that fpmsyncd created the correct entries in APP_DB */
        std::string action;
        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "action", action), true);
        ASSERT_EQ(action, "end.t");

        std::string vrf;
        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "vrf", vrf), true);
        ASSERT_EQ(vrf, "Vrf10");

        /* Destroy the Netlink object and free the memory */
        free_nlobj(nl_obj);


        nl_obj = create_srv6_localsid_nlmsg(RTM_DELSRV6LOCALSID, &_localsid, _block_len, _node_len, _func_len, _arg_len, _action, _vrf);
        if (!nl_obj)
            printf("Error\n\n");

        /* Send the Netlink object to the FpmLink */
        m_fpmLink->processRawMsg(&nl_obj->n);

        /* Check that fpmsyncd created the correct entries in APP_DB */
        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "action", action), false);

        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "vrf", vrf), false);

        /* Destroy the Netlink object and free the memory */
        free_nlobj(nl_obj);
    }

    /* Test Receiving a route containing an SRv6 Local SID nexthop bound to the End.DX6 behavior */
    TEST_F(FpmSyncdSRv6LocalSIDsTest, RecevingRouteWithSRv6LocalSIDEndDX6)
    {
        ASSERT_NE(m_routeSync, nullptr);

        /* Create a Netlink object containing an SRv6 Local SID */
        IpAddress _localsid = IpAddress("fc00:0:1:1::");
        uint8_t _block_len = 32;
        uint8_t _node_len = 16;
        uint8_t _func_len = 16;
        uint8_t _arg_len = 0;
        uint32_t _action = SRV6_LOCALSID_ACTION_END_DX6;
        IpAddress _adj = IpAddress("2001:db8:1::1");

        struct nlmsg *nl_obj = create_srv6_localsid_nlmsg(RTM_NEWSRV6LOCALSID, &_localsid, _block_len, _node_len, _func_len, _arg_len, _action, NULL, &_adj);
        if (!nl_obj)
            printf("Error\n\n");

        /* Send the Netlink object to the FpmLink */
        m_fpmLink->processRawMsg(&nl_obj->n);

        /* Check that fpmsyncd created the correct entries in APP_DB */
        std::string action;
        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "action", action), true);
        ASSERT_EQ(action, "end.dx6");

        std::string adj;
        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "adj", adj), true);
        ASSERT_EQ(adj, "2001:db8:1::1");

        /* Destroy the Netlink object and free the memory */
        free_nlobj(nl_obj);


        nl_obj = create_srv6_localsid_nlmsg(RTM_DELSRV6LOCALSID, &_localsid, _block_len, _node_len, _func_len, _arg_len, _action, NULL, &_adj);
        if (!nl_obj)
            printf("Error\n\n");

        /* Send the Netlink object to the FpmLink */
        m_fpmLink->processRawMsg(&nl_obj->n);

        /* Check that fpmsyncd created the correct entries in APP_DB */
        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "action", action), false);

        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "adj", adj), false);
        /* Destroy the Netlink object and free the memory */
        free_nlobj(nl_obj);
    }

    /* Test Receiving a route containing an SRv6 Local SID nexthop bound to the End.DX4 behavior */
    TEST_F(FpmSyncdSRv6LocalSIDsTest, RecevingRouteWithSRv6LocalSIDEndDX4)
    {
        ASSERT_NE(m_routeSync, nullptr);

        /* Create a Netlink object containing an SRv6 Local SID */
        IpAddress _localsid = IpAddress("fc00:0:1:1::");
        uint8_t _block_len = 32;
        uint8_t _node_len = 16;
        uint8_t _func_len = 16;
        uint8_t _arg_len = 0;
        uint32_t _action = SRV6_LOCALSID_ACTION_END_DX4;
        IpAddress _adj = IpAddress("10.0.0.1");

        struct nlmsg *nl_obj = create_srv6_localsid_nlmsg(RTM_NEWSRV6LOCALSID, &_localsid, _block_len, _node_len, _func_len, _arg_len, _action, NULL, &_adj);
        if (!nl_obj)
            printf("Error\n\n");

        /* Send the Netlink object to the FpmLink */
        m_fpmLink->processRawMsg(&nl_obj->n);

        /* Check that fpmsyncd created the correct entries in APP_DB */
        std::string action;
        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "action", action), true);
        ASSERT_EQ(action, "end.dx4");

        std::string adj;
        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "adj", adj), true);
        ASSERT_EQ(adj, "10.0.0.1");

        /* Destroy the Netlink object and free the memory */
        free_nlobj(nl_obj);


        nl_obj = create_srv6_localsid_nlmsg(RTM_DELSRV6LOCALSID, &_localsid, _block_len, _node_len, _func_len, _arg_len, _action, NULL, &_adj);
        if (!nl_obj)
            printf("Error\n\n");

        /* Send the Netlink object to the FpmLink */
        m_fpmLink->processRawMsg(&nl_obj->n);

        /* Check that fpmsyncd created the correct entries in APP_DB */
        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "action", action), false);

        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "adj", adj), false);

        /* Destroy the Netlink object and free the memory */
        free_nlobj(nl_obj);
    }

    /* Test Receiving a route containing an SRv6 Local SID nexthop bound to the End.DT4 behavior */
    TEST_F(FpmSyncdSRv6LocalSIDsTest, RecevingRouteWithSRv6LocalSIDEndDT4)
    {
        ASSERT_NE(m_routeSync, nullptr);

        /* Create a Netlink object containing an SRv6 Local SID */
        IpAddress _localsid = IpAddress("fc00:0:1:1::");
        uint8_t _block_len = 32;
        uint8_t _node_len = 16;
        uint8_t _func_len = 16;
        uint8_t _arg_len = 0;
        uint32_t _action = SRV6_LOCALSID_ACTION_END_DT4;
        char *_vrf = "Vrf10";

        struct nlmsg *nl_obj = create_srv6_localsid_nlmsg(RTM_NEWSRV6LOCALSID, &_localsid, _block_len, _node_len, _func_len, _arg_len, _action, _vrf);
        if (!nl_obj)
            printf("Error\n\n");

        /* Send the Netlink object to the FpmLink */
        m_fpmLink->processRawMsg(&nl_obj->n);

        /* Check that fpmsyncd created the correct entries in APP_DB */
        std::string action;
        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "action", action), true);
        ASSERT_EQ(action, "end.dt4");

        std::string vrf;
        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "vrf", vrf), true);
        ASSERT_EQ(vrf, "Vrf10");

        /* Destroy the Netlink object and free the memory */
        free_nlobj(nl_obj);


        nl_obj = create_srv6_localsid_nlmsg(RTM_DELSRV6LOCALSID, &_localsid, _block_len, _node_len, _func_len, _arg_len, _action, _vrf);
        if (!nl_obj)
            printf("Error\n\n");

        /* Send the Netlink object to the FpmLink */
        m_fpmLink->processRawMsg(&nl_obj->n);

        /* Check that fpmsyncd created the correct entries in APP_DB */
        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "action", action), false);

        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "vrf", vrf), false);

        /* Destroy the Netlink object and free the memory */
        free_nlobj(nl_obj);
    }

    /* Test Receiving a route containing an SRv6 Local SID nexthop bound to the End.DT6 behavior */
    TEST_F(FpmSyncdSRv6LocalSIDsTest, RecevingRouteWithSRv6LocalSIDEndDT6)
    {
        ASSERT_NE(m_routeSync, nullptr);

        /* Create a Netlink object containing an SRv6 Local SID */
        IpAddress _localsid = IpAddress("fc00:0:1:1::");
        uint8_t _block_len = 32;
        uint8_t _node_len = 16;
        uint8_t _func_len = 16;
        uint8_t _arg_len = 0;
        uint32_t _action = SRV6_LOCALSID_ACTION_END_DT6;
        char *_vrf = "Vrf10";

        struct nlmsg *nl_obj = create_srv6_localsid_nlmsg(RTM_NEWSRV6LOCALSID, &_localsid, _block_len, _node_len, _func_len, _arg_len, _action, _vrf);
        if (!nl_obj)
            printf("Error\n\n");

        /* Send the Netlink object to the FpmLink */
        m_fpmLink->processRawMsg(&nl_obj->n);

        /* Check that fpmsyncd created the correct entries in APP_DB */
        std::string action;
        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "action", action), true);
        ASSERT_EQ(action, "end.dt6");

        std::string vrf;
        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "vrf", vrf), true);
        ASSERT_EQ(vrf, "Vrf10");

        /* Destroy the Netlink object and free the memory */
        free_nlobj(nl_obj);


        nl_obj = create_srv6_localsid_nlmsg(RTM_DELSRV6LOCALSID, &_localsid, _block_len, _node_len, _func_len, _arg_len, _action, _vrf);
        if (!nl_obj)
            printf("Error\n\n");

        /* Send the Netlink object to the FpmLink */
        m_fpmLink->processRawMsg(&nl_obj->n);

        /* Check that fpmsyncd created the correct entries in APP_DB */
        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "action", action), false);

        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "vrf", vrf), false);

        /* Destroy the Netlink object and free the memory */
        free_nlobj(nl_obj);
    }

    /* Test Receiving a route containing an SRv6 Local SID nexthop bound to the End.DT46 behavior */
    TEST_F(FpmSyncdSRv6LocalSIDsTest, RecevingRouteWithSRv6LocalSIDEndDT46)
    {
        ASSERT_NE(m_routeSync, nullptr);

        /* Create a Netlink object containing an SRv6 Local SID */
        IpAddress _localsid = IpAddress("fc00:0:1:1::");
        uint8_t _block_len = 32;
        uint8_t _node_len = 16;
        uint8_t _func_len = 16;
        uint8_t _arg_len = 0;
        uint32_t _action = SRV6_LOCALSID_ACTION_END_DT46;
        char *_vrf = "Vrf10";

        struct nlmsg *nl_obj = create_srv6_localsid_nlmsg(RTM_NEWSRV6LOCALSID, &_localsid, _block_len, _node_len, _func_len, _arg_len, _action, _vrf);
        if (!nl_obj)
            printf("Error\n\n");

        /* Send the Netlink object to the FpmLink */
        m_fpmLink->processRawMsg(&nl_obj->n);

        /* Check that fpmsyncd created the correct entries in APP_DB */
        std::string action;
        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "action", action), true);
        ASSERT_EQ(action, "end.dt46");

        std::string vrf;
        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "vrf", vrf), true);
        ASSERT_EQ(vrf, "Vrf10");

        /* Destroy the Netlink object and free the memory */
        free_nlobj(nl_obj);


        nl_obj = create_srv6_localsid_nlmsg(RTM_DELSRV6LOCALSID, &_localsid, _block_len, _node_len, _func_len, _arg_len, _action, _vrf);
        if (!nl_obj)
            printf("Error\n\n");

        /* Send the Netlink object to the FpmLink */
        m_fpmLink->processRawMsg(&nl_obj->n);

        /* Check that fpmsyncd created the correct entries in APP_DB */
        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "action", action), false);

        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "vrf", vrf), false);

        /* Destroy the Netlink object and free the memory */
        free_nlobj(nl_obj);
    }

    /* Test Receiving a route containing an SRv6 Local SID nexthop bound to the uN behavior */
    TEST_F(FpmSyncdSRv6LocalSIDsTest, RecevingRouteWithSRv6LocalSIDUN)
    {
        ASSERT_NE(m_routeSync, nullptr);

        /* Create a Netlink object containing an SRv6 Local SID */
        IpAddress _localsid = IpAddress("fc00:0:1:1::");
        uint8_t _block_len = 32;
        uint8_t _node_len = 16;
        uint8_t _func_len = 16;
        uint8_t _arg_len = 0;
        uint32_t _action = SRV6_LOCALSID_ACTION_UN;

        struct nlmsg *nl_obj = create_srv6_localsid_nlmsg(RTM_NEWSRV6LOCALSID, &_localsid, _block_len, _node_len, _func_len, _arg_len, _action);
        if (!nl_obj)
            printf("Error\n\n");

        /* Send the Netlink object to the FpmLink */
        m_fpmLink->processRawMsg(&nl_obj->n);

        /* Check that fpmsyncd created the correct entries in APP_DB */
        std::string action;
        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "action", action), true);
        ASSERT_EQ(action, "un");

        /* Destroy the Netlink object and free the memory */
        free_nlobj(nl_obj);


        nl_obj = create_srv6_localsid_nlmsg(RTM_DELSRV6LOCALSID, &_localsid, _block_len, _node_len, _func_len, _arg_len, _action);
        if (!nl_obj)
            printf("Error\n\n");

        /* Send the Netlink object to the FpmLink */
        m_fpmLink->processRawMsg(&nl_obj->n);

        /* Check that fpmsyncd created the correct entries in APP_DB */
        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "action", action), false);

        /* Destroy the Netlink object and free the memory */
        free_nlobj(nl_obj);
    }

    /* Test Receiving a route containing an SRv6 Local SID nexthop bound to the uA behavior */
    TEST_F(FpmSyncdSRv6LocalSIDsTest, RecevingRouteWithSRv6LocalSIDUA)
    {
        ASSERT_NE(m_routeSync, nullptr);

        /* Create a Netlink object containing an SRv6 Local SID */
        IpAddress _localsid = IpAddress("fc00:0:1:1::");
        uint8_t _block_len = 32;
        uint8_t _node_len = 16;
        uint8_t _func_len = 16;
        uint8_t _arg_len = 0;
        uint32_t _action = SRV6_LOCALSID_ACTION_UA;
        IpAddress _adj = IpAddress("2001:db8:1::1");

        struct nlmsg *nl_obj = create_srv6_localsid_nlmsg(RTM_NEWSRV6LOCALSID, &_localsid, _block_len, _node_len, _func_len, _arg_len, _action, NULL, &_adj);
        if (!nl_obj)
            printf("Error\n\n");

        /* Send the Netlink object to the FpmLink */
        m_fpmLink->processRawMsg(&nl_obj->n);

        /* Check that fpmsyncd created the correct entries in APP_DB */
        std::string action;
        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "action", action), true);
        ASSERT_EQ(action, "ua");

        std::string adj;
        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "adj", adj), true);
        ASSERT_EQ(adj, "2001:db8:1::1");

        /* Destroy the Netlink object and free the memory */
        free_nlobj(nl_obj);


        nl_obj = create_srv6_localsid_nlmsg(RTM_DELSRV6LOCALSID, &_localsid, _block_len, _node_len, _func_len, _arg_len, _action, NULL, &_adj);
        if (!nl_obj)
            printf("Error\n\n");

        /* Send the Netlink object to the FpmLink */
        m_fpmLink->processRawMsg(&nl_obj->n);

        /* Check that fpmsyncd created the correct entries in APP_DB */
        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "action", action), false);

        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "adj", adj), false);

        /* Destroy the Netlink object and free the memory */
        free_nlobj(nl_obj);
    }

    /* Test Receiving a route containing an SRv6 Local SID nexthop bound to the uDX6 behavior */
    TEST_F(FpmSyncdSRv6LocalSIDsTest, RecevingRouteWithSRv6LocalSIDUDX6)
    {
        ASSERT_NE(m_routeSync, nullptr);

        /* Create a Netlink object containing an SRv6 Local SID */
        IpAddress _localsid = IpAddress("fc00:0:1:1::");
        uint8_t _block_len = 32;
        uint8_t _node_len = 16;
        uint8_t _func_len = 16;
        uint8_t _arg_len = 0;
        uint32_t _action = SRV6_LOCALSID_ACTION_UDX6;
        IpAddress _adj = IpAddress("2001:db8:1::1");

        struct nlmsg *nl_obj = create_srv6_localsid_nlmsg(RTM_NEWSRV6LOCALSID, &_localsid, _block_len, _node_len, _func_len, _arg_len, _action, NULL, &_adj);
        if (!nl_obj)
            printf("Error\n\n");

        /* Send the Netlink object to the FpmLink */
        m_fpmLink->processRawMsg(&nl_obj->n);

        /* Check that fpmsyncd created the correct entries in APP_DB */
        std::string action;
        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "action", action), true);
        ASSERT_EQ(action, "udx6");

        std::string adj;
        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "adj", adj), true);
        ASSERT_EQ(adj, "2001:db8:1::1");

        /* Destroy the Netlink object and free the memory */
        free_nlobj(nl_obj);


        nl_obj = create_srv6_localsid_nlmsg(RTM_DELSRV6LOCALSID, &_localsid, _block_len, _node_len, _func_len, _arg_len, _action, NULL, &_adj);
        if (!nl_obj)
            printf("Error\n\n");

        /* Send the Netlink object to the FpmLink */
        m_fpmLink->processRawMsg(&nl_obj->n);

        /* Check that fpmsyncd created the correct entries in APP_DB */
        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "action", action), false);

        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "adj", adj), false);

        /* Destroy the Netlink object and free the memory */
        free_nlobj(nl_obj);
    }

    /* Test Receiving a route containing an SRv6 Local SID nexthop bound to the uDX4 behavior */
    TEST_F(FpmSyncdSRv6LocalSIDsTest, RecevingRouteWithSRv6LocalSIDUDX4)
    {
        ASSERT_NE(m_routeSync, nullptr);

        /* Create a Netlink object containing an SRv6 Local SID */
        IpAddress _localsid = IpAddress("fc00:0:1:1::");
        uint8_t _block_len = 32;
        uint8_t _node_len = 16;
        uint8_t _func_len = 16;
        uint8_t _arg_len = 0;
        uint32_t _action = SRV6_LOCALSID_ACTION_UDX4;
        IpAddress _adj = IpAddress("10.0.0.1");

        struct nlmsg *nl_obj = create_srv6_localsid_nlmsg(RTM_NEWSRV6LOCALSID, &_localsid, _block_len, _node_len, _func_len, _arg_len, _action, NULL, &_adj);
        if (!nl_obj)
            printf("Error\n\n");

        /* Send the Netlink object to the FpmLink */
        m_fpmLink->processRawMsg(&nl_obj->n);

        /* Check that fpmsyncd created the correct entries in APP_DB */
        std::string action;
        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "action", action), true);
        ASSERT_EQ(action, "udx4");

        std::string adj;
        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "adj", adj), true);
        ASSERT_EQ(adj, "10.0.0.1");

        /* Destroy the Netlink object and free the memory */
        free_nlobj(nl_obj);


        nl_obj = create_srv6_localsid_nlmsg(RTM_DELSRV6LOCALSID, &_localsid, _block_len, _node_len, _func_len, _arg_len, _action, NULL, &_adj);
        if (!nl_obj)
            printf("Error\n\n");

        /* Send the Netlink object to the FpmLink */
        m_fpmLink->processRawMsg(&nl_obj->n);

        /* Check that fpmsyncd created the correct entries in APP_DB */
        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "action", action), false);

        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "adj", adj), false);

        /* Destroy the Netlink object and free the memory */
        free_nlobj(nl_obj);
    }

    /* Test Receiving a route containing an SRv6 Local SID nexthop bound to the uDT4 behavior */
    TEST_F(FpmSyncdSRv6LocalSIDsTest, RecevingRouteWithSRv6LocalSIDUDT4)
    {
        ASSERT_NE(m_routeSync, nullptr);

        /* Create a Netlink object containing an SRv6 Local SID */
        IpAddress _localsid = IpAddress("fc00:0:1:1::");
        uint8_t _block_len = 32;
        uint8_t _node_len = 16;
        uint8_t _func_len = 16;
        uint8_t _arg_len = 0;
        uint32_t _action = SRV6_LOCALSID_ACTION_UDT4;
        char *_vrf = "Vrf10";

        struct nlmsg *nl_obj = create_srv6_localsid_nlmsg(RTM_NEWSRV6LOCALSID, &_localsid, _block_len, _node_len, _func_len, _arg_len, _action, _vrf);
        if (!nl_obj)
            printf("Error\n\n");

        /* Send the Netlink object to the FpmLink */
        m_fpmLink->processRawMsg(&nl_obj->n);

        /* Check that fpmsyncd created the correct entries in APP_DB */
        std::string action;
        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "action", action), true);
        ASSERT_EQ(action, "udt4");

        std::string vrf;
        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "vrf", vrf), true);
        ASSERT_EQ(vrf, "Vrf10");

        /* Destroy the Netlink object and free the memory */
        free_nlobj(nl_obj);


        nl_obj = create_srv6_localsid_nlmsg(RTM_DELSRV6LOCALSID, &_localsid, _block_len, _node_len, _func_len, _arg_len, _action, _vrf);
        if (!nl_obj)
            printf("Error\n\n");

        /* Send the Netlink object to the FpmLink */
        m_fpmLink->processRawMsg(&nl_obj->n);

        /* Check that fpmsyncd created the correct entries in APP_DB */
        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "action", action), false);

        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "vrf", vrf), false);

        /* Destroy the Netlink object and free the memory */
        free_nlobj(nl_obj);
    }

    /* Test Receiving a route containing an SRv6 Local SID nexthop bound to the uDT6 behavior */
    TEST_F(FpmSyncdSRv6LocalSIDsTest, RecevingRouteWithSRv6LocalSIDUDT6)
    {
        ASSERT_NE(m_routeSync, nullptr);

        /* Create a Netlink object containing an SRv6 Local SID */
        IpAddress _localsid = IpAddress("fc00:0:1:1::");
        uint8_t _block_len = 32;
        uint8_t _node_len = 16;
        uint8_t _func_len = 16;
        uint8_t _arg_len = 0;
        uint32_t _action = SRV6_LOCALSID_ACTION_UDT6;
        char *_vrf = "Vrf10";

        struct nlmsg *nl_obj = create_srv6_localsid_nlmsg(RTM_NEWSRV6LOCALSID, &_localsid, _block_len, _node_len, _func_len, _arg_len, _action, _vrf);
        if (!nl_obj)
            printf("Error\n\n");

        /* Send the Netlink object to the FpmLink */
        m_fpmLink->processRawMsg(&nl_obj->n);

        /* Check that fpmsyncd created the correct entries in APP_DB */
        std::string action;
        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "action", action), true);
        ASSERT_EQ(action, "udt6");

        std::string vrf;
        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "vrf", vrf), true);
        ASSERT_EQ(vrf, "Vrf10");

        /* Destroy the Netlink object and free the memory */
        free_nlobj(nl_obj);


        nl_obj = create_srv6_localsid_nlmsg(RTM_DELSRV6LOCALSID, &_localsid, _block_len, _node_len, _func_len, _arg_len, _action, _vrf);
        if (!nl_obj)
            printf("Error\n\n");

        /* Send the Netlink object to the FpmLink */
        m_fpmLink->processRawMsg(&nl_obj->n);

        /* Check that fpmsyncd created the correct entries in APP_DB */
        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "action", action), false);

        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "vrf", vrf), false);

        /* Destroy the Netlink object and free the memory */
        free_nlobj(nl_obj);
    }

    /* Test Receiving a route containing an SRv6 Local SID nexthop bound to the uDT46 behavior */
    TEST_F(FpmSyncdSRv6LocalSIDsTest, RecevingRouteWithSRv6LocalSIDUDT46)
    {
        ASSERT_NE(m_routeSync, nullptr);

        /* Create a Netlink object containing an SRv6 Local SID */
        IpAddress _localsid = IpAddress("fc00:0:1:1::");
        uint8_t _block_len = 32;
        uint8_t _node_len = 16;
        uint8_t _func_len = 16;
        uint8_t _arg_len = 0;
        uint32_t _action = SRV6_LOCALSID_ACTION_UDT46;
        char *_vrf = "Vrf10";

        struct nlmsg *nl_obj = create_srv6_localsid_nlmsg(RTM_NEWSRV6LOCALSID, &_localsid, _block_len, _node_len, _func_len, _arg_len, _action, _vrf);
        if (!nl_obj)
            printf("Error\n\n");

        /* Send the Netlink object to the FpmLink */
        m_fpmLink->processRawMsg(&nl_obj->n);

        /* Check that fpmsyncd created the correct entries in APP_DB */
        std::string action;
        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "action", action), true);
        ASSERT_EQ(action, "udt46");

        std::string vrf;
        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "vrf", vrf), true);
        ASSERT_EQ(vrf, "Vrf10");

        /* Destroy the Netlink object and free the memory */
        free_nlobj(nl_obj);


        nl_obj = create_srv6_localsid_nlmsg(RTM_DELSRV6LOCALSID, &_localsid, _block_len, _node_len, _func_len, _arg_len, _action, _vrf);
        if (!nl_obj)
            printf("Error\n\n");

        /* Send the Netlink object to the FpmLink */
        m_fpmLink->processRawMsg(&nl_obj->n);

        /* Check that fpmsyncd created the correct entries in APP_DB */
        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "action", action), false);

        ASSERT_EQ(m_srv6LocalSidTable->hget("32:16:16:0:fc00:0:1:1::", "vrf", vrf), false);

        /* Destroy the Netlink object and free the memory */
        free_nlobj(nl_obj);
    }
}