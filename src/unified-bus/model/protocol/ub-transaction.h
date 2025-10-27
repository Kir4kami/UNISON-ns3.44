// SPDX-License-Identifier: GPL-2.0-only
#ifndef UB_TRANSACTION_H
#define UB_TRANSACTION_H

#include <ns3/node.h>
#include <vector>
#include <unordered_map>
#include "ns3/ub-datatype.h"
#include "ns3/ub-network-address.h"

namespace ns3 {
    /**
     * @brief Transaction Service Mode enumeration
     */
    enum class TransactionServiceMode : uint8_t {
        ROI = 0,    // Reliable Ordered by Initiator
        ROT = 1,    // Reliable Ordered by Target
        ROL = 2,    // Reliable Ordered by Lower Layer
        UNO = 3     // Unreliable No Order
    };

    class UbController;
    /**
    * @brief
    */
    class UbTransaction : public Object {
    public:

        static TypeId GetTypeId(void);

        UbTransaction();
        UbTransaction(Ptr<Node> node);
        virtual ~UbTransaction();
        // 判断wqe是否能发送
        bool IsOrderedByInitiator(Ptr<UbWqe> wqe);
        bool IsOrderedByTarget(Ptr<UbWqe> wqe);
        bool IsReliable(Ptr<UbWqe> wqe);
        bool IsUnreliable(Ptr<UbWqe> wqe);

        void SetTransactionServiceMode(TransactionServiceMode mode) { m_serviceMode = mode; }
        TransactionServiceMode GetTransactionServiceMode() { return m_serviceMode; }

        // 新增wqe任务
        void AddWqe(Ptr<UbWqe> wqe);

        // 某个wqe完成，刷新状态
        void WqeFinish(Ptr<UbWqe> wqe);

    private:

        void DoDispose() override;
        
        uint32_t m_nodeId;
        TransactionServiceMode m_serviceMode = TransactionServiceMode::ROI;
        // 记录wqe的Order序列
        std::vector<uint32_t> m_wqeVector;
        // 1. 只有m_serviceMode == TransactionServiceMode::ROI，才启用IsOrderedByInitiator()
        // IsOrderedByInitiator() 需要unordered_map jettyNum->vector/queue<顺序存储WQE的ORDER信息> 每个jetty未确认的WQE的ORDER信息存储下来
        // RO *SO -> 不能发
        // *SO -> 可以发
        // 如果是NO No Order，则无需存储。
        // 如果是RO，往里存但是总是可以继续发（返回True）；
        // 如果是SO，则在前面的RO被ACK删除这些ORDER记录之后，才能继续发，如果前面还有RO记号，则返回false不让发。
        // 此时这个jetty可以使用多路径/单路径，可以使用packet spray

        // 2. 只有m_serviceMode == TransactionServiceMode::ROT，才启用IsOrderedByTarget()
        // ROT (not implemented yet)
        // 此时这个jetty可以使用多路径/单路径，可以使用packet spray

        // 3. ROL时，需要保序的事务指定相同的TP channel。
        // jetty应该使用单路径，可以使用packet spray usePacketSpray 可以= 1

        // 4. UNO (not implemented yet)
        // 如果jetty使用单路径，则直接不需要操作。

        // 5. IsReliable和IsUnreliable目前的行为都是通知性质的，在每一个WQE发送完成后，都打印一个trace
        // trace的内容里包含WQE ID完成发送，加上m_serviceMode
    };

} // namespace ns3

#endif /* UB_TRANSACTION_H */
