#ifndef __VNETORCH_H
#define __VNETORCH_H

#include <vector>
#include <set>
#include <unordered_map>
#include <algorithm>
#include <bitset>

#include "request_parser.h"
#include "ipaddresses.h"

#define VNET_BITMAP_SIZE 32
#define VNET_TUNNEL_SIZE 512
#define VNET_NEIGHBOR_MAX 0xffff

extern sai_object_id_t gVirtualRouterId;

const request_description_t vnet_request_description = {
    { REQ_T_STRING },
    {
        { "src_mac",       REQ_T_MAC_ADDRESS },
        { "vxlan_tunnel",  REQ_T_STRING },
        { "vni",           REQ_T_UINT },
        { "peer_list",     REQ_T_SET },
        { "guid",          REQ_T_STRING },
    },
    { "vxlan_tunnel", "vni" } // mandatory attributes
};

enum class VNET_EXEC
{
    VNET_EXEC_VRF,
    VNET_EXEC_BRIDGE,
    VNET_EXEC_INVALID
};

enum class VR_TYPE
{
    ING_VR_VALID,
    EGR_VR_VALID,
    VR_INVALID
};

struct VNetInfo
{
    string tunnel;
    uint32_t vni;
    set<string> peers;
};

typedef map<VR_TYPE, sai_object_id_t> vrid_list_t;
extern std::vector<VR_TYPE> vr_cntxt;

class VNetRequest : public Request
{
public:
    VNetRequest() : Request(vnet_request_description, ':') { }
};

struct tunnelEndpoint
{
    IpAddress ip;
    MacAddress mac;
    uint32_t vni;
};

class VNetObject
{
public:
    VNetObject(const VNetInfo& vnetInfo) :
               tunnel_(vnetInfo.tunnel),
               peer_list_(vnetInfo.peers),
               vni_(vnetInfo.vni)
               { }

    virtual bool updateObj(vector<sai_attribute_t>&) = 0;

    void setPeerList(set<string>& p_list)
    {
        peer_list_ = p_list;
    }

    const set<string>& getPeerList() const
    {
        return peer_list_;
    }

    string getTunnelName() const
    {
        return tunnel_;
    }

    uint32_t getVni() const
    {
        return vni_;
    }

    virtual ~VNetObject() {};

private:
    set<string> peer_list_ = {};
    string tunnel_;
    uint32_t vni_;
};

struct nextHop
{
    IpAddresses ips;
    string ifname;
};

typedef std::map<IpPrefix, tunnelEndpoint> TunnelRoutes;
typedef std::map<IpPrefix, nextHop> RouteMap;

class VNetVrfObject : public VNetObject
{
public:
    VNetVrfObject(const string& vnet, const VNetInfo& vnetInfo, vector<sai_attribute_t>& attrs);

    sai_object_id_t getVRidIngress() const;

    sai_object_id_t getVRidEgress() const;

    set<sai_object_id_t> getVRids() const;

    sai_object_id_t getEncapMapId() const
    {
        return getVRidIngress();
    }

    sai_object_id_t getDecapMapId() const
    {
        return getVRidEgress();
    }

    sai_object_id_t getVRid() const
    {
        return getVRidIngress();
    }

    bool createObj(vector<sai_attribute_t>&);

    bool updateObj(vector<sai_attribute_t>&);

    bool addRoute(IpPrefix& ipPrefix, tunnelEndpoint& endp);
    bool addRoute(IpPrefix& ipPrefix, nextHop& nh);
    bool removeRoute(IpPrefix& ipPrefix);

    size_t getRouteCount() const;
    bool getRouteNextHop(IpPrefix& ipPrefix, nextHop& nh);
    bool hasRoute(IpPrefix& ipPrefix);

    sai_object_id_t getTunnelNextHop(tunnelEndpoint& endp);
    bool removeTunnelNextHop(tunnelEndpoint& endp);

    ~VNetVrfObject();

private:
    string vnet_name_;
    vrid_list_t vr_ids_;

    TunnelRoutes tunnels_;
    RouteMap routes_;
};

struct VnetBridgeInfo
{
    sai_object_id_t bridge_id;
    sai_object_id_t bridge_port_rif_id;
    sai_object_id_t bridge_port_tunnel_id;
    sai_object_id_t rif_id;
};

class VNetBitmapObject: public VNetObject
{
public:
    VNetBitmapObject(const string& vnet, const VNetInfo& vnetInfo, vector<sai_attribute_t>& attrs);

