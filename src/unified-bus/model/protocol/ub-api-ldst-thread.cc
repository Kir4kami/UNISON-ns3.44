// SPDX-License-Identifier: GPL-2.0-only
#include "ub-api-ldst-thread.h"

#include "../../../core/model/log.h"
#include "../ub-queue-manager.h"
#include "ub-routing-process.h"

#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/ub-controller.h"
#include "ns3/ub-datatype.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("UbApiLdstThread");

NS_OBJECT_ENSURE_REGISTERED(UbApiLdstThread);

TypeId UbApiLdstThread::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::UbApiLdstThread")
                            .SetParent<Object>()
                            .SetGroupName("UnifiedBus")
                            .AddAttribute("StoreOutstanding",
                                          "Maximum number of outstanding STORE requests this thread may issue.",
                                          UintegerValue(64),
                                          MakeUintegerAccessor(&UbApiLdstThread::m_storeOutstanding),
                                          MakeUintegerChecker<uint32_t>())
                            .AddAttribute("LoadOutstanding",
                                          "Maximum number of outstanding LOAD requests this thread may issue.",
                                          UintegerValue(64),
                                          MakeUintegerAccessor(&UbApiLdstThread::m_loadOutstanding),
                                          MakeUintegerChecker<uint32_t>())
                            .AddAttribute("LoadRequestSize",
                                          "Payload size (bytes) for each LOAD request.",
                                          UintegerValue(64),
                                          MakeUintegerAccessor(&UbApiLdstThread::m_loadReqSize),
                                          MakeUintegerChecker<uint32_t>())
                            .AddAttribute("QueuePriority",
                                          "Queue (VOQ) priority for packets emitted by this thread.",
                                          UintegerValue(1),
                                          MakeUintegerAccessor(&UbApiLdstThread::m_queuePriority),
                                          MakeUintegerChecker<uint32_t>())
                            .AddAttribute("UsePacketSpray",
                                          "Enable per-packet load balancing across equal-cost paths.",
                                          BooleanValue(true),
                                          MakeBooleanAccessor(&UbApiLdstThread::m_usePacketSpray),
                                          MakeBooleanChecker())
                            .AddAttribute("UseShortestPaths",
                                          "Restrict routing to shortest paths only.",
                                          BooleanValue(true),
                                          MakeBooleanAccessor(&UbApiLdstThread::m_useShortestPaths),
                                          MakeBooleanChecker())
                            .AddTraceSource("MemTaskStartsNotify",
                                            "Emitted when a memory task starts on this thread.",
                                            MakeTraceSourceAccessor(&UbApiLdstThread::m_traceMemTaskStartsNotify),
                                            "ns3::UbApiLdstThread::MemTaskStartsNotify")
                            .AddTraceSource("FirstPacketSendsNotify",
                                            "Emitted when the first packet of a memory task is sent.",
                                            MakeTraceSourceAccessor(&UbApiLdstThread::m_traceFirstPacketSendsNotify),
                                            "ns3::UbApiLdstThread::FirstPacketSendsNotify")
                            .AddTraceSource("LastPacketSendsNotify",
                                            "Emitted when the last packet of a memory task is sent.",
                                            MakeTraceSourceAccessor(&UbApiLdstThread::m_traceLastPacketSendsNotify),
                                            "ns3::UbApiLdstThread::LastPacketSendsNotify");

    return tid;
}

UbApiLdstThread::UbApiLdstThread()
{
    NS_LOG_DEBUG("UbApiLdstThread created");
}

UbApiLdstThread::~UbApiLdstThread()
{
}

void UbApiLdstThread::SetUbLdstThread(uint32_t nodeId, uint32_t ldstThreadNum,
                                      uint32_t storeReqSize)
{
    m_nodeId = nodeId;
    m_threadNum = ldstThreadNum;
    m_storeReqSize = storeReqSize;
}

void UbApiLdstThread::PushMemTask(Ptr<UbMemTask> ubMemTask)
{
    if (ubMemTask->GetType() == UbMemOperationType::STORE) {
        m_memStoreTaskQueue.push_back(ubMemTask);
    } else if (ubMemTask->GetType() == UbMemOperationType::LOAD) {
        m_memLoadTaskQueue.push_back(ubMemTask);
    }
    m_taskidSendCnt[ubMemTask->GetMemTaskId()] = 0;
    MemTaskStartsNotify(m_nodeId, ubMemTask->GetMemTaskId());
    GenPacketAndSend();
}

