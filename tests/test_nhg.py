import os
import re
import sys
import time
import json
import pytest
import ipaddress

from swsscommon import swsscommon


class TestNextHopGroup(object):
    def test_route_nhg(self, dvs, testlog):
        config_db = dvs.get_config_db()
        fvs = {"NULL": "NULL"}
        config_db.create_entry("INTERFACE", "Ethernet0", fvs)
        config_db.create_entry("INTERFACE", "Ethernet4", fvs)
        config_db.create_entry("INTERFACE", "Ethernet8", fvs)
        config_db.create_entry("INTERFACE", "Ethernet0|10.0.0.0/31", fvs)
        config_db.create_entry("INTERFACE", "Ethernet4|10.0.0.2/31", fvs)
        config_db.create_entry("INTERFACE", "Ethernet8|10.0.0.4/31", fvs)
        dvs.runcmd("config interface startup Ethernet0")
        dvs.runcmd("config interface startup Ethernet4")
        dvs.runcmd("config interface startup Ethernet8")

        dvs.runcmd("arp -s 10.0.0.1 00:00:00:00:00:01")
        dvs.runcmd("arp -s 10.0.0.3 00:00:00:00:00:02")
        dvs.runcmd("arp -s 10.0.0.5 00:00:00:00:00:03")

        assert dvs.servers[0].runcmd("ip link set down dev eth0") == 0
        assert dvs.servers[1].runcmd("ip link set down dev eth0") == 0
        assert dvs.servers[2].runcmd("ip link set down dev eth0") == 0

        assert dvs.servers[0].runcmd("ip link set up dev eth0") == 0
        assert dvs.servers[1].runcmd("ip link set up dev eth0") == 0
        assert dvs.servers[2].runcmd("ip link set up dev eth0") == 0

        app_db = dvs.get_app_db()
        ps = swsscommon.ProducerStateTable(app_db.db_connection, "ROUTE_TABLE")

        asic_db = dvs.get_asic_db()
        asic_routes_count = len(asic_db.get_keys("ASIC_STATE:SAI_OBJECT_TYPE_ROUTE_ENTRY"))

        fvs = swsscommon.FieldValuePairs([("nexthop","10.0.0.1,10.0.0.3,10.0.0.5"), ("ifname", "Ethernet0,Ethernet4,Ethernet8")])
        ps.set("2.2.2.0/24", fvs)

        # check if route was propagated to ASIC DB

        asic_db.wait_for_n_keys("ASIC_STATE:SAI_OBJECT_TYPE_ROUTE_ENTRY", asic_routes_count + 1)
        keys = asic_db.get_keys("ASIC_STATE:SAI_OBJECT_TYPE_ROUTE_ENTRY")

        found_route = False
        for k in keys:
            rt_key = json.loads(k)

            if rt_key['dest'] == "2.2.2.0/24":
                found_route = True
                break

        assert found_route

        # assert the route points to next hop group
        fvs = asic_db.get_entry("ASIC_STATE:SAI_OBJECT_TYPE_ROUTE_ENTRY", k)

        nhgid = fvs["SAI_ROUTE_ENTRY_ATTR_NEXT_HOP_ID"]

        fvs = asic_db.get_entry("ASIC_STATE:SAI_OBJECT_TYPE_NEXT_HOP_GROUP", nhgid)

        assert bool(fvs)

        keys = asic_db.get_keys("ASIC_STATE:SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER")

        assert len(keys) == 3

        for k in keys:
            fvs = asic_db.get_entry("ASIC_STATE:SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER", k)

            assert fvs["SAI_NEXT_HOP_GROUP_MEMBER_ATTR_NEXT_HOP_GROUP_ID"] == nhgid

        # bring links down one-by-one
        for i in [0, 1, 2]:
            dvs.servers[i].runcmd("ip link set down dev eth0") == 0

            time.sleep(1)

            fvs = app_db.get_entry("PORT_TABLE", "Ethernet%d" % (i * 4))

            assert bool(fvs)
            assert fvs["oper_status"] == "down"

            keys = asic_db.get_keys("ASIC_STATE:SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER")

            assert len(keys) == 2 - i

        # bring links up one-by-one
        for i in [0, 1, 2]:
            dvs.servers[i].runcmd("ip link set up dev eth0") == 0

            time.sleep(1)

            fvs = app_db.get_entry("PORT_TABLE", "Ethernet%d" % (i * 4))

            assert bool(fvs)
            assert fvs["oper_status"] == "up"

            keys = asic_db.get_keys("ASIC_STATE:SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER")

            assert len(keys) == i + 1

            for k in keys:
                fvs = asic_db.get_entry("ASIC_STATE:SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER", k)
                assert fvs["SAI_NEXT_HOP_GROUP_MEMBER_ATTR_NEXT_HOP_GROUP_ID"] == nhgid

        # Remove route 2.2.2.0/24
        ps._del("2.2.2.0/24")

        # Wait for route 2.2.2.0/24 to be removed
        asic_db.wait_for_n_keys("ASIC_STATE:SAI_OBJECT_TYPE_ROUTE_ENTRY", asic_routes_count)

    def test_route_nhg_exhaust(self, dvs, testlog):
        """
        Test the situation of exhausting ECMP group, assume SAI_SWITCH_ATTR_NUMBER_OF_ECMP_GROUPS is 512

        In order to achieve that, we will config
            1. 9 ports
            2. 512 routes with different nexthop group

        See Also
        --------
        SwitchStateBase::set_number_of_ecmp_groups()
        https://github.com/Azure/sonic-sairedis/blob/master/vslib/src/SwitchStateBase.cpp

        """

        # TODO: check ECMP 512

        def port_name(i):
            return "Ethernet" + str(i * 4)

        def port_ip(i):
            return "10.0.0." + str(i * 2)

        def peer_ip(i):
            return "10.0.0." + str(i * 2 + 1)

        def port_ipprefix(i):
            return port_ip(i) + "/31"

        def port_mac(i):
            return "00:00:00:00:00:0" + str(i)

        def gen_ipprefix(r):
            """ Construct route like 2.X.X.0/24 """
            ip = ipaddress.IPv4Address(IP_INTEGER_BASE + r * 256)
            ip = str(ip)
            ipprefix = ip + "/24"
            return ipprefix

        def gen_nhg_fvs(binary):
            nexthop = []
            ifname = []
            for i in range(MAX_PORT_COUNT):
                if binary[i] == '1':
                    nexthop.append(peer_ip(i))
                    ifname.append(port_name(i))

            nexthop = ','.join(nexthop)
            ifname = ','.join(ifname)
            fvs = swsscommon.FieldValuePairs([("nexthop", nexthop), ("ifname", ifname)])
            return fvs

        def asic_route_exists(keys, ipprefix):
            for k in keys:
                rt_key = json.loads(k)

                if rt_key['dest'] == ipprefix:
                    return k
            else:
                return None

        def asic_route_nhg_fvs(k):
            fvs = asic_db.get_entry("ASIC_STATE:SAI_OBJECT_TYPE_ROUTE_ENTRY", k)
            if not fvs:
                return None

            print(fvs)
            nhgid = fvs.get("SAI_ROUTE_ENTRY_ATTR_NEXT_HOP_ID")
            if nhgid is None:
                return None

            fvs = asic_db.get_entry("ASIC_STATE:SAI_OBJECT_TYPE_NEXT_HOP_GROUP", nhgid)
            return fvs

        MAX_ECMP_COUNT = 512
        MAX_PORT_COUNT = 10
        if sys.version_info < (3, 0):
            IP_INTEGER_BASE = int(ipaddress.IPv4Address(unicode("2.2.2.0")))
        else:
            IP_INTEGER_BASE = int(ipaddress.IPv4Address(str("2.2.2.0")))

        config_db = dvs.get_config_db()
        fvs = {"NULL": "NULL"}

        for i in range(MAX_PORT_COUNT):
            config_db.create_entry("INTERFACE", port_name(i), fvs)
            config_db.create_entry("INTERFACE", "{}|{}".format(port_name(i), port_ipprefix(i)), fvs)
            dvs.runcmd("config interface startup " + port_name(i))
            dvs.runcmd("arp -s {} {}".format(peer_ip(i), port_mac(i)))
            assert dvs.servers[i].runcmd("ip link set down dev eth0") == 0
            assert dvs.servers[i].runcmd("ip link set up dev eth0") == 0

        app_db = dvs.get_app_db()
        asic_db = dvs.get_asic_db()
        ps = swsscommon.ProducerStateTable(app_db.db_connection, "ROUTE_TABLE")

        # Add first batch of routes with unique nexthop groups in AppDB
        route_count = 0
        r = 0
        asic_routes_count = len(asic_db.get_keys("ASIC_STATE:SAI_OBJECT_TYPE_ROUTE_ENTRY"))
        while route_count < MAX_ECMP_COUNT:
            r += 1
            fmt = '{{0:0{}b}}'.format(MAX_PORT_COUNT)
            binary = fmt.format(r)
            # We need at least 2 ports for a nexthop group
            if binary.count('1') <= 1:
                continue
            fvs = gen_nhg_fvs(binary)
            route_ipprefix = gen_ipprefix(route_count)
            ps.set(route_ipprefix, fvs)
            route_count += 1

        # Wait and check ASIC DB the count of nexthop groups used
        asic_db.wait_for_n_keys("ASIC_STATE:SAI_OBJECT_TYPE_NEXT_HOP_GROUP", MAX_ECMP_COUNT)

        # Wait and check ASIC DB the count of routes
        asic_db.wait_for_n_keys("ASIC_STATE:SAI_OBJECT_TYPE_ROUTE_ENTRY", asic_routes_count + MAX_ECMP_COUNT)
        asic_routes_count += MAX_ECMP_COUNT

        # Add second batch of routes with unique nexthop groups in AppDB
        # Add more routes with new nexthop group in AppDBdd
        route_ipprefix = gen_ipprefix(route_count)
        base_ipprefix = route_ipprefix
        base = route_count
        route_count = 0
        while route_count < 10:
            r += 1
            fmt = '{{0:0{}b}}'.format(MAX_PORT_COUNT)
            binary = fmt.format(r)
            # We need at least 2 ports for a nexthop group
            if binary.count('1') <= 1:
                continue
            fvs = gen_nhg_fvs(binary)
            route_ipprefix = gen_ipprefix(base + route_count)
            ps.set(route_ipprefix, fvs)
            route_count += 1
        last_ipprefix = route_ipprefix

        # Wait until we get expected routes and check ASIC DB on the count of nexthop groups used, and it should not increase
        keys = asic_db.wait_for_n_keys("ASIC_STATE:SAI_OBJECT_TYPE_ROUTE_ENTRY", asic_routes_count + 10)
        asic_db.wait_for_n_keys("ASIC_STATE:SAI_OBJECT_TYPE_NEXT_HOP_GROUP", MAX_ECMP_COUNT)

        # Check the route points to next hop group
        # Note: no need to wait here
        k = asic_route_exists(keys, "2.2.2.0/24")
        assert k is not None
        fvs = asic_route_nhg_fvs(k)
        assert fvs is not None

        # Check the second batch does not point to next hop group
        k = asic_route_exists(keys, base_ipprefix)
        assert k is not None
        fvs = asic_route_nhg_fvs(k)
        assert not(fvs)

        # Remove first batch of routes with unique nexthop groups in AppDB
        route_count = 0
        r = 0
        while route_count < MAX_ECMP_COUNT:
            r += 1
            fmt = '{{0:0{}b}}'.format(MAX_PORT_COUNT)
            binary = fmt.format(r)
            # We need at least 2 ports for a nexthop group
            if binary.count('1') <= 1:
                continue
            route_ipprefix = gen_ipprefix(route_count)
            ps._del(route_ipprefix)
            route_count += 1

        # Wait and check the second batch points to next hop group
        # Check ASIC DB on the count of nexthop groups used, and it should not increase or decrease
        asic_db.wait_for_n_keys("ASIC_STATE:SAI_OBJECT_TYPE_NEXT_HOP_GROUP", 10)
        keys = asic_db.get_keys("ASIC_STATE:SAI_OBJECT_TYPE_ROUTE_ENTRY")
        k = asic_route_exists(keys, base_ipprefix)
        assert k is not None
        fvs = asic_route_nhg_fvs(k)
        assert fvs is not None
        k = asic_route_exists(keys, last_ipprefix)
        assert k is not None
        fvs = asic_route_nhg_fvs(k)
        assert fvs is not None


# Add Dummy always-pass test at end as workaroud
# for issue when Flaky fail on final test it invokes module tear-down before retrying
def test_nonflaky_dummy():
    pass