    virtual bool addIntf(const string& alias, const IpPrefix *prefix);

    virtual bool addTunnelRoute(IpPrefix& ipPrefix, tunnelEndpoint& endp);

    virtual bool addRoute(IpPrefix& ipPrefix, nextHop& nh);

    void setVniInfo(uint32_t vni);

    bool updateObj(vector<sai_attribute_t>&);

    virtual ~VNetBitmapObject() {}

private:
    static uint32_t getFreeBitmapId(const string& name);

    static uint32_t getBitmapId(const string& name);

    static void recycleBitmapId(uint32_t id);

    static uint32_t getFreeTunnelRouteTableOffset();

    static void recycleTunnelRouteTableOffset(uint32_t offset);

    static VnetBridgeInfo getBridgeInfoByVni(uint32_t vni, string tunnelName);

    static uint32_t getFreeNeighbor(void);

    static std::bitset<VNET_BITMAP_SIZE> vnetBitmap_;
    static map<string, uint32_t> vnetIds_;
    static std::bitset<VNET_TUNNEL_SIZE> tunnelOffsets_;
    static map<uint32_t, VnetBridgeInfo> bridgeInfoMap_;
    static map<tuple<MacAddress, sai_object_id_t>, sai_fdb_entry_t> fdbMap_;
    static map<tuple<MacAddress, sai_object_id_t>, sai_neighbor_entry_t> neighMap_;

    uint32_t vnet_id_;
};

typedef std::unique_ptr<VNetObject> VNetObject_T;
typedef std::unordered_map<std::string, VNetObject_T> VNetTable;

class VNetOrch : public Orch2
{
public:
    VNetOrch(DBConnector *db, const std::string&, VNET_EXEC op = VNET_EXEC::VNET_EXEC_VRF);

    bool setIntf(const string& alias, const string name, const IpPrefix *prefix = nullptr);

    bool isVnetExists(const std::string& name) const
    {
        return vnet_table_.find(name) != std::end(vnet_table_);
    }

    template <class T>
    T* getTypePtr(const std::string& name) const
    {
        return static_cast<T *>(vnet_table_.at(name).get());
    }

    const set<string>& getPeerList(const std::string& name) const
    {
        return vnet_table_.at(name)->getPeerList();
    }

    string getTunnelName(const std::string& name) const
    {
        return vnet_table_.at(name)->getTunnelName();
    }

    bool isVnetExecVrf() const
    {
        return (vnet_exec_ == VNET_EXEC::VNET_EXEC_VRF);
    }

    bool isVnetExecBridge() const
    {
        return (vnet_exec_ == VNET_EXEC::VNET_EXEC_BRIDGE);
    }

private:
    virtual bool addOperation(const Request& request);
    virtual bool delOperation(const Request& request);

    template <class T>
    std::unique_ptr<T> createObject(const string&, const VNetInfo&, vector<sai_attribute_t>&);

    VNetTable vnet_table_;
    VNetRequest request_;
    VNET_EXEC vnet_exec_;

};

const request_description_t vnet_route_description = {
    { REQ_T_STRING, REQ_T_IP_PREFIX },
    {
        { "endpoint",    REQ_T_IP },
        { "ifname",      REQ_T_STRING },
        { "nexthop",     REQ_T_STRING },
        { "vni",         REQ_T_UINT },
        { "mac_address", REQ_T_MAC_ADDRESS },
    },
    { }
};

class VNetRouteRequest : public Request
{
public:
    VNetRouteRequest() : Request(vnet_route_description, ':') { }
};

class VNetRouteOrch : public Orch2
{
public:
    VNetRouteOrch(DBConnector *db, vector<string> &tableNames, VNetOrch *);

    typedef pair<string, bool (VNetRouteOrch::*) (const Request& )> handler_pair;
    typedef map<string, bool (VNetRouteOrch::*) (const Request& )> handler_map;

private:
    virtual bool addOperation(const Request& request);
    virtual bool delOperation(const Request& request);

    bool handleRoutes(const Request&);
    bool handleTunnel(const Request&);

    template<typename T>
    bool doRouteTask(const string& vnet, IpPrefix& ipPrefix, tunnelEndpoint& endp, string& op);

    template<typename T>
    bool doRouteTask(const string& vnet, IpPrefix& ipPrefix, nextHop& nh, string& op);

    VNetOrch *vnet_orch_;
    VNetRouteRequest request_;
    handler_map handler_map_;
};

#endif // __VNETORCH_H