void UbApiLdstThread::GenPacketAndSend()
{
    if (m_storeOutstanding > 0 && m_memStoreTaskQueue.size() > 0) {
        NS_LOG_INFO("MEM Task Starts, taskId: "<< std::to_string(m_memStoreTaskQueue.front()->GetMemTaskId()));
    }
    // store发送
    while (m_storeOutstanding > 0 && m_memStoreTaskQueue.size() > 0) {
        Ptr<UbMemTask> currentMemTask = m_memStoreTaskQueue.front();
        if (currentMemTask == nullptr) {
            continue;
        }
        // 组数据包进行发送
        uint64_t payloadSize = currentMemTask->GetBytesLeft();
        if (payloadSize > m_storeReqSize) {
            payloadSize = m_storeReqSize;
        }

        // 调用路由算法，找到出端口
        RoutingKey rtKey;
        rtKey.sip = utils::NodeIdToIp(currentMemTask->GetSrc()).Get();
        rtKey.dip = utils::NodeIdToIp(currentMemTask->GetDest()).Get();
        rtKey.sport = m_lbHashSalt;
        rtKey.dport = 0;
        rtKey.priority = m_queuePriority;
        rtKey.useShortestPath = m_useShortestPaths;
        rtKey.usePacketSpray = m_usePacketSpray;

        auto node = NodeList::GetNode(m_nodeId);
        auto sw = node->GetObject<UbSwitch>();
        int outPort = sw->GetRoutingProcess()->GetOutPort(rtKey);
        if (outPort < 0) {
            // Route failed
            NS_ASSERT_MSG(0, "The route cannot be found");
        }
        uint16_t destPort = outPort;
        
        // 生成包头
        Ptr<Packet> p = GenDataPacket(currentMemTask, payloadSize, destPort);
        if (m_usePacketSpray) {
            if (m_lbHashSalt == MAX_LB) {
                m_lbHashSalt = MIN_LB;
            } else {
                m_lbHashSalt++;
            }
        }
        
        // 发到队列 如果要从port0发，就放到port0 优先级为1的voq
        sw->AddPktToVoq(p, destPort, m_queuePriority, destPort);
        Ptr<UbPort> port = DynamicCast<UbPort>(node->GetDevice(destPort));
        port->TriggerTransmit();

        currentMemTask->UpdateSentBytes(payloadSize);
        m_taskidSendCnt[currentMemTask->GetMemTaskId()]++;
        if (m_taskidSendCnt[currentMemTask->GetMemTaskId()] == 1) {
            // 加入trace
            FirstPacketSendsNotify(m_nodeId, currentMemTask->GetMemTaskId());
        }

        m_storeOutstanding--;

        // 所有包都发完
        if (m_taskidSendCnt[currentMemTask->GetMemTaskId()] == currentMemTask->GetPsnSize()) {
            // 加入trace
            LastPacketSendsNotify(m_nodeId, currentMemTask->GetMemTaskId());
            m_memStoreTaskQueue.pop_front();
        }
    }

    if (m_loadOutstanding > 0 && m_memLoadTaskQueue.size() > 0) {
        NS_LOG_INFO("MEM Task Starts, taskId: " << std::to_string(m_memLoadTaskQueue.front()->GetMemTaskId()));
    }
    // load发送
    while (m_loadOutstanding > 0 && m_memLoadTaskQueue.size() > 0) {
        Ptr<UbMemTask> currentMemTask = m_memLoadTaskQueue.front();
        if (currentMemTask == nullptr) {
            continue;
        }

        // 调用路由算法，找到出端口
        RoutingKey rtKey;
        rtKey.sip = utils::NodeIdToIp(currentMemTask->GetSrc()).Get();
        rtKey.dip = utils::NodeIdToIp(currentMemTask->GetDest()).Get();
        rtKey.sport = m_lbHashSalt;
        rtKey.dport = 0;
        rtKey.priority = m_queuePriority;
        rtKey.useShortestPath = m_useShortestPaths;
        rtKey.usePacketSpray = m_usePacketSpray;
        auto node = NodeList::GetNode(m_nodeId);
        auto sw = node->GetObject<UbSwitch>();
        int outPort = sw->GetRoutingProcess()->GetOutPort(rtKey);
        if (outPort < 0) {
            // Route failed
            NS_ASSERT_MSG(0, "The route cannot be found");
        }
        uint16_t destPort = outPort;
        
        // 生成包头 64大小
        Ptr<Packet> p = GenDataPacket(currentMemTask, m_loadReqSize, destPort);
        if (m_usePacketSpray) {
            if (m_lbHashSalt == MAX_LB) {
                m_lbHashSalt = MIN_LB;
            } else {
                m_lbHashSalt++;
            }
        }

        // 发到队列 如果要从port0发，就放到port0->port0 优先级为1的voq
        sw->AddPktToVoq(p, destPort, m_queuePriority, destPort);
        Ptr<UbPort> port = DynamicCast<UbPort>(node->GetDevice(destPort));
        port->TriggerTransmit();

        m_taskidSendCnt[currentMemTask->GetMemTaskId()]++;
        if (m_taskidSendCnt[currentMemTask->GetMemTaskId()] == 1) {
            FirstPacketSendsNotify(m_nodeId, currentMemTask->GetMemTaskId());
        }
        m_loadOutstanding--;

        // 所有包都发完
        if (m_taskidSendCnt[currentMemTask->GetMemTaskId()] == currentMemTask->GetPsnSize()) {
            LastPacketSendsNotify(m_nodeId, currentMemTask->GetMemTaskId());
            m_memLoadTaskQueue.pop_front();
        }
    }
}

