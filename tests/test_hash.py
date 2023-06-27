import pytest
import logging


logging.basicConfig(level=logging.INFO)
hashlogger = logging.getLogger(__name__)


HASH_FIELD_LIST = [
    "DST_MAC",
    "SRC_MAC",
    "ETHERTYPE",
    "IP_PROTOCOL",
    "DST_IP",
    "SRC_IP",
    "L4_DST_PORT",
    "L4_SRC_PORT"
]
INNER_HASH_FIELD_LIST = [
    "INNER_DST_MAC",
    "INNER_SRC_MAC",
    "INNER_ETHERTYPE",
    "INNER_IP_PROTOCOL",
    "INNER_DST_IP",
    "INNER_SRC_IP",
    "INNER_L4_DST_PORT",
    "INNER_L4_SRC_PORT"
]
DEFAULT_HASH_FIELD_LIST = [
    "DST_MAC",
    "SRC_MAC",
    "ETHERTYPE",
    "IN_PORT"
]

SAI_HASH_FIELD_LIST = [
    "SAI_NATIVE_HASH_FIELD_DST_MAC",
    "SAI_NATIVE_HASH_FIELD_SRC_MAC",
    "SAI_NATIVE_HASH_FIELD_ETHERTYPE",
    "SAI_NATIVE_HASH_FIELD_IP_PROTOCOL",
    "SAI_NATIVE_HASH_FIELD_DST_IP",
    "SAI_NATIVE_HASH_FIELD_SRC_IP",
    "SAI_NATIVE_HASH_FIELD_L4_DST_PORT",
    "SAI_NATIVE_HASH_FIELD_L4_SRC_PORT"
]
SAI_INNER_HASH_FIELD_LIST = [
    "SAI_NATIVE_HASH_FIELD_INNER_DST_MAC",
    "SAI_NATIVE_HASH_FIELD_INNER_SRC_MAC",
    "SAI_NATIVE_HASH_FIELD_INNER_ETHERTYPE",
    "SAI_NATIVE_HASH_FIELD_INNER_IP_PROTOCOL",
    "SAI_NATIVE_HASH_FIELD_INNER_DST_IP",
    "SAI_NATIVE_HASH_FIELD_INNER_SRC_IP",
    "SAI_NATIVE_HASH_FIELD_INNER_L4_DST_PORT",
    "SAI_NATIVE_HASH_FIELD_INNER_L4_SRC_PORT"
]
SAI_DEFAULT_HASH_FIELD_LIST = [
    "SAI_NATIVE_HASH_FIELD_DST_MAC",
    "SAI_NATIVE_HASH_FIELD_SRC_MAC",
    "SAI_NATIVE_HASH_FIELD_ETHERTYPE",
    "SAI_NATIVE_HASH_FIELD_IN_PORT"
]


@pytest.mark.usefixtures("dvs_hash_manager")
class TestHashBasicFlows:
    @pytest.fixture(scope="class")
    def hashData(self, dvs_hash_manager):
        hashlogger.info("Initialize HASH data")

        hashlogger.info("Verify HASH count")
        self.dvs_hash.verify_hash_count(0)

        hashlogger.info("Get ECMP/LAG HASH id")
        hashIdList = sorted(self.dvs_hash.get_hash_ids())

        # Assumption: VS has only two HASH objects: ECMP, LAG
        meta_dict = {
            "ecmp": hashIdList[0],
            "lag": hashIdList[1]
        }

        yield meta_dict

        hashlogger.info("Deinitialize HASH data")

    @pytest.mark.parametrize(
        "hash,field", [
            pytest.param(
                "ecmp",
                "ecmp_hash",
                id="ecmp-hash"
            ),
            pytest.param(
                "lag",
                "lag_hash",
                id="lag-hash"
            )
        ]
    )
    @pytest.mark.parametrize(
        "hfList,saiHfList", [
            pytest.param(
                ",".join(HASH_FIELD_LIST),
                SAI_HASH_FIELD_LIST,
                id="outer-frame"
            ),
            pytest.param(
                ",".join(INNER_HASH_FIELD_LIST),
                SAI_INNER_HASH_FIELD_LIST,
                id="inner-frame"
            )
        ]
    )
    def test_HashSwitchGlobalConfiguration(self, hash, field, hfList, saiHfList, testlog, hashData):
        attr_dict = {
            field: hfList
        }

        hashlogger.info("Update {} hash".format(hash.upper()))
        self.dvs_hash.update_switch_hash(
            qualifiers=attr_dict
        )

        hashId = hashData[hash]
        sai_attr_dict = {
            "SAI_HASH_ATTR_NATIVE_HASH_FIELD_LIST": saiHfList
        }

        hashlogger.info("Validate {} hash".format(hash.upper()))
        self.dvs_hash.verify_hash_generic(
            sai_hash_id=hashId,
            sai_qualifiers=sai_attr_dict
        )

    @pytest.mark.parametrize(
        "hash,field", [
            pytest.param(
                "ecmp",
                "ecmp_hash",
                id="ecmp-hash"
            ),
            pytest.param(
                "lag",
                "lag_hash",
                id="lag-hash"
            )
        ]
    )
    def test_HashDefaultSwitchGlobalConfiguration(self, hash, field, testlog, hashData):
        attr_dict = {
            field: ",".join(DEFAULT_HASH_FIELD_LIST)
        }

        hashlogger.info("Update {} hash".format(hash.upper()))
        self.dvs_hash.update_switch_hash(
            qualifiers=attr_dict
        )

        hashId = hashData[hash]
        sai_attr_dict = {
            "SAI_HASH_ATTR_NATIVE_HASH_FIELD_LIST": SAI_DEFAULT_HASH_FIELD_LIST
        }

        hashlogger.info("Validate {} hash".format(hash.upper()))
        self.dvs_hash.verify_hash_generic(
            sai_hash_id=hashId,
            sai_qualifiers=sai_attr_dict
        )


# Add Dummy always-pass test at end as workaroud
# for issue when Flaky fail on final test it invokes module tear-down before retrying
def test_nonflaky_dummy():
    pass
