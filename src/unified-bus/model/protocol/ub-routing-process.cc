// SPDX-License-Identifier: GPL-2.0-only
#include "ns3/ub-controller.h"
#include "ns3/ub-queue-manager.h"
#include "ns3/ub-routing-process.h"
using namespace utils;

namespace ns3 {
NS_OBJECT_ENSURE_REGISTERED(UbRoutingProcess);
NS_LOG_COMPONENT_DEFINE("UbRoutingProcess");

/*-----------------------------------------UbRoutingProcess----------------------------------------------*/
TypeId UbRoutingProcess::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::UbRoutingProcess")
        .SetParent<Object>()
        .SetGroupName("UnifiedBus")
        .AddConstructor<UbRoutingProcess>();
    return tid;
}

UbRoutingProcess::UbRoutingProcess()
{
}

void UbRoutingProcess::AddShortestRoute(const uint32_t destIP, const std::vector<uint16_t>& outPorts)
{
    // 标准化端口集合（排序去重）
    std::vector<uint16_t> target;
    auto itRt = m_rtShortest.find(destIP);
    if (itRt != m_rtShortest.end()) {
        target.insert(target.end(), (*(itRt->second)).begin(), (*(itRt->second)).end());
    }
    target.insert(target.end(), outPorts.begin(), outPorts.end());
    std::vector<uint16_t> normalized = normalizePorts(outPorts);
    
    // 查找或创建共享端口集合
    auto it = m_portSetPool.find(normalized);
    if (it != m_portSetPool.end()) {
        //  已存在相同端口集合，共享指针
        m_rtShortest[destIP] = it->second;
    } else {
        // 创建新端口集合并加入池中
        auto sharedPorts = std::make_shared<std::vector<uint16_t>>(normalized);
        m_portSetPool[normalized] = sharedPorts;
        m_rtShortest[destIP] = sharedPorts;
    }
}

void UbRoutingProcess::AddOtherRoute(const uint32_t destIP, const std::vector<uint16_t>& outPorts)
{
    // 标准化端口集合（排序去重）
    std::vector<uint16_t> target;
    auto itRt = m_rtOther.find(destIP);
    if (itRt != m_rtOther.end()) {
        target.insert(target.end(), (*(itRt->second)).begin(), (*(itRt->second)).end());
    }
    target.insert(target.end(), outPorts.begin(), outPorts.end());
    std::vector<uint16_t> normalized = normalizePorts(target);
    
    // 查找或创建共享端口集合
    auto it = m_portSetPool.find(normalized);
    if (it != m_portSetPool.end()) {
        // 已存在相同端口集合，共享指针
        m_rtOther[destIP] = it->second;
    } else {
        // 创建新端口集合并加入池中
        auto sharedPorts = std::make_shared<std::vector<uint16_t>>(normalized);
        m_portSetPool[normalized] = sharedPorts;
        m_rtOther[destIP] = sharedPorts;
    }
}

const std::vector<uint16_t>& UbRoutingProcess::GetShortestOutPorts(const uint32_t destIP)
{
    static const std::vector<uint16_t> empty; // 返回空集的引用
    auto it = m_rtShortest.find(destIP);
    return it != m_rtShortest.end() ? *(it->second) : empty;
}

const std::vector<uint16_t>& UbRoutingProcess::GetOtherOutPorts(const uint32_t destIP)
{
    static const std::vector<uint16_t> empty; // 返回空集的引用
    auto it = m_rtOther.find(destIP);
    return it != m_rtOther.end() ? *(it->second) : empty;
}

const std::vector<uint16_t> UbRoutingProcess::GetAllOutPorts(const uint32_t destIP)
{
    std::vector<uint16_t> res;
    auto it = m_rtOther.find(destIP);
    if (it != m_rtOther.end()) {
        res.insert(res.end(), (*(it->second)).begin(), (*(it->second)).end());
    }
    it = m_rtShortest.find(destIP);
    if (it != m_rtOther.end()) {
        res.insert(res.end(), (*(it->second)).begin(), (*(it->second)).end());
    }
    return res;
}


// 删除路由条目
bool UbRoutingProcess::RemoveShortestRoute(const uint32_t destIP)
{
    return m_rtShortest.erase(destIP) > 0;
}

// 删除路由条目
bool UbRoutingProcess::RemoveOtherRoute(const uint32_t destIP)
{
    return m_rtOther.erase(destIP) > 0;
}

