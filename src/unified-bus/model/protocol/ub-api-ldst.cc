// SPDX-License-Identifier: GPL-2.0-only
#include "ns3/log.h"
#include "ns3/simulator.h"

#include "ns3/ub-datatype.h"
#include "ns3/ub-controller.h"
#include "ub-api-ldst.h"

namespace ns3 {
NS_LOG_COMPONENT_DEFINE("UbApiLdst");

NS_OBJECT_ENSURE_REGISTERED(UbApiLdst);

TypeId UbApiLdst::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::UbApiLdst")
                            .SetParent<Object>()
                            .SetGroupName("UnifiedBus")
                            .AddAttribute("ThreadNum",
                                          "Number of LDST worker threads.",
                                          UintegerValue(10),
                                          MakeUintegerAccessor(&UbApiLdst::m_threadNum),
                                          MakeUintegerChecker<uint32_t>())
                            .AddAttribute("LoadResponseSize",
                                          "Payload size in bytes for LOAD responses.",
                                          UintegerValue(512),
                                          MakeUintegerAccessor(&UbApiLdst::m_loadRspSize),
                                          MakeUintegerChecker<uint32_t>())
                            .AddAttribute("StoreRequestSize",
                                          "Payload size in bytes for STORE requests.",
                                          UintegerValue(512),
                                          MakeUintegerAccessor(&UbApiLdst::m_storeReqSize),
                                          MakeUintegerChecker<uint32_t>())
                            .AddAttribute("QueuePriority",
                                          "VOQ priority used when enqueueing packets.",
                                          UintegerValue(1),
                                          MakeUintegerAccessor(&UbApiLdst::m_queuePriority),
                                          MakeUintegerChecker<uint32_t>())
                            .AddTraceSource("LastPacketACKsNotify",
                                            "Emitted when the last packet of a task is ACKed.",
                                            MakeTraceSourceAccessor(&UbApiLdst::m_traceLastPacketACKsNotify),
                                            "ns3::UbApiLdst::LastPacketACKsNotify")
                            .AddTraceSource("PeerSendFirstPacketACKsNotify",
                                            "Emitted when the peer ACKs the first packet of a task.",
                                            MakeTraceSourceAccessor(&UbApiLdst::m_tracePeerSendFirstPacketACKsNotify),
                                            "ns3::UbApiLdst::PeerSendFirstPacketACKsNotify")
                            .AddTraceSource("MemTaskCompletesNotify",
                                            "Emitted when a memory task completes.",
                                            MakeTraceSourceAccessor(&UbApiLdst::m_traceMemTaskCompletesNotify),
                                            "ns3::UbApiLdst::MemTaskCompletesNotify");

    return tid;
}

UbApiLdst::UbApiLdst()
{
    NS_LOG_DEBUG("UbApiLdst created");
}

UbApiLdst::~UbApiLdst()
{
}

void UbApiLdst::SetUbLdst(uint32_t nodeId)
{
    m_nodeId = nodeId;
    for (size_t i = 0; i < m_threadNum; i++) {
        Ptr<UbApiLdstThread> ldstThread = CreateObject<UbApiLdstThread>();
        if (ldstThread != nullptr) {
            ldstThread->SetUbLdstThread(nodeId, i, m_storeReqSize);
        }
        m_ldstThreadVector.push_back(ldstThread);
    }
}

uint32_t UbApiLdst::GetThreadNum()
{
    return m_threadNum;
}

std::vector<Ptr<UbApiLdstThread>> UbApiLdst::GetLdstThreads()
{
    return m_ldstThreadVector;
}

