// SPDX-License-Identifier: GPL-2.0-only
#include "ns3/log.h"
#include "ns3/simulator.h"

#include "ns3/ub-datatype.h"
#include "ns3/ub-controller.h"
#include "ub-transaction.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("UbFunction");

NS_OBJECT_ENSURE_REGISTERED(UbFunction);

TypeId UbFunction::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::UbFunction")
        .SetParent<Object>()
        .SetGroupName("UnifiedBus");
    return tid;
}

UbFunction::UbFunction()
{
    NS_LOG_DEBUG("UbFunction created");
    m_apiLdst = CreateObject<UbApiLdst>();
    m_random = CreateObject<UniformRandomVariable>();
    m_random->SetAttribute("Min", DoubleValue(0.0));
    m_random->SetAttribute("Max", DoubleValue(1.0));
}

UbFunction::~UbFunction()
{
    m_numToJetty.clear();
    m_jettyTpGroup.clear();
}

Ptr<UbApiLdst> UbFunction::GetUbLdst()
{
    return m_apiLdst;
}

void UbFunction::SetUbFunction(uint32_t nodeId)
{
    m_nodeId = nodeId;
    m_apiLdst->SetUbLdst(m_nodeId);
}

void UbFunction::CreateJetty(uint32_t src, uint32_t dest, uint32_t jettyNum)
{
    NS_LOG_DEBUG(this << src << dest << jettyNum);
    // 创建新的Jetty
    Ptr<UbJetty> jetty = CreateObject<UbJetty>();
    jetty->Init();
    jetty->SetJettyNum(jettyNum);
    jetty->SetSrc(src);
    jetty->SetDest(dest);
    m_numToJetty[jettyNum] = jetty;
    NS_LOG_DEBUG("Created jetty success");
}

bool UbFunction::IsJettyExists(uint32_t jettyNum)
{
    if (m_numToJetty.find(jettyNum) != m_numToJetty.end()) {
        return true;
    }
    return false;
}

Ptr<UbJetty> UbFunction::GetJetty(uint32_t jettyNum)
{
    auto it = m_numToJetty.find(jettyNum);
    if (it != m_numToJetty.end()) {
        return it->second;
    }

    NS_LOG_DEBUG("Jetty not found");
    return nullptr;
}

std::vector<Ptr<UbTransportChannel>> UbFunction::GetTransportChannelVec(uint32_t jettyNum)
{
    NS_LOG_DEBUG(this);
    auto it = m_jettyTpGroup.find(jettyNum);
    if (it != m_jettyTpGroup.end()) {
        return it->second;
    }

    NS_LOG_DEBUG("UbTransportChannel vector not found");
    return {};
}

void UbFunction::DestroyJetty(uint32_t jettyNum)
{
    NS_LOG_DEBUG(this);
    auto it = m_numToJetty.find(jettyNum);
    if (it != m_numToJetty.end()) {
        m_numToJetty.erase(it);
        NS_LOG_DEBUG("Destroyed ");
    } else {
        NS_LOG_WARN("Jetty  not found for destruction");
    }

    auto itJettyTp = m_jettyTpGroup.find(jettyNum);
    if (itJettyTp != m_jettyTpGroup.end()) {
        // 解除关系
        std::vector<Ptr<UbTransportChannel>> vec = itJettyTp->second;
        for (size_t i = 0; i < vec.size(); i++) {
            vec[i]->DeleteTpJettyRelationship(jettyNum);
        }
        m_jettyTpGroup.erase(itJettyTp);
        NS_LOG_DEBUG("Destroyed ");
    } else {
        NS_LOG_WARN("Jetty  not found for destruction");
    }

    // 删除m_jettyVector中元素
    for (size_t i = 0; i < m_jettyVector.size(); i++) {
        if (m_jettyVector[i]->GetJettyNum() == jettyNum) {
            m_jettyVector.erase(m_jettyVector.begin() + i);
            break;
        }
    }
}