int UbRoutingProcess::GetOutPort(Ptr<Packet> packet, Ptr<UbQueueManager> queueManager, Ptr<UbController> ctrl)
{
    NS_ASSERT_MSG(0, "Not yet implemented!");
    return -1;
}

uint64_t UbRoutingProcess::CalcHash(uint32_t sip, uint32_t dip, uint16_t sport, uint16_t dport, uint8_t priority)
{
    uint8_t buf[13];
    buf[0] = (sip >> 24) & 0xff;
    buf[1] = (sip >> 16) & 0xff;
    buf[2] = (sip >> 8) & 0xff;
    buf[3] = sip & 0xff;
    buf[4] = (dip >> 24) & 0xff;
    buf[5] = (dip >> 16) & 0xff;
    buf[6] = (dip >> 8) & 0xff;
    buf[7] = dip & 0xff;
    buf[8] = (sport >> 8) & 0xff;
    buf[9] = sport & 0xff;
    buf[10] = (dport >> 8) & 0xff;
    buf[11] = dport & 0xff;
    buf[12] = priority;
    std::string str(reinterpret_cast<const char*>(buf), sizeof(buf));
    uint64_t hash = Hash64(str);
    return hash;
}

int UbRoutingProcess::SelectOutPort(uint32_t sip, uint32_t dip, uint16_t sport, uint16_t dport,
    uint8_t priority, bool useShortestPath, bool usePacketSpray, uint16_t inPortId)
{
    uint32_t idx = 0;
    std::string hashKey;
    uint64_t hash64 = 0;
    if (usePacketSpray) {
        hash64 = CalcHash(sip, dip, sport, dport, priority);
    } else {
        // usePacketSpray == LB_MODE_PER_FLOW
        hash64 = CalcHash(sip, dip, 0, 0, priority);
    }

    if (useShortestPath) {
        auto outPorts = GetShortestOutPorts(dip);
        std::vector<uint16_t> validPorts;
        for (uint16_t port : outPorts) {
            if (port != inPortId)
                validPorts.push_back(port);
        }
        if (validPorts.size() == 0)
            return -1;
        idx = hash64 % validPorts.size();
        return validPorts[idx];
    } else {
        // useShortestPath == ROUTING_ALL_PATHS
        auto outPorts = GetAllOutPorts(dip);
        std::vector<uint16_t> validPorts;
        for (uint16_t port : outPorts) {
            if (port != inPortId)
                validPorts.push_back(port);
        }
        if (validPorts.size() == 0)
            return -1;
        idx = hash64 % validPorts.size();
        auto shortestOutPorts = GetShortestOutPorts(dip);
        if (std::find(shortestOutPorts.begin(), shortestOutPorts.end(), validPorts[idx]) != shortestOutPorts.end()) {
            m_selectShortestPaths = true;
        } else {
            m_selectShortestPaths = false;
        }
        return validPorts[idx];
    }
}

bool UbRoutingProcess::GetSelectShortestPath()
{
    return m_selectShortestPaths;
}

int UbRoutingProcess::GetOutPort(RoutingKey &rtKey, uint16_t inPort)
{
    uint32_t sip = rtKey.sip;
    uint32_t dip = rtKey.dip;
    uint16_t sport = rtKey.sport;
    uint16_t dport = rtKey.dport;
    uint8_t priority = rtKey.priority;
    bool useShortestPath = rtKey.useShortestPath;
    bool usePacketSpray = rtKey.usePacketSpray;
    NS_LOG_DEBUG("[UbRoutingProcess GetOutPort]: sip: " << Ipv4Address(sip)
                << " dip: " << Ipv4Address(dip)
                << " sport: " << sport
                << " dport: " << dport
                << " priority: " << (uint16_t)priority
                << " useShortestPath: " << useShortestPath
                << " usePacketSpray: " << usePacketSpray);
    // 1. 首先基于目的节点的port地址进行选择
    int outPortId = SelectOutPort(sip, dip, sport, dport, priority, useShortestPath, usePacketSpray, inPort);
    if (outPortId == -1) {
        // 2. 如果找不到，掩盖port地址，使用主机的primary地址进行寻址
        Ipv4Mask mask("255.255.255.0");
        dip = Ipv4Address(dip).CombineMask(mask).Get();
        outPortId = SelectOutPort(sip, dip, sport, dport, priority, useShortestPath, usePacketSpray, inPort);
        // 3. 如果还是找不到，报ASSERT
        NS_ASSERT_MSG(outPortId != -1, "No available output port found");
    }
    return outPortId;
}
} // namespace ns3