void UbApiLdst::PushMemTask(uint32_t src, uint32_t dest, uint32_t size, uint32_t taskId,
                            UbMemOperationType type, uint32_t threadId)
{
    NS_LOG_DEBUG("PushMemTask, threadId: " << std::to_string(threadId));
    // 判断threadid是否在合理范围
    if (threadId < 0 || threadId >= m_threadNum) {
        NS_ASSERT_MSG(0, "threadId  out of range, threadId: " << threadId);
    }
    // 判断任务是否存在
    if (m_taskThreadMap.find(taskId) != m_taskThreadMap.end()) {
        NS_ASSERT_MSG(0, "taskid  exists");
    }
     // 创建新的task
    Ptr<UbMemTask> ubMemTask = CreateObject<UbMemTask>();
    ubMemTask->SetSrc(src);
    ubMemTask->SetDest(dest);
    if (type == UbMemOperationType::STORE) {
        ubMemTask->SetSize(size, m_storeReqSize);
    } else if (type == UbMemOperationType::LOAD) {
        ubMemTask->SetSize(size, m_loadRspSize);
    } else {
        NS_ASSERT_MSG(0, "task type is wrong");
    }
    
    ubMemTask->SetMemTaskId(taskId);
    ubMemTask->SetType(type);
    m_memTaskQueue.push_back(ubMemTask);

    m_taskThreadMap[taskId] = threadId;
    m_taskTypeMap[taskId] = type;
    m_taskAckcountMap[taskId] = 0;
    GetUbLdstThreadByThreadId(threadId)->PushMemTask(ubMemTask);
}

void UbApiLdst::SetClientCallback(Callback<void, uint32_t> cb)
{
    FinishCallback = cb;
}

/**
 * 接收到一个数据包，调用此函数处理，产生ack
 */
void UbApiLdst::RecvDataPacket(Ptr<Packet> packet, uint8_t vlIndex, uint8_t vl, uint8_t type)
{
    // Store/load request: DLH cNTH cTAH(0x03/0x06) [cMAETAH] Payload
    // Store/load response: DLH cNTH cATAH(0x11/0x12) Payload
    NS_LOG_DEBUG("RecvDataPacket");
    if (packet == nullptr) {
        NS_LOG_ERROR("Null packet received");
        return;
    }

    UbDatalinkPacketHeader linkPacketHeader;
    UbCompactAckTransactionHeader caTaHeader;
    UbCna16NetworkHeader memHeader;
    
    packet->RemoveHeader(linkPacketHeader);
    packet->RemoveHeader(memHeader);

    Ptr<Packet> ackp;
    // 收到store数据包
    if (type == static_cast<uint8_t>(TaOpcode::TA_OPCODE_WRITE)) {
        ackp = Create<Packet>(0);
        caTaHeader.SetTaOpcode(TaOpcode::TA_OPCODE_TRANSACTION_ACK);
    } else if (type == static_cast<uint8_t>(TaOpcode::TA_OPCODE_READ)) {
        caTaHeader.SetTaOpcode(TaOpcode::TA_OPCODE_READ_RESPONSE);
        ackp = Create<Packet>(m_loadRspSize);
    }

    UbCompactTransactionHeader cTaHeader;
    packet->PeekHeader(cTaHeader);
    uint16_t taskId = cTaHeader.GetIniTaSsn();
    caTaHeader.SetIniTaSsn(taskId);
    // 根据taskid去查map，若没有记录，则是第一个要回的response
    if (m_taskReplyRsp.find(taskId) == m_taskReplyRsp.end()) {
        m_taskReplyRsp[taskId] = 1;
        PeerSendFirstPacketACKsNotify(m_nodeId, taskId, type);
    }

    uint16_t tmp = memHeader.GetScna();
    memHeader.SetScna(memHeader.GetDcna());
    memHeader.SetDcna(tmp);

    ackp->AddHeader(caTaHeader);
    ackp->AddHeader(memHeader);
    UbDataLink::GenPacketHeader(ackp, false, true, vlIndex, vl, 0, 1,
                                UbDatalinkHeaderConfig::PACKET_UB_MEM);
    
    Ipv4Address ip_node = utils::NodeIdToIp(utils::Cna16ToNodeId(memHeader.GetDcna()));

    auto node = NodeList::GetNode(m_nodeId);
    auto sw = node->GetObject<UbSwitch>();

    std::vector<uint16_t> vec = sw->GetRoutingProcess()->GetShortestOutPorts(ip_node.Get());
    uint16_t destPort = vec[0]; // 暂时使用第0个

    sw->AddPktToVoq(ackp, destPort, m_queuePriority, destPort);

    Ptr<UbPort> triggerPort = DynamicCast<UbPort>(node->GetDevice(destPort));
    triggerPort->TriggerTransmit(); // 触发发送

    NS_LOG_DEBUG("RecvDataPacket");
}