bool UbFunction::jettyBindTp(uint32_t src, uint32_t dest, uint32_t jettyNum,
                             bool multiPath, std::vector<uint32_t> tpns)
{
    NS_LOG_DEBUG(this);
    Ptr<UbJetty> ubJetty  = GetJetty(jettyNum);
    if (ubJetty == nullptr) {
        return false;
    }

    std::vector<Ptr<UbTransportChannel>> ubTransportGroup;
    for (uint32_t i = 0; i < tpns.size(); i++) {
        uint32_t tpn = tpns[i];
        auto node = NodeList::GetNode(m_nodeId);
        ubTransportGroup.push_back(node->GetObject<UbController>()->GetTp(tpn));
    }
    // 在事务层模式为ROL时只能开启单路径模式
    if (ubJetty->GetTransaction()->GetTransactionServiceMode() == TransactionServiceMode::ROL) {
        NS_LOG_WARN("ROL, set to single path forced.");
        multiPath = false;
    }
    if (multiPath) {
        NS_LOG_DEBUG("Multiple tp");
        for (uint32_t i = 0; i < ubTransportGroup.size(); i++) {
            ubTransportGroup[i]->CreateTpJettyRelationship(ubJetty);
        }
    } else {
        NS_LOG_DEBUG("Single tp");
        double res = m_random->GetValue();
        int pos;
        if (res < 1.0) {
            pos = (int)(res * ubTransportGroup.size());
        } else {
            pos = ubTransportGroup.size() - 1;
        }

        ubTransportGroup[pos]->CreateTpJettyRelationship(ubJetty);
    }

    m_jettyTpGroup[jettyNum] = ubTransportGroup;
    return true;
}

Ptr<UbWqe> UbFunction::CreateWqe(uint32_t src, uint32_t dest, uint32_t size, uint32_t wqeId)
{
    NS_LOG_DEBUG(this);
    // 创建新的 WQE
    Ptr<UbWqe> ubWqe = CreateObject<UbWqe>();
    ubWqe->SetSrc(src);
    ubWqe->SetDest(dest);
    ubWqe->SetSize(size);
    ubWqe->SetWqeId(wqeId);
    return ubWqe;
}

void UbFunction::PushWqeToJetty(Ptr<UbWqe> wqe, uint32_t jettyNum)
{
    NS_LOG_DEBUG(this);
    Ptr<UbJetty> ubJetty = GetJetty(jettyNum);
    if (ubJetty == nullptr) {
        NS_LOG_WARN("Get jetty failed");
        return;
    }
    ubJetty->SetNodeId(m_nodeId);
    // 将 WQE 添加到 Jetty
    ubJetty->PushWqe(wqe);
    std::vector<Ptr<UbTransportChannel>> ubTransportGrpVec = GetTransportChannelVec(jettyNum);
    // 发包
    if (ubTransportGrpVec.size() == 0) {
        NS_LOG_WARN("No tp");
    } else {
        if (ubJetty->IsLimited()) {
            NS_LOG_WARN("Inflight reach limit");
        } else {
            NS_LOG_DEBUG("TA New Wqe Trigger transmit");
            for (uint32_t i = 0; i < ubTransportGrpVec.size(); i++) {
                ubTransportGrpVec[i]->TriggerTransmit();
            }
        }
    }
}

void UbFunction::PushLdstTask(uint32_t src, uint32_t dest, uint32_t size, uint32_t taskId,
                              UbMemOperationType type, uint32_t threadId)
{
    NS_LOG_DEBUG(this);
    if (m_apiLdst != nullptr) {
        m_apiLdst->PushMemTask(src, dest, size, taskId, type, threadId);
    } else {
        NS_LOG_WARN("Get ldst failed");
    }
}

void UbFunction::DoDispose()
{
    NS_LOG_FUNCTION(this);
    m_apiLdst = nullptr;
    for (auto i = m_jettyVector.begin(); i != m_jettyVector.end(); i++) {
        *i = nullptr;
    }
    m_jettyVector.clear();
    m_numToJetty.clear();
    for (auto &it : m_jettyTpGroup) {
        for (auto tp : it.second) {
            tp = nullptr;
        }
    }
    m_jettyTpGroup.clear();
    m_random = nullptr;
    Object::DoDispose();
}

NS_OBJECT_ENSURE_REGISTERED(UbJetty);

TypeId UbJetty::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::UbJetty")
                        .SetParent<Object>()
                        .SetGroupName("UnifiedBus")
                        .AddAttribute("JettyOooAckThreshold",
                                      "Jetty Out-of-Order Ack Threshold",
                                      UintegerValue(2048),
                                      MakeUintegerAccessor(&UbJetty::m_oooAckThreshold),
                                      MakeUintegerChecker<uint32_t>())
                        .AddAttribute("UbInflightMax",
                                      "jetty inflight max",
                                      UintegerValue(10000),
                                      MakeUintegerAccessor(&UbJetty::m_inflightMax),
                                      MakeUintegerChecker<uint32_t>());
    return tid;
}

UbJetty::UbJetty()
{
    m_transaction = CreateObject<UbTransaction>();
}

void UbJetty::Init()
{
    ResetSsnAckBitset(m_oooAckThreshold);
}

void UbJetty::SetClientCallback(Callback<void, uint32_t, uint32_t> cb)
{
    FinishCallback = cb;
}


// ============================================================================
// UbJetty 实现
// ============================================================================
Ptr<UbWqeSegment> UbJetty::GetNextWqeSegment()
{
    NS_LOG_FUNCTION(this);

    if (IsLimited()) {
        NS_LOG_WARN("Inflight reach limit");
        return nullptr;
    }

    if (m_wqeVector.empty()) {
        NS_LOG_DEBUG("No WQE available to send");
        return nullptr;
    }

    // 获取第一个未完成的 WQE
    Ptr<UbWqe> currentWqe = nullptr;
    for (auto it = m_wqeVector.begin(); it != m_wqeVector.end(); ++it) {
        if (*it && !(*it)->IsSentCompleted()) {
            currentWqe = *it;
            if (currentWqe->GetSendStatus()) {
                break;
            } else {
                // 调用TA，判断currentWqe是否允许发送
                if (m_transaction->IsOrderedByInitiator(currentWqe)) {
                    currentWqe->UpdateSendStatus(true);
                    break;
                }
            }
        }
    }

    if (!currentWqe) {
        NS_LOG_DEBUG("No unfinished WQE available to send");
        return nullptr;
    }

    if (!currentWqe->GetSendStatus()) {
        NS_LOG_DEBUG("No unfinished WQE available to send");
        return nullptr;
    }

    uint64_t segmentSize = currentWqe->GetBytesLeft();
    if (segmentSize > UB_WQE_TA_SEGMENT_BYTE) {
        segmentSize = UB_WQE_TA_SEGMENT_BYTE;
    }

    Ptr<UbWqeSegment> segment = GenWqeSegment(currentWqe, segmentSize);
    segment->SetTaskId(currentWqe->GetWqeId());
    segment->SetWqeSize(currentWqe->GetSize());
    currentWqe->UpdateSentBytes(segmentSize);
    IncreaseTaSsnSndNxt(); // 更新下一个待发送的分段序号

    return segment;
}

Ptr<UbWqeSegment> UbJetty::GenWqeSegment(Ptr<UbWqe> wqe, uint32_t segmentSize)
{
    NS_LOG_FUNCTION(this << segmentSize);

    if (!wqe) {
        NS_LOG_ERROR("WQE is null");
        return nullptr;
    }

    if (segmentSize == 0) {
        NS_LOG_ERROR("Segment size is zero");
        return nullptr;
    }

    // 创建新的 WQE 分段
    Ptr<UbWqeSegment> segment = CreateObject<UbWqeSegment>();

    // 设置任务描述信息（从WQE复制）
    segment->SetSrc(wqe->GetSrc());
    segment->SetDest(wqe->GetDest());
    segment->SetSport(wqe->GetSport());
    segment->SetDport(wqe->GetDport());
    segment->SetType(wqe->GetType());
    segment->SetSize(segmentSize);
    segment->SetPriority(wqe->GetPriority());
    // 设置TA层信息
    segment->SetOrderType(wqe->GetOrderType());
    // 设置网络层信息（从WQE复制）
    segment->SetSip(wqe->GetSip());
    segment->SetDip(wqe->GetDip());

    // 设置TA层信息
    segment->SetJettyNum(wqe->GetJettyNum());
    segment->SetTaMsn(wqe->GetTaMsn());
    segment->SetTaSsn(m_taSsnSndNxt); // 使用当前的分段序号

    // TP layer information will be set during subsequent TP layer scheduling

    NS_LOG_DEBUG("Generated WQE segment: MSN=" << std::to_string(wqe->GetTaMsn())
                                              << ", SSN=" << std::to_string(m_taSsnSndNxt)
                                              << ", size=" << std::to_string(segmentSize)
                                              << ", src=" << std::to_string(wqe->GetSrc())
                                              << ", dest=" << std::to_string(wqe->GetDest()));

    return segment;
}