void UbApiLdstThread::IncreaseOutstanding(UbMemOperationType type)
{
    if (type == UbMemOperationType::STORE) {
        m_storeOutstanding++;
    } else if (type == UbMemOperationType::LOAD) {
        m_loadOutstanding++;
    }
}

uint32_t UbApiLdstThread::GetThreadNum()
{
    return m_threadNum;
}

void UbApiLdstThread::SetUsePacketSpray(bool usePacketSpray)
{
    m_usePacketSpray = usePacketSpray;
}

void UbApiLdstThread::SetUseShortestPaths(bool useShortestPaths)
{
    m_useShortestPaths = useShortestPaths;
}

Ptr<Packet> UbApiLdstThread::GenDataPacket(Ptr<UbMemTask> memTask, uint32_t payloadSize, uint16_t destPort)
{
    NS_LOG_DEBUG("GenDataPacket");
    // Store/load request: DLH cNTH cTAH(0x03/0x06) [cMAETAH] Payload
    Ptr<Packet> p = Create<Packet>(payloadSize);
    UbCompactMAExtTah cMAETah;
    cMAETah.SetLength((uint8_t)payloadSize);
    p->AddHeader(cMAETah);
    // add cTAH (Compact Transaction Header)
    UbCompactTransactionHeader cTaHeader;
    if (memTask->GetType() == UbMemOperationType::STORE) {
        cTaHeader.SetTaOpcode(TaOpcode::TA_OPCODE_WRITE);
    } else if (memTask->GetType() == UbMemOperationType::LOAD) {
        cTaHeader.SetTaOpcode(TaOpcode::TA_OPCODE_READ);
    }

    cTaHeader.SetIniTaSsn(memTask->GetMemTaskId()); // taskid
    p->AddHeader(cTaHeader);

    // add MemHeader
    UbCna16NetworkHeader memHeader;
    uint16_t scna = static_cast<uint16_t>(utils::NodeIdToCna16(memTask->GetSrc()));
    memHeader.SetScna(scna);
    uint16_t dcna = static_cast<uint16_t>(utils::NodeIdToCna16(memTask->GetDest()));
    memHeader.SetDcna(dcna);
    memHeader.SetLb(m_lbHashSalt);
    memHeader.SetServiceLevel(1);  // memory store和load语义会固定使用某个vl
    p->AddHeader(memHeader);

    // add dl header
    UbDataLink::GenPacketHeader(p, false, false, m_queuePriority, m_queuePriority,
                                m_usePacketSpray, m_useShortestPaths, UbDatalinkHeaderConfig::PACKET_UB_MEM);
    NS_LOG_DEBUG("GenDataPacket successful!!!");
    return p;
}

void UbApiLdstThread::DoDispose()
{
    NS_LOG_DEBUG(this);
    m_taskidSendCnt.clear();
    for (auto i = m_memStoreTaskQueue.begin(); i != m_memStoreTaskQueue.end(); i++) {
        *i = nullptr;
    }
    m_memStoreTaskQueue.clear();
    for (auto i = m_memLoadTaskQueue.begin(); i != m_memLoadTaskQueue.end(); i++) {
        *i = nullptr;
    }
    m_memLoadTaskQueue.clear();
}

void UbApiLdstThread::MemTaskStartsNotify(uint32_t nodeId, uint32_t memTaskId)
{
    m_traceMemTaskStartsNotify(nodeId, memTaskId);
}

void UbApiLdstThread::FirstPacketSendsNotify(uint32_t nodeId, uint32_t memTaskId)
{
    m_traceFirstPacketSendsNotify(nodeId, memTaskId);
}
 
void UbApiLdstThread::LastPacketSendsNotify(uint32_t nodeId, uint32_t memTaskId)
{
    m_traceLastPacketSendsNotify(nodeId, memTaskId);
}
} // namespace ns3