Ptr<UbApiLdstThread> UbApiLdst::GetUbLdstThreadByThreadId(uint32_t threadId)
{
    for (size_t i = 0; i < m_ldstThreadVector.size(); i++) {
        if (threadId == m_ldstThreadVector[i]->GetThreadNum()) {
            return m_ldstThreadVector[i];
        }
    }
    NS_LOG_WARN("Can't get UbApiLdstThread by threadId");
    return nullptr;
}

void UbApiLdst::RecvResponse(Ptr<Packet> p)
{
    NS_LOG_DEBUG("RecvResponse");
    if (p == nullptr) {
        NS_LOG_ERROR("in RecvResponse, p is null");
        return;
    }
    // Store/load response: DLH cNTH cATAH(0x11/0x12) Payload
    UbDatalinkPacketHeader linkPacketHeader;
    UbCna16NetworkHeader memHeader;
    UbCompactAckTransactionHeader caTaHeader;
    p->RemoveHeader(linkPacketHeader);
    p->RemoveHeader(memHeader);
    p->RemoveHeader(caTaHeader);

    uint32_t taskId = caTaHeader.GetIniTaSsn();
    GetUbLdstThreadByThreadId(m_taskThreadMap[taskId])->IncreaseOutstanding(m_taskTypeMap[taskId]);
    // 该taskid,收到的ack数目++
    m_taskAckcountMap[taskId]++;

    uint32_t psnCnt = 0;
    for (size_t i = 0; i < m_memTaskQueue.size(); i++) {
        if (m_memTaskQueue[i]->GetMemTaskId() == taskId) {
            psnCnt = m_memTaskQueue[i]->GetPsnSize();
            break;
        }
    }
    // 所有ack都收到，则内存语义任务完成
    if (psnCnt == m_taskAckcountMap[taskId]) {
        LastPacketACKsNotify(m_nodeId, taskId);
        MemTaskCompletesNotify(m_nodeId, taskId);
        NS_LOG_INFO("MEM Task Finishes, taskId: " << std::to_string(taskId));
        FinishCallback(taskId); // 调用应用层的回调
    } else {
        // 继续发包
        GetUbLdstThreadByThreadId(m_taskThreadMap[taskId])->GenPacketAndSend();
    }
}

void UbApiLdst::DoDispose()
{
    NS_LOG_DEBUG(this);
    for (auto i = m_memTaskQueue.begin(); i != m_memTaskQueue.end(); i++) {
        *i = nullptr;
    }
    m_memTaskQueue.clear();
    m_taskReplyRsp.clear();
    for (auto i = m_ldstThreadVector.begin(); i != m_ldstThreadVector.end(); i++) {
        *i = nullptr;
    }
    m_ldstThreadVector.clear();
    m_taskTypeMap.clear();
    m_taskThreadMap.clear();
    m_taskAckcountMap.clear();
}

void UbApiLdst::LastPacketACKsNotify(uint32_t nodeId, uint32_t taskId)
{
    m_traceLastPacketACKsNotify(nodeId, taskId);
}

void UbApiLdst::MemTaskCompletesNotify(uint32_t nodeId, uint32_t taskId)
{
    m_traceMemTaskCompletesNotify(nodeId, taskId);
}

void UbApiLdst::PeerSendFirstPacketACKsNotify(uint32_t nodeId, uint32_t taskId, uint32_t type)
{
    m_tracePeerSendFirstPacketACKsNotify(nodeId, taskId, type);
}

} // namespace ns3