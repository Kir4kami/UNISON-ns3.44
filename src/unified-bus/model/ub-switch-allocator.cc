// SPDX-License-Identifier: GPL-2.0-only
#include "ns3/ub-switch-allocator.h"
#include "ns3/ub-switch.h"
#include "ns3/ub-port.h"
#include "protocol/ub-routing-process.h"
#include "ub-queue-manager.h"

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED(UbSwitchAllocator);
NS_LOG_COMPONENT_DEFINE("UbSwitchAllocator");

TypeId UbSwitchAllocator::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::UbSwitchAllocator")
        .SetParent<Object>()
        .AddConstructor<UbSwitchAllocator>()
        .AddAttribute("AllocationTime",
                      "Time of Allocation Used.",
                      TimeValue(NanoSeconds(10)),
                      MakeTimeAccessor(&UbSwitchAllocator::m_allocationTime),
                      MakeTimeChecker());
    return tid;
}
UbSwitchAllocator::UbSwitchAllocator()
{
}

UbSwitchAllocator::~UbSwitchAllocator()
{
}

void UbSwitchAllocator::TriggerAllocator(Ptr<UbPort> outPort)
{
}

void UbSwitchAllocator::Init()
{
}
void UbSwitchAllocator::RegisterUbIngressQueue(Ptr<UbIngressQueue> ingressQueue, uint32_t outPort, uint32_t priority)
{
    m_igsrc[outPort][priority].push_back(ingressQueue);
}

void UbSwitchAllocator::RegisterEgressStauts(uint32_t portsNum)
{
    m_egStatus.resize(portsNum, true);
}

void UbSwitchAllocator::SetEgressStatus(uint32_t portId, bool status)
{
    m_egStatus[portId] = status;
}

bool UbSwitchAllocator::GetEgressStatus(uint32_t portId)
{
    return m_egStatus[portId];
}

/*-----------------------------------------UbRoundRobinAllocator----------------------------------------------*/
TypeId UbRoundRobinAllocator::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::UbRoundRobinAllocator")
        .SetParent<UbSwitchAllocator>()
        .AddConstructor<UbRoundRobinAllocator>();
    return tid;
}

void UbRoundRobinAllocator::Init()
{
    auto node = NodeList::GetNode(m_nodeId);
    uint32_t portsNum = node->GetNDevices();
    auto vlNum = node->GetObject<UbSwitch>()->GetVLNum();
    m_rrIdx.resize(portsNum);
    for (auto &v: m_rrIdx) {
        v.resize(vlNum, 0);
    }
    m_igsrc.resize(portsNum);
    m_isRunning.resize(portsNum, false);
    for (auto &i : m_igsrc) {
        i.resize(vlNum);
    }
}

void UbRoundRobinAllocator::TriggerAllocator(Ptr<UbPort> outPort)
{
    auto outPortId = outPort->GetIfIndex();
    if (m_isRunning[outPortId]) {
        Simulator::ScheduleNow(&UbPort::NotifyAllocationFinish, outPort);
        return;
    }
    // 轮询调度
    auto ingressQueue = DispatchPacket(outPortId);
    if (ingressQueue != nullptr) {
        // 调度得到需要发送的包，则认为调度算法需要 m_allocationTime 时间才能得到结果
        m_isRunning[outPortId] = true;
        Simulator::Schedule(m_allocationTime, &UbRoundRobinAllocator::AddPacketToEgressQueue,
                            this, outPort, ingressQueue);
        Simulator::Schedule(m_allocationTime, &UbPort::NotifyAllocationFinish, outPort);
    } else {
        // 无包需发送，则认为调度算法不需要时间，解除port调度状态，防止调度状态下有新包
        Simulator::ScheduleNow(&UbPort::NotifyAllocationFinish, outPort);
    }
}

void UbRoundRobinAllocator::AddPacketToEgressQueue(Ptr<UbPort> outPort, Ptr<UbIngressQueue> ingressQueue)
{
    auto outPortId = outPort->GetIfIndex();
    m_isRunning[outPortId] = false;
    outPort->GetUbQueue()->DoEnqueue(ingressQueue);
}

Ptr<UbIngressQueue> UbRoundRobinAllocator::DispatchPacket(uint32_t outPort)
{
    uint32_t idx;
    uint32_t pi;
    uint32_t outPortId = outPort;
    auto node = NodeList::GetNode(m_nodeId);
    auto vlNum = node->GetObject<UbSwitch>()->GetVLNum();
    for (pi = 0 ; pi < vlNum; pi++) {
        size_t qSize = m_igsrc[outPortId][pi].size();
        for (idx = 0; idx < qSize; idx++) {
            auto qidx = (idx + m_rrIdx[outPortId][pi]) % qSize;
            if (!m_igsrc[outPortId][pi][qidx]->IsEmpty()) {
                m_rrIdx[outPortId][pi] = (qidx + 1) % qSize;
                NS_LOG_DEBUG("[UbSwitchAllocator DispatchPacket] " << " NodeId: " << node->GetId()
                << " PortId: " << outPortId <<" qidx: "<< qidx);
                return m_igsrc[outPortId][pi][qidx];
            }
        }
    }
    return nullptr;
}

} // namespae ns3