void UbJetty::PushWqe(Ptr<UbWqe> ubWqe)
{
    ubWqe->SetJettyNum(m_jettyNum);
    ubWqe->SetTaMsn(m_taMsnCnt);
    ubWqe->SetTaSsnStart(m_taSsnCnt);
    uint64_t ssnSize = (ubWqe->GetSize() + UB_WQE_TA_SEGMENT_BYTE - 1) / UB_WQE_TA_SEGMENT_BYTE;
    ubWqe->SetTaSsnSize(ssnSize);
    m_transaction->AddWqe(ubWqe);
    // 更新 Jetty 的 MSN 和 SSN 计数器
    m_taMsnCnt++;
    m_taSsnCnt += ssnSize;

    // 加入队列
    m_wqeVector.push_back(ubWqe);
    NS_LOG_INFO("WQE Starts, jettyNum:{" << std::to_string(m_jettyNum)
                <<  "} taskId:{" << std::to_string(ubWqe->GetWqeId()) << "}");
}

bool UbJetty::IsValid()
{
    return true;
}

bool UbJetty::IsReadyToSend()
{
    return true;
}

/*
 * TP调用这个方法，传入taSsnFin。
 */
bool UbJetty::ProcessWqeSegmentComplete(uint32_t taSsnAck)
{
    NS_LOG_DEBUG(this << taSsnAck);
    // 检查 SSN 是否在有效范围内
    if (taSsnAck < m_taSsnSndUna) {
        NS_LOG_WARN("Received ACK for already processed SSN " << std::to_string(taSsnAck)
                                                              << ", current m_taSsnSndUna is "
                                                              << std::to_string(m_taSsnSndUna));
        return false;
    }

    if (taSsnAck >= m_taSsnSndNxt) {
        NS_LOG_WARN("Received ACK for future SSN " << std::to_string(taSsnAck)
                                                   << ", current m_taSsnSndNxt is "
                                                   << std::to_string(m_taSsnSndNxt));
        return false;
    }

    if (IsLimited()) {
        Ptr<UbFunction> function = NodeList::GetNode(m_nodeId)->GetObject<UbController>()->GetUbFunction();
        std::vector<Ptr<UbTransportChannel>> ubTransportGrpVec = function->GetTransportChannelVec(m_jettyNum);
        for (uint32_t i = 0; i < ubTransportGrpVec.size(); i++) {
            ubTransportGrpVec[i]->TriggerTransmit();
        }
    }

    // 计算在 bitset 中的相对位置
    uint32_t bitIndex = taSsnAck - m_taSsnSndUna;

    // 检查是否超出 bitset 范围
    if (bitIndex >= m_ssnAckBitset.size()) {
        NS_LOG_ERROR("SSN " << std::to_string(taSsnAck)
                            << " exceeds bitset capacity, bitIndex=" << std::to_string(bitIndex));
        return false;
    }

    // 设置对应的 bit
    m_ssnAckBitset[bitIndex] = true;

    NS_LOG_DEBUG("Set ACK bit for SSN " << std::to_string(taSsnAck) << " at bit index "
                                       << std::to_string(bitIndex));

    // 从最小未确认的 SSN 开始，连续更新已确认的分段
    uint32_t oldSndUna = m_taSsnSndUna;
    while (m_taSsnSndUna < m_taSsnSndNxt) {
        uint32_t currentBitIndex = m_taSsnSndUna - oldSndUna;
        if (currentBitIndex < m_ssnAckBitset.size() && m_ssnAckBitset[currentBitIndex]) {
            m_taSsnSndUna++;
        } else {
            break; // 遇到未确认的分段，停止
        }
    }

    // 如果 m_taSsnSndUna 有更新，需要清理 bitset
    if (m_taSsnSndUna > oldSndUna) {
        NS_LOG_DEBUG("Updated m_taSsnSndUna from " << std::to_string(oldSndUna) << " to "
                                                  << std::to_string(m_taSsnSndUna));

        // 手动右移 bitset
        uint32_t shiftCount = m_taSsnSndUna - oldSndUna;
        RightShiftBitset(shiftCount);

        // 检查并移除已完成的 WQE
        CheckAndRemoveCompletedWqe();
    }
    return true;
}

