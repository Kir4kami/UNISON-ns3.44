// SPDX-License-Identifier: GPL-2.0-only
#include "ns3/log.h"
#include "ns3/simulator.h"

#include "ns3/ub-datatype.h"
#include "ns3/ub-controller.h"
#include "ns3/ub-transaction.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("UbTransaction");

NS_OBJECT_ENSURE_REGISTERED(UbTransaction);

TypeId UbTransaction::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::UbTransaction")
        .SetParent<Object>()
        .SetGroupName("UnifiedBus");
    return tid;
}

UbTransaction::UbTransaction()
{
    NS_LOG_DEBUG("UbTransaction created");
}

UbTransaction::~UbTransaction()
{
}

UbTransaction::UbTransaction(Ptr<Node> node)
{
    m_nodeId = node->GetId();
}

// 用于判断某个wqe是否order

bool UbTransaction::IsOrderedByInitiator(Ptr<UbWqe> wqe)
{
    // ROI
    bool res;
    if (m_serviceMode == TransactionServiceMode::ROI) {
        // 判断NO/RO/SO关系
        switch (wqe->GetOrderType()) {
            case OrderType::ORDER_NO:
            case OrderType::ORDER_RELAX:
                res = true;
                break;
            case OrderType::ORDER_STRONG:
                res = (m_wqeVector[0] == wqe->GetWqeId());
                break;
            case OrderType::ORDER_RESERVED:
                res = true;
            default:
                res = true;
        }
    } else { // 不是ROI，直接返回true
        res = true;
    }
    return res;
}

bool UbTransaction::IsOrderedByTarget(Ptr<UbWqe> wqe)
{
    NS_LOG_DEBUG("IsOrderedByTarget");
    return true;
}

bool UbTransaction::IsReliable(Ptr<UbWqe> wqe)
{
    return true;
}

bool UbTransaction::IsUnreliable(Ptr<UbWqe> wqe)
{
    return false;
}

void UbTransaction::AddWqe(Ptr<UbWqe> wqe)
{
    if (m_serviceMode == TransactionServiceMode::ROI
        && (wqe->GetOrderType() == OrderType::ORDER_RELAX || wqe->GetOrderType() == OrderType::ORDER_STRONG)) {
        m_wqeVector.push_back(wqe->GetWqeId());
    }
}

void UbTransaction::WqeFinish(Ptr<UbWqe> wqe)
{
    if (m_serviceMode == TransactionServiceMode::ROI) {
        // 从vector中寻找该wqe并删除
        auto it = std::find(m_wqeVector.begin(), m_wqeVector.end(), wqe->GetWqeId());
        if (it != m_wqeVector.end()) {
            m_wqeVector.erase(it);
        }
    }
}

void UbTransaction::DoDispose()
{
    NS_LOG_FUNCTION(this);
    m_wqeVector.clear();
    Object::DoDispose();
}

} // namespace ns3