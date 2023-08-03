import time
import pytest

from dvslib.dvs_common import wait_for_result
from swsscommon import swsscommon

L3_TABLE_TYPE = "L3"
L3_TABLE_NAME = "L3_TEST"
L3_BIND_PORTS = ["Ethernet0"]
L3_RULE_NAME = "L3_TEST_RULE"

class TestNat(object):
    def setup_db(self, dvs):
        self.app_db = dvs.get_app_db()
        self.asic_db = dvs.get_asic_db()
        self.config_db = dvs.get_config_db()

    def set_interfaces(self, dvs):
        dvs.interface_ip_add("Ethernet0", "67.66.65.1/24")
        dvs.interface_ip_add("Ethernet4", "18.18.18.1/24")
        dvs.port_admin_set("Ethernet0", "up")
        dvs.port_admin_set("Etherent4", "up")

        dvs.servers[0].runcmd("ip link set down dev eth0")
        dvs.servers[0].runcmd("ip link set up dev eth0")
        dvs.servers[0].runcmd("ifconfig eth0 67.66.65.2/24")
        dvs.servers[0].runcmd("ip route add default via 67.66.65.1")

        dvs.servers[1].runcmd("ip link set down dev eth0")
        dvs.servers[1].runcmd("ip link set up dev eth0")
        dvs.servers[1].runcmd("ifconfig eth0 18.18.18.2/24")
        dvs.servers[1].runcmd("ip route add default via 18.18.18.1")

        dvs.set_nat_zone("Ethernet0", "1")

        time.sleep(1)

    def clear_interfaces(self, dvs):
        dvs.servers[0].runcmd("ifconfig eth0 0.0.0.0")
        dvs.servers[1].runcmd("ifconfig eth0 0.0.0.0")

        time.sleep(1)

    def test_NatGlobalTable(self, dvs, testlog):
        # initialize
        self.setup_db(dvs)

        # enable NAT feature
        dvs.nat_mode_set("enabled")
        dvs.nat_timeout_set("450")
        dvs.nat_udp_timeout_set("360")
        dvs.nat_tcp_timeout_set("900")

        # check NAT global values in appdb
        self.app_db.wait_for_n_keys("NAT_GLOBAL_TABLE", 1)

        fvs = self.app_db.wait_for_entry("NAT_GLOBAL_TABLE", "Values")

        assert fvs == {"admin_mode": "enabled", "nat_timeout": "450", "nat_udp_timeout": "360", "nat_tcp_timeout": "900"}

    def test_NatInterfaceZone(self, dvs, testlog):
        # initialize
        self.setup_db(dvs)
        self.set_interfaces(dvs)

        # check NAT zone is set for interface in app db
        fvs = self.app_db.wait_for_entry("INTF_TABLE", "Ethernet0")
        zone = False
        for f, v in fvs.items():
            if f == "nat_zone" and v == '1':
                zone = True
                break
        assert zone

    def test_AddNatStaticEntry(self, dvs, testlog):
        # initialize
        self.setup_db(dvs)

        # get neighbor and arp entry
        dvs.servers[0].runcmd("ping -c 1 18.18.18.2")

        # add a static nat entry
        dvs.add_nat_basic_entry("67.66.65.1", "18.18.18.2")

        # check the entry in the config db
        self.config_db.wait_for_n_keys("STATIC_NAT", 1)

        fvs = self.config_db.wait_for_entry("STATIC_NAT", "67.66.65.1")

        assert fvs == {"local_ip": "18.18.18.2"}

        # check the entry in app db
        self.app_db.wait_for_n_keys("NAT_TABLE", 2)

        fvs = self.app_db.wait_for_entry("NAT_TABLE", "67.66.65.1")

        assert fvs == {
            "translated_ip": "18.18.18.2",
            "nat_type": "dnat",
            "entry_type": "static"
        }

        #check the entry in asic db, 3 keys = SNAT, DNAT and DNAT_Pool
        keys = self.asic_db.wait_for_n_keys("ASIC_STATE:SAI_OBJECT_TYPE_NAT_ENTRY", 3)
        for key in keys:
            if (key.find("dst_ip:67.66.65.1")) or (key.find("src_ip:18.18.18.2")):
                assert True
            else:
                assert False

    def test_DelNatStaticEntry(self, dvs, testlog):
        # initialize
        self.setup_db(dvs)

        # delete a static nat entry
        dvs.del_nat_basic_entry("67.66.65.1")

        # check the entry is no there in the config db
        self.config_db.wait_for_n_keys("STATIC_NAT", 0)

        # check the entry is not there in app db
        self.app_db.wait_for_n_keys("NAT_TABLE", 0)

        #check the entry is not there in asic db
        self.asic_db.wait_for_n_keys("ASIC_STATE:SAI_OBJECT_TYPE_NAT_ENTRY", 0)

    def test_AddNaPtStaticEntry(self, dvs, testlog):
        # initialize
        self.setup_db(dvs)

        # get neighbor and arp entry
        dvs.servers[0].runcmd("ping -c 1 18.18.18.2")

        # add a static nat entry
        dvs.add_nat_udp_entry("67.66.65.1", "670", "18.18.18.2", "180")

        # check the entry in the config db
        self.config_db.wait_for_n_keys("STATIC_NAPT", 1)

        fvs = self.config_db.wait_for_entry("STATIC_NAPT", "67.66.65.1|UDP|670")

        assert fvs == {"local_ip": "18.18.18.2", "local_port": "180"}

        # check the entry in app db
        self.app_db.wait_for_n_keys("NAPT_TABLE:UDP", 2)

        fvs = self.app_db.wait_for_entry("NAPT_TABLE:UDP", "67.66.65.1:670")

        assert fvs == {"translated_ip": "18.18.18.2", "translated_l4_port": "180", "nat_type": "dnat", "entry_type": "static"}

        #check the entry in asic db, 3 keys = SNAT, DNAT and DNAT_Pool
        keys = self.asic_db.wait_for_n_keys("ASIC_STATE:SAI_OBJECT_TYPE_NAT_ENTRY", 3)
        for key in keys:
            if (key.find("dst_ip:67.66.65.1")) and (key.find("key.l4_dst_port:670")):
                assert True
            if (key.find("src_ip:18.18.18.2")) or (key.find("key.l4_src_port:180")):
                assert True
            else:
                assert False

    def test_DelNaPtStaticEntry(self, dvs, testlog):
        # initialize
        self.setup_db(dvs)

        # delete a static nat entry
        dvs.del_nat_udp_entry("67.66.65.1", "670")

        # check the entry is no there in the config db
        self.config_db.wait_for_n_keys("STATIC_NAPT", 0)

        # check the entry is not there in app db
        self.app_db.wait_for_n_keys("NAPT_TABLE:UDP", 0)

        #check the entry is not there in asic db
        self.asic_db.wait_for_n_keys("ASIC_STATE:SAI_OBJECT_TYPE_NAT_ENTRY", 0)

    @pytest.mark.skip(reason="Failing. Under investigation")
    def test_AddTwiceNatEntry(self, dvs, testlog):
        # initialize
        self.setup_db(dvs)

        # get neighbor and arp entry
        dvs.servers[0].runcmd("ping -c 1 18.18.18.2")
        dvs.servers[1].runcmd("ping -c 1 67.66.65.2")

        # add a twice nat entry
        dvs.add_twice_nat_basic_entry("67.66.65.2", "18.18.18.1", "snat", "9")
        dvs.add_twice_nat_basic_entry("67.66.65.1", "18.18.18.2", "dnat", "9")

        # check the entry in the config db
        self.config_db.wait_for_n_keys("STATIC_NAT", 2)

        fvs = self.config_db.wait_for_entry("STATIC_NAT", "67.66.65.1")
        assert fvs == {"nat_type": "dnat", "twice_nat_id": "9", "local_ip": "18.18.18.2"}

        fvs = self.config_db.wait_for_entry("STATIC_NAT", "67.66.65.2")
        assert fvs == {"nat_type": "snat", "twice_nat_id": "9", "local_ip": "18.18.18.1"}

        # check the entry in app db
        self.app_db.wait_for_n_keys("NAT_TWICE_TABLE", 2)

        fvs = self.app_db.wait_for_entry("NAT_TWICE_TABLE", "67.66.65.2:67.66.65.1")
        assert fvs == {"translated_src_ip": "18.18.18.1", "translated_dst_ip": "18.18.18.2", "entry_type": "static"}

        fvs = self.app_db.wait_for_entry("NAT_TWICE_TABLE", "18.18.18.2:18.18.18.1")
        assert fvs == {"translated_src_ip": "67.66.65.1", "translated_dst_ip": "67.66.65.2", "entry_type": "static"}

        #check the entry in asic db, 4 keys = SNAT, DNAT and 2 DNAT_Pools
        keys = self.asic_db.wait_for_n_keys("ASIC_STATE:SAI_OBJECT_TYPE_NAT_ENTRY", 4)
        for key in keys:
            if (key.find("dst_ip:18.18.18.1")) or (key.find("src_ip:18.18.18.2")):
                assert True
            else:
                assert False

    def test_DelTwiceNatStaticEntry(self, dvs, testlog):
        # initialize
        self.setup_db(dvs)

        # delete a static nat entry
        dvs.del_twice_nat_basic_entry("67.66.65.2")
        dvs.del_twice_nat_basic_entry("67.66.65.1")

        # check the entry is no there in the config db
        self.config_db.wait_for_n_keys("STATIC_NAT", 0)

        # check the entry is not there in app db
        self.app_db.wait_for_n_keys("NAT_TWICE_TABLE", 0)

        #check the entry is not there in asic db
        self.asic_db.wait_for_n_keys("ASIC_STATE:SAI_OBJECT_TYPE_NAT_ENTRY", 0)

    def test_AddTwiceNaPtEntry(self, dvs, testlog):
        # initialize
        self.setup_db(dvs)

        # get neighbor and arp entry
        dvs.servers[0].runcmd("ping -c 1 18.18.18.2")
        dvs.servers[1].runcmd("ping -c 1 67.66.65.2")

        # add a twice nat entry
        dvs.add_twice_nat_udp_entry("67.66.65.2", "670", "18.18.18.1", "181", "snat", "7")
        dvs.add_twice_nat_udp_entry("67.66.65.1", "660", "18.18.18.2", "182", "dnat", "7")

        # check the entry in the config db
        self.config_db.wait_for_n_keys("STATIC_NAPT", 2)

        fvs = self.config_db.wait_for_entry("STATIC_NAPT", "67.66.65.1|UDP|660")
        assert fvs == {"nat_type": "dnat", "local_ip": "18.18.18.2", "twice_nat_id": "7", "local_port": "182"}

        fvs = self.config_db.wait_for_entry("STATIC_NAPT", "67.66.65.2|UDP|670")
        assert fvs == {"nat_type": "snat", "local_ip": "18.18.18.1", "twice_nat_id": "7", "local_port": "181"}

        # check the entry in app db
        self.app_db.wait_for_n_keys("NAPT_TWICE_TABLE", 2)

        fvs = self.app_db.wait_for_entry("NAPT_TWICE_TABLE", "UDP:67.66.65.2:670:67.66.65.1:660")
        assert fvs == {"translated_src_ip": "18.18.18.1", "translated_src_l4_port": "181", "translated_dst_ip": "18.18.18.2", "translated_dst_l4_port": "182", "entry_type": "static"}

        fvs = self.app_db.wait_for_entry("NAPT_TWICE_TABLE", "UDP:18.18.18.2:182:18.18.18.1:181")
        assert fvs == {"translated_src_ip": "67.66.65.1", "translated_src_l4_port": "660", "translated_dst_ip": "67.66.65.2", "translated_dst_l4_port": "670", "entry_type": "static"}

        #check the entry in asic db, 4 keys = SNAT, DNAT and 2 DNAT_Pools
        keys = self.asic_db.wait_for_n_keys("ASIC_STATE:SAI_OBJECT_TYPE_NAT_ENTRY", 4)
        for key in keys:
            if (key.find("src_ip:18.18.18.2")) or (key.find("l4_src_port:182")):
                assert True
            if (key.find("dst_ip:18.18.18.1")) or (key.find("l4_dst_port:181")):
                assert True
            else:
                assert False

    def test_DelTwiceNaPtStaticEntry(self, dvs, testlog):
        # initialize
        self.setup_db(dvs)

        # delete a static nat entry
        dvs.del_twice_nat_udp_entry("67.66.65.2", "670")
        dvs.del_twice_nat_udp_entry("67.66.65.1", "660")

        # check the entry is not there in the config db
        self.config_db.wait_for_n_keys("STATIC_NAPT", 0)

        # check the entry is not there in app db
        self.app_db.wait_for_n_keys("NAPT_TWICE_TABLE", 0)

        #check the entry is not there in asic db
        self.asic_db.wait_for_n_keys("ASIC_STATE:SAI_OBJECT_TYPE_NAT_ENTRY", 0)

    def test_VerifyConntrackTimeoutForNatEntry(self, dvs, testlog):
        # get neighbor and arp entry
        dvs.servers[0].runcmd("ping -c 1 18.18.18.2")

        # add a static nat entry
        dvs.add_nat_basic_entry("67.66.65.1", "18.18.18.2")

        # check the conntrack timeout for static entry
        def _check_conntrack_for_static_entry():
            output = dvs.runcmd("conntrack -j -L -s 18.18.18.2 -p udp -q 67.66.65.1")
            if len(output) != 2:
                return (False, None)

            conntrack_list = list(output[1].split(" "))

            src_exists = "src=18.18.18.2" in conntrack_list
            dst_exists = "dst=67.66.65.1" in conntrack_list
            proto_exists = "udp" in conntrack_list

            if not src_exists or not dst_exists or not proto_exists:
                return (False, None)

            proto_index = conntrack_list.index("udp")

            if int(conntrack_list[proto_index + 7]) > 432000 or int(conntrack_list[proto_index + 7]) < 431900:
                return (False, None)

            return (True, None)

        wait_for_result(_check_conntrack_for_static_entry)

        # delete a static nat entry
        dvs.del_nat_basic_entry("67.66.65.1")

    def test_DoNotNatAclAction(self, dvs_acl, testlog):

        # Creating the ACL Table
        dvs_acl.create_acl_table(L3_TABLE_NAME, L3_TABLE_TYPE, L3_BIND_PORTS, stage="ingress")

        acl_table_id = dvs_acl.get_acl_table_ids(1)[0]
        acl_table_group_ids = dvs_acl.get_acl_table_group_ids(len(L3_BIND_PORTS))

        dvs_acl.verify_acl_table_group_members(acl_table_id, acl_table_group_ids, 1)
        dvs_acl.verify_acl_table_port_binding(acl_table_id, L3_BIND_PORTS, 1)

        # Create a ACL Rule with "do_not_nat" packet action
        config_qualifiers = {"SRC_IP": "14.1.0.1/32"}
        dvs_acl.create_acl_rule(L3_TABLE_NAME, L3_RULE_NAME, config_qualifiers, action="DO_NOT_NAT", priority="97")

        expected_sai_qualifiers = {
            "SAI_ACL_ENTRY_ATTR_FIELD_SRC_IP": dvs_acl.get_simple_qualifier_comparator("14.1.0.1&mask:255.255.255.255")
        }

        dvs_acl.verify_nat_acl_rule(expected_sai_qualifiers, priority="97")

        # Deleting the ACL Rule
        dvs_acl.remove_acl_rule(L3_TABLE_NAME, L3_RULE_NAME)
        dvs_acl.verify_no_acl_rules()

        # Deleting the ACL Table
        dvs_acl.remove_acl_table(L3_TABLE_NAME)
        dvs_acl.verify_acl_table_count(0)

    def test_CrmSnatAndDnatEntryUsedCount(self, dvs, testlog):
        # initialize
        self.setup_db(dvs)

        # get neighbor and arp entry
        dvs.servers[0].runcmd("ping -c 1 18.18.18.2")

        # set pooling interval to 1
        dvs.crm_poll_set("1")

        dvs.setReadOnlyAttr('SAI_OBJECT_TYPE_SWITCH', 'SAI_SWITCH_ATTR_AVAILABLE_SNAT_ENTRY', '1000')
        dvs.setReadOnlyAttr('SAI_OBJECT_TYPE_SWITCH', 'SAI_SWITCH_ATTR_AVAILABLE_DNAT_ENTRY', '1000')

        time.sleep(2)

        # get snat counters
        used_snat_counter = dvs.getCrmCounterValue('STATS', 'crm_stats_snat_entry_used')
        avail_snat_counter = dvs.getCrmCounterValue('STATS', 'crm_stats_snat_entry_available')

        # get dnat counters
        used_dnat_counter = dvs.getCrmCounterValue('STATS', 'crm_stats_dnat_entry_used')
        avail_dnat_counter = dvs.getCrmCounterValue('STATS', 'crm_stats_dnat_entry_available')

        # add a static nat entry
        dvs.add_nat_basic_entry("67.66.65.1", "18.18.18.2")

        #check the entry in asic db, 3 keys = SNAT, DNAT and DNAT_Pool
        keys = self.asic_db.wait_for_n_keys("ASIC_STATE:SAI_OBJECT_TYPE_NAT_ENTRY", 3)
        for key in keys:
            if (key.find("dst_ip:67.66.65.1")) or (key.find("src_ip:18.18.18.2")):
                assert True
            else:
                assert False

        dvs.setReadOnlyAttr('SAI_OBJECT_TYPE_SWITCH', 'SAI_SWITCH_ATTR_AVAILABLE_SNAT_ENTRY', '999')
        dvs.setReadOnlyAttr('SAI_OBJECT_TYPE_SWITCH', 'SAI_SWITCH_ATTR_AVAILABLE_DNAT_ENTRY', '999')

        time.sleep(2)

        # get snat counters
        new_used_snat_counter = dvs.getCrmCounterValue('STATS', 'crm_stats_snat_entry_used')
        new_avail_snat_counter = dvs.getCrmCounterValue('STATS', 'crm_stats_snat_entry_available')

        # get dnat counters
        new_used_dnat_counter = dvs.getCrmCounterValue('STATS', 'crm_stats_dnat_entry_used')
        new_avail_dnat_counter = dvs.getCrmCounterValue('STATS', 'crm_stats_dnat_entry_available')

        assert new_used_snat_counter - used_snat_counter == 1
        assert avail_snat_counter - new_avail_snat_counter == 1
        assert new_used_dnat_counter - used_dnat_counter == 1
        assert avail_dnat_counter - new_avail_dnat_counter == 1

        # delete a static nat entry
        dvs.del_nat_basic_entry("67.66.65.1")

        dvs.setReadOnlyAttr('SAI_OBJECT_TYPE_SWITCH', 'SAI_SWITCH_ATTR_AVAILABLE_SNAT_ENTRY', '1000')
        dvs.setReadOnlyAttr('SAI_OBJECT_TYPE_SWITCH', 'SAI_SWITCH_ATTR_AVAILABLE_DNAT_ENTRY', '1000')

        time.sleep(2)

        # get snat counters
        new_used_snat_counter = dvs.getCrmCounterValue('STATS', 'crm_stats_snat_entry_used')
        new_avail_snat_counter = dvs.getCrmCounterValue('STATS', 'crm_stats_snat_entry_available')

        # get dnat counters
        new_used_dnat_counter = dvs.getCrmCounterValue('STATS', 'crm_stats_dnat_entry_used')
        new_avail_dnat_counter = dvs.getCrmCounterValue('STATS', 'crm_stats_dnat_entry_available')

        assert new_used_snat_counter == used_snat_counter
        assert new_avail_snat_counter == avail_snat_counter
        assert new_used_dnat_counter == used_dnat_counter
        assert new_avail_dnat_counter == avail_dnat_counter

        # clear interfaces
        self.clear_interfaces(dvs)

    def test_AddSnatDynamicEntry(self, dvs, testlog):
        # initialize
        self.setup_db(dvs)
        dvs.nat_mode_set("enabled")
        dvs.nat_timeout_set("400")
        self.set_interfaces(dvs)

        exitcode, output = dvs.runcmd("conntrack -L -j")
        assert exitcode == 0, "0 flow entries have been shown" in output

        # create SNAT entry in kernel
        exitcode, output = dvs.runcmd("conntrack -I -p udp -t 400 --src 33.33.33.33 --sport 3000 --dst 44.44.44.44 --dport 4000 -n 10.10.10.10:3000")
        assert exitcode == 0, "1 flow entries have been created" in output

        # check the SNAT entry and implicit DNAT entry pushed to app db
        self.app_db.wait_for_n_keys("NAT_TABLE", 2)

        # check the entries in app db
        fvs = self.app_db.wait_for_entry("NAT_TABLE", "33.33.33.33")
        assert fvs == {
            "translated_ip": "10.10.10.10",
            "nat_type": "snat",
            "entry_type": "dynamic"
        }
        fvs = self.app_db.wait_for_entry("NAT_TABLE", "10.10.10.10")
        assert fvs == {
            "translated_ip": "33.33.33.33",
            "nat_type": "dnat",
            "entry_type": "dynamic"
        }

        # check the entries in asic db
        keys = self.asic_db.wait_for_n_keys("ASIC_STATE:SAI_OBJECT_TYPE_NAT_ENTRY", 2)
        for key in keys:
            if '"nat_type":"SAI_NAT_TYPE_SOURCE_NAT"' in key:
                fvs = self.asic_db.wait_for_entry("ASIC_STATE:SAI_OBJECT_TYPE_NAT_ENTRY", key)
                assert fvs == {
                    'SAI_NAT_ENTRY_ATTR_SRC_IP': '10.10.10.10',
                    'SAI_NAT_ENTRY_ATTR_SRC_IP_MASK': '255.255.255.255',
                    'SAI_NAT_ENTRY_ATTR_ENABLE_PACKET_COUNT': 'true',
                    'SAI_NAT_ENTRY_ATTR_ENABLE_BYTE_COUNT': 'true',
                    'SAI_NAT_ENTRY_ATTR_AGING_TIME': '400'
                }
            elif '"nat_type":"SAI_NAT_TYPE_DESTINATION_NAT"' in key:
                fvs = self.asic_db.wait_for_entry("ASIC_STATE:SAI_OBJECT_TYPE_NAT_ENTRY", key)
                assert fvs == {
                    'SAI_NAT_ENTRY_ATTR_DST_IP': '33.33.33.33',
                    'SAI_NAT_ENTRY_ATTR_DST_IP_MASK': '255.255.255.255',
                    'SAI_NAT_ENTRY_ATTR_ENABLE_PACKET_COUNT': 'true',
                    'SAI_NAT_ENTRY_ATTR_ENABLE_BYTE_COUNT': 'true',
                    'SAI_NAT_ENTRY_ATTR_AGING_TIME': '400'
                }
            else:
                assert False

        # delete SNAT entry in kernel
        exitcode, output = dvs.runcmd("conntrack -D -p udp --src 33.33.33.33 --sport 3000 --dst 44.44.44.44 --dport 4000")
        assert exitcode == 0, "1 flow entries have been deleted" in output

        exitcode, output = dvs.runcmd("conntrack -L -j")
        assert exitcode == 0, "0 flow entries have been shown" in output

        # check the entry is not there in app db
        self.app_db.wait_for_n_keys("NAT_TABLE", 0)

        # check the entry is not there in asic db
        self.asic_db.wait_for_n_keys("ASIC_STATE:SAI_OBJECT_TYPE_NAT_ENTRY", 0)

    def test_AddDnatDynamicEntry(self, dvs, testlog):
        # initialize
        self.setup_db(dvs)
        dvs.nat_mode_set("enabled")
        dvs.nat_timeout_set("450")
        self.set_interfaces(dvs)

        exitcode, output = dvs.runcmd("conntrack -L -j")
        assert exitcode == 0, "0 flow entries have been shown" in output

        # create DNAT entry in kernel
        exitcode, output = dvs.runcmd("conntrack -I -p udp -t 450 --src 33.33.33.33 --sport 3000 --dst 44.44.44.44 --dport 4000 -g 10.10.10.10:4000")
        assert exitcode == 0, "1 flow entries have been created" in output

        # check the DNAT entry and implicit SNAT entry pushed to app db
        self.app_db.wait_for_n_keys("NAT_TABLE", 2)

        # check the entries in app db
        fvs = self.app_db.wait_for_entry("NAT_TABLE", "44.44.44.44")
        assert fvs == {
            "translated_ip": "10.10.10.10",
            "nat_type": "dnat",
            "entry_type": "dynamic"
        }
        fvs = self.app_db.wait_for_entry("NAT_TABLE", "10.10.10.10")
        assert fvs == {
            "translated_ip": "44.44.44.44",
            "nat_type": "snat",
            "entry_type": "dynamic"
        }

        # check the entries in asic db
        keys = self.asic_db.wait_for_n_keys("ASIC_STATE:SAI_OBJECT_TYPE_NAT_ENTRY", 2)
        for key in keys:
            if '"nat_type":"SAI_NAT_TYPE_DESTINATION_NAT"' in key:
                fvs = self.asic_db.wait_for_entry("ASIC_STATE:SAI_OBJECT_TYPE_NAT_ENTRY", key)
                assert fvs == {
                    'SAI_NAT_ENTRY_ATTR_DST_IP': '10.10.10.10',
                    'SAI_NAT_ENTRY_ATTR_DST_IP_MASK': '255.255.255.255',
                    'SAI_NAT_ENTRY_ATTR_ENABLE_PACKET_COUNT': 'true',
                    'SAI_NAT_ENTRY_ATTR_ENABLE_BYTE_COUNT': 'true',
                    'SAI_NAT_ENTRY_ATTR_AGING_TIME': '450'
                }
            elif '"nat_type":"SAI_NAT_TYPE_SOURCE_NAT"' in key:
                fvs = self.asic_db.wait_for_entry("ASIC_STATE:SAI_OBJECT_TYPE_NAT_ENTRY", key)
                assert fvs == {
                    'SAI_NAT_ENTRY_ATTR_SRC_IP': '44.44.44.44',
                    'SAI_NAT_ENTRY_ATTR_SRC_IP_MASK': '255.255.255.255',
                    'SAI_NAT_ENTRY_ATTR_ENABLE_PACKET_COUNT': 'true',
                    'SAI_NAT_ENTRY_ATTR_ENABLE_BYTE_COUNT': 'true',
                    'SAI_NAT_ENTRY_ATTR_AGING_TIME': '450'
                }

        # delete DNAT entry in kernel
        exitcode, output = dvs.runcmd("conntrack -D -p udp --src 33.33.33.33 --sport 3000 --dst 44.44.44.44 --dport 4000")
        assert exitcode == 0, "1 flow entries have been deleted" in output

        exitcode, output = dvs.runcmd("conntrack -L -j")
        assert exitcode == 0, "0 flow entries have been shown" in output

        # check the entry is not there in app db
        self.app_db.wait_for_n_keys("NAT_TABLE", 0)

        # check the entry is not there in asic db
        self.asic_db.wait_for_n_keys("ASIC_STATE:SAI_OBJECT_TYPE_NAT_ENTRY", 0)

    def test_AddSnaptDynamicEntry(self, dvs, testlog):
        # initialize
        self.setup_db(dvs)
        dvs.nat_mode_set("enabled")
        dvs.nat_udp_timeout_set("360")
        self.set_interfaces(dvs)

        exitcode, output = dvs.runcmd("conntrack -L -j")
        assert exitcode == 0, "0 flow entries have been shown" in output

        # create SNAPT entry in kernel
        exitcode, output = dvs.runcmd("conntrack -I -p udp -t 360 --src 33.33.33.33 --sport 3000 --dst 44.44.44.44 --dport 4000 -n 10.10.10.10:5000")
        assert exitcode == 0, "1 flow entries have been created" in output

        # check the SNAPT entry and implicit DNAPT entry pushed to app db
        self.app_db.wait_for_n_keys("NAPT_TABLE", 2)

        # check the entries in app db
        fvs = self.app_db.wait_for_entry("NAPT_TABLE", "UDP:33.33.33.33:3000")
        assert fvs == {
            "translated_ip": "10.10.10.10",
            "translated_l4_port": "5000",
            "nat_type": "snat",
            "entry_type": "dynamic"
        }
        fvs = self.app_db.wait_for_entry("NAPT_TABLE", "UDP:10.10.10.10:5000")
        assert fvs == {
            "translated_ip": "33.33.33.33",
            "translated_l4_port": "3000",
            "nat_type": "dnat",
            "entry_type": "dynamic"
        }

        # check the entries in asic db
        keys = self.asic_db.wait_for_n_keys("ASIC_STATE:SAI_OBJECT_TYPE_NAT_ENTRY", 2)
        for key in keys:
            if '"nat_type":"SAI_NAT_TYPE_SOURCE_NAT"' in key:
                fvs = self.asic_db.wait_for_entry("ASIC_STATE:SAI_OBJECT_TYPE_NAT_ENTRY", key)
                assert fvs == {
                    'SAI_NAT_ENTRY_ATTR_SRC_IP': '10.10.10.10',
                    'SAI_NAT_ENTRY_ATTR_L4_SRC_PORT': '5000',
                    'SAI_NAT_ENTRY_ATTR_SRC_IP_MASK': '255.255.255.255',
                    'SAI_NAT_ENTRY_ATTR_ENABLE_PACKET_COUNT': 'true',
                    'SAI_NAT_ENTRY_ATTR_ENABLE_BYTE_COUNT': 'true',
                    'SAI_NAT_ENTRY_ATTR_AGING_TIME': '360'
                }
            elif '"nat_type":"SAI_NAT_TYPE_DESTINATION_NAT"' in key:
                fvs = self.asic_db.wait_for_entry("ASIC_STATE:SAI_OBJECT_TYPE_NAT_ENTRY", key)
                assert fvs == {
                    'SAI_NAT_ENTRY_ATTR_DST_IP': '33.33.33.33',
                    'SAI_NAT_ENTRY_ATTR_L4_DST_PORT': '3000',
                    'SAI_NAT_ENTRY_ATTR_DST_IP_MASK': '255.255.255.255',
                    'SAI_NAT_ENTRY_ATTR_ENABLE_PACKET_COUNT': 'true',
                    'SAI_NAT_ENTRY_ATTR_ENABLE_BYTE_COUNT': 'true',
                    'SAI_NAT_ENTRY_ATTR_AGING_TIME': '360'
                }
            else:
                assert False

        # delete SNAPT entry in kernel
        exitcode, output = dvs.runcmd("conntrack -D -p udp --src 33.33.33.33 --sport 3000 --dst 44.44.44.44 --dport 4000")
        assert exitcode == 0, "1 flow entries have been deleted" in output

        exitcode, output = dvs.runcmd("conntrack -L -j")
        assert exitcode == 0, "0 flow entries have been shown" in output

        # check the entry is not there in app db
        self.app_db.wait_for_n_keys("NAPT_TABLE", 0)

        # check the entry is not there in asic db
        self.asic_db.wait_for_n_keys("ASIC_STATE:SAI_OBJECT_TYPE_NAT_ENTRY", 0)

    def test_AddDnaptDynamicEntry(self, dvs, testlog):
        # initialize
        self.setup_db(dvs)
        dvs.nat_mode_set("enabled")
        dvs.nat_tcp_timeout_set("432")
        self.set_interfaces(dvs)

        exitcode, output = dvs.runcmd("conntrack -L -j")
        assert exitcode == 0, "0 flow entries have been shown" in output

        # create DNAPT entry in kernel
        exitcode, output = dvs.runcmd("conntrack -I -p tcp --state ESTABLISHED -t 432 --src 33.33.33.33 --sport 3000 --dst 44.44.44.44 --dport 4000 -u ASSURED -g 10.10.10.10:6000")
        assert exitcode == 0, "1 flow entries have been created" in output

        # check the DNAPT entry and implicit SNAPT entry pushed to app db
        self.app_db.wait_for_n_keys("NAPT_TABLE", 2)

        fvs = self.app_db.wait_for_entry("NAPT_TABLE", "TCP:44.44.44.44:4000")
        assert fvs == {
            "translated_ip": "10.10.10.10",
            "translated_l4_port": "6000",
            "nat_type": "dnat",
            "entry_type": "dynamic"
        }
        fvs = self.app_db.wait_for_entry("NAPT_TABLE", "TCP:10.10.10.10:6000")
        assert fvs == {
            "translated_ip": "44.44.44.44",
            "translated_l4_port": "4000",
            "nat_type": "snat",
            "entry_type": "dynamic"
        }

        # check the entries in asic db
        keys = self.asic_db.wait_for_n_keys("ASIC_STATE:SAI_OBJECT_TYPE_NAT_ENTRY", 2)
        for key in keys:
            if '"nat_type":"SAI_NAT_TYPE_DESTINATION_NAT"' in key:
                fvs = self.asic_db.wait_for_entry("ASIC_STATE:SAI_OBJECT_TYPE_NAT_ENTRY", key)
                assert fvs == {
                    'SAI_NAT_ENTRY_ATTR_DST_IP': '10.10.10.10',
                    'SAI_NAT_ENTRY_ATTR_L4_DST_PORT': '6000',
                    'SAI_NAT_ENTRY_ATTR_DST_IP_MASK': '255.255.255.255',
                    'SAI_NAT_ENTRY_ATTR_ENABLE_PACKET_COUNT': 'true',
                    'SAI_NAT_ENTRY_ATTR_ENABLE_BYTE_COUNT': 'true',
                    'SAI_NAT_ENTRY_ATTR_AGING_TIME': '432'
                }
            elif '"nat_type":"SAI_NAT_TYPE_SOURCE_NAT"' in key:
                fvs = self.asic_db.wait_for_entry("ASIC_STATE:SAI_OBJECT_TYPE_NAT_ENTRY", key)
                assert fvs == {
                    'SAI_NAT_ENTRY_ATTR_SRC_IP': '44.44.44.44',
                    'SAI_NAT_ENTRY_ATTR_L4_SRC_PORT': '4000',
                    'SAI_NAT_ENTRY_ATTR_SRC_IP_MASK': '255.255.255.255',
                    'SAI_NAT_ENTRY_ATTR_ENABLE_PACKET_COUNT': 'true',
                    'SAI_NAT_ENTRY_ATTR_ENABLE_BYTE_COUNT': 'true',
                    'SAI_NAT_ENTRY_ATTR_AGING_TIME': '432'
                }
            else:
                assert False

        # delete DNAPT entry in kernel
        exitcode, output = dvs.runcmd("conntrack -D -p tcp --src 33.33.33.33 --sport 3000 --dst 44.44.44.44 --dport 4000")
        assert exitcode == 0, "1 flow entries have been deleted" in output

        exitcode, output = dvs.runcmd("conntrack -L -j")
        assert exitcode == 0, "0 flow entries have been shown" in output

        # check the entry is not there in app db
        self.app_db.wait_for_n_keys("NAPT_TABLE", 0)

        # check the entry is not there in asic db
        self.asic_db.wait_for_n_keys("ASIC_STATE:SAI_OBJECT_TYPE_NAT_ENTRY", 0)

    def test_NaptDynamicEntryNotification(self, dvs, testlog):
        # initialize
        self.setup_db(dvs)
        dvs.nat_mode_set("enabled")
        dvs.nat_tcp_timeout_set("700")
        self.set_interfaces(dvs)

        exitcode, output = dvs.runcmd("conntrack -L -j")
        assert exitcode == 0, "0 flow entries have been shown" in output

        # create SNAPT entry in kernel
        exitcode, output = dvs.runcmd("conntrack -I -p tcp --state ESTABLISHED -t 700 --src 33.33.33.33 --sport 3000 --dst 44.44.44.44 --dport 4000 -u ASSURED -n 10.10.10.10:7000")
        assert exitcode == 0, "1 flow entries have been created" in output

        # check the SNAPT entry and implicit DNAPT entry pushed to app db by Natsyncd
        self.app_db.wait_for_n_keys("NAPT_TABLE", 2)

        # check the entries in app db
        fvs = self.app_db.wait_for_entry("NAPT_TABLE", "TCP:33.33.33.33:3000")
        assert fvs == {
            "translated_ip": "10.10.10.10",
            "translated_l4_port": "7000",
            "nat_type": "snat",
            "entry_type": "dynamic"
        }
        fvs = self.app_db.wait_for_entry("NAPT_TABLE", "TCP:10.10.10.10:7000")
        assert fvs == {
            "translated_ip": "33.33.33.33",
            "translated_l4_port": "3000",
            "nat_type": "dnat",
            "entry_type": "dynamic"
        }

        # check the entries in asic db
        keys = self.asic_db.wait_for_n_keys("ASIC_STATE:SAI_OBJECT_TYPE_NAT_ENTRY", 2)
        for key in keys:
            if '"nat_type":"SAI_NAT_TYPE_SOURCE_NAT"' in key:
                fvs = self.asic_db.wait_for_entry("ASIC_STATE:SAI_OBJECT_TYPE_NAT_ENTRY", key)
                assert fvs == {
                    'SAI_NAT_ENTRY_ATTR_SRC_IP': '10.10.10.10',
                    'SAI_NAT_ENTRY_ATTR_L4_SRC_PORT': '7000',
                    'SAI_NAT_ENTRY_ATTR_SRC_IP_MASK': '255.255.255.255',
                    'SAI_NAT_ENTRY_ATTR_ENABLE_PACKET_COUNT': 'true',
                    'SAI_NAT_ENTRY_ATTR_ENABLE_BYTE_COUNT': 'true',
                    'SAI_NAT_ENTRY_ATTR_AGING_TIME': '700'
                }
            elif '"nat_type":"SAI_NAT_TYPE_DESTINATION_NAT"' in key:
                fvs = self.asic_db.wait_for_entry("ASIC_STATE:SAI_OBJECT_TYPE_NAT_ENTRY", key)
                assert fvs == {
                    'SAI_NAT_ENTRY_ATTR_DST_IP': '33.33.33.33',
                    'SAI_NAT_ENTRY_ATTR_L4_DST_PORT': '3000',
                    'SAI_NAT_ENTRY_ATTR_DST_IP_MASK': '255.255.255.255',
                    'SAI_NAT_ENTRY_ATTR_ENABLE_PACKET_COUNT': 'true',
                    'SAI_NAT_ENTRY_ATTR_ENABLE_BYTE_COUNT': 'true',
                    'SAI_NAT_ENTRY_ATTR_AGING_TIME': '700'
                }
            else:
                assert False

        # Obtain vr_id to send through the notification
        asic_db = swsscommon.DBConnector(swsscommon.ASIC_DB, dvs.redis_sock, 0)
        tbl =  swsscommon.Table(asic_db, "ASIC_STATE:SAI_OBJECT_TYPE_VIRTUAL_ROUTER")
        keys = tbl.getKeys()
        assert len(keys) == 1, "Wrong number of virtual routers found"
        vr_id =  keys[0]

        # Send notification
        ntf = swsscommon.NotificationProducer(asic_db, "NOTIFICATIONS")
        fvp = swsscommon.FieldValuePairs()
        ntf_data = "[{\"nat_entry\":\"{\\\"nat_data\\\":{\\\"key\\\":{\\\"dst_ip\\\":\\\"0.0.0.0\\\",\\\"l4_dst_port\\\":\\\"0\\\",\\\"l4_src_port\\\":\\\"3000\\\",\\\"proto\\\":\\\"6\\\",\\\"src_ip\\\":\\\"33.33.33.33\\\"},\\\"mask\\\":{\\\"dst_ip\\\":\\\"0.0.0.0\\\",\\\"l4_dst_port\\\":\\\"0\\\",\\\"l4_src_port\\\":\\\"65535\\\",\\\"proto\\\":\\\"255\\\",\\\"src_ip\\\":\\\"255.255.255.255\\\"}},\\\"nat_type\\\":\\\"SAI_NAT_TYPE_SOURCE_NAT\\\",\\\"switch_id\\\":\\\"oid:0x21000000000000\\\",\\\"vr\\\":\\\"" + vr_id + "\\\"}\",\"nat_event\":\"SAI_NAT_EVENT_AGED\"}]"
        ntf.send("nat_event", ntf_data, fvp)

        # check the entry is not there in kernel
        exitcode, output = dvs.runcmd("conntrack -L -j")
        assert exitcode == 0, "0 flow entries have been shown" in output

        # check the entry is not there in app db
        self.app_db.wait_for_n_keys("NAPT_TABLE", 0)

        # check the entry is not there in asic db
        self.asic_db.wait_for_n_keys("ASIC_STATE:SAI_OBJECT_TYPE_NAT_ENTRY", 0)

# Add Dummy always-pass test at end as workaroud
# for issue when Flaky fail on final test it invokes module tear-down before retrying
def test_nonflaky_dummy():
    pass