void UbJetty::RightShiftBitset(uint32_t shiftCount)
{
    if (shiftCount >= m_ssnAckBitset.size()) {
        std::fill(m_ssnAckBitset.begin(), m_ssnAckBitset.end(), false); // 清空所有位
        return;
    }

    // 手动实现右移
    for (size_t i = 0; i + shiftCount < m_ssnAckBitset.size(); ++i) {
        m_ssnAckBitset[i] = m_ssnAckBitset[i + shiftCount];
    }

    // 清空右移后的高位
    for (size_t i = m_ssnAckBitset.size() - shiftCount; i < m_ssnAckBitset.size(); ++i) {
        m_ssnAckBitset[i] = 0;
    }
}

void UbJetty::CheckAndRemoveCompletedWqe()
{
    NS_LOG_DEBUG(this);
    // 检查并移除已完成的WQE
    for (auto it = m_wqeVector.begin(); it != m_wqeVector.end();) {
        Ptr<UbWqe> wqe = *it;
        uint32_t wqeId = wqe->GetWqeId();
        if (wqe && IsWqeCompleted(wqe)) {
            NS_LOG_INFO("WQE Finishes, jettyNum: {" << m_jettyNum  << "} taskId:{ " << std::to_string(wqeId) <<"}");
            // 从vector中移除已完成的WQE
            it = m_wqeVector.erase(it);
            FinishCallback(wqeId, m_jettyNum); // 调用应用层的回调
            m_transaction->WqeFinish(wqe);
            // trigger tp
            Ptr<UbFunction> function = NodeList::GetNode(m_nodeId)->GetObject<UbController>()->GetUbFunction();
            std::vector<Ptr<UbTransportChannel>> ubTransportGrpVec = function->GetTransportChannelVec(m_jettyNum);
            for (uint32_t i = 0; i < ubTransportGrpVec.size(); i++) {
                ubTransportGrpVec[i]->TriggerTransmit();
            }
        } else {
            ++it;
        }
    }

    // 检查Jetty是否还有未完成的工作
    if (m_wqeVector.empty()) {
        NS_LOG_DEBUG("All WQEs in Jetty " << std::to_string(m_jettyNum) << " are completed");
    }
}

bool UbJetty::IsWqeCompleted(Ptr<UbWqe> wqe)
{
    NS_LOG_DEBUG(this);
    if (!wqe) {
        return false;
    }

    // 检查WQE的所有分段是否都已确认
    uint32_t wqeStartSsn = wqe->GetTaSsnStart();
    uint32_t wqeSsnSize = wqe->GetTaSsnSize();
    uint32_t wqeEndSsn = wqeStartSsn + wqeSsnSize - 1;

    // 如果WQE的最后一个分段已经被确认，则WQE完成
    return (wqeEndSsn < m_taSsnSndUna);
}

void UbJetty::DoDispose()
{
    NS_LOG_FUNCTION(this);
    m_wqeVector.clear();
    m_ssnAckBitset.clear();
    m_transaction = nullptr;
    Object::DoDispose();
}


} // namespace ns3