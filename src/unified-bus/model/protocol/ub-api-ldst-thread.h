// SPDX-License-Identifier: GPL-2.0-only
#ifndef UB_APILDSTTHREAD_H
#define UB_APILDSTTHREAD_H

#include <ns3/node.h>
#include "ns3/ub-datatype.h"
#include "ns3/ub-network-address.h"
#include "ns3/node-list.h"
namespace ns3 {
    constexpr int MAX_LB = 255;
    constexpr int MIN_LB = 0;
    class UbController;
    /**
    * @brief 发送内存语义包
    */
    class UbApiLdstThread : public Object {
    public:
        static TypeId GetTypeId(void);
        UbApiLdstThread();
        virtual ~UbApiLdstThread();
        void SetUbLdstThread(uint32_t nodeId, uint32_t ldstThreadNum, uint32_t storeReqSize);
        void PushMemTask(Ptr<UbMemTask> memTask);
        Ptr<Packet> GenDataPacket(Ptr<UbMemTask> memTask, uint32_t payloadSize, uint16_t destPort);
        void GenPacketAndSend();
        void IncreaseOutstanding(UbMemOperationType type);
        uint32_t GetThreadNum();
        void SetUsePacketSpray(bool usePacketSpray);
        void SetUseShortestPaths(bool useShortestPaths);

    private:
        void DoDispose() override;
        TracedCallback<uint32_t, uint32_t> m_traceMemTaskStartsNotify;
        TracedCallback<uint32_t, uint32_t> m_traceFirstPacketSendsNotify;
        TracedCallback<uint32_t, uint32_t> m_traceLastPacketSendsNotify;

        void MemTaskStartsNotify(uint32_t nodeId, uint32_t memTaskId);
        void FirstPacketSendsNotify(uint32_t nodeId, uint32_t memTaskId);
        void LastPacketSendsNotify(uint32_t nodeId, uint32_t memTaskId);

        uint32_t m_nodeId;
        uint32_t m_storeOutstanding; // 发数据包++, 收ack --
        uint32_t m_loadOutstanding; // 发数据包++, 收ack --
        uint32_t m_storeReqSize;
        uint32_t m_loadReqSize;
        uint32_t m_queuePriority;
        bool m_usePacketSpray;
        bool m_useShortestPaths;
        uint8_t m_lbHashSalt = 0; // 从0-255不断循环
        
        uint32_t m_threadNum;
        std::map<uint32_t, uint32_t> m_taskidSendCnt; // key是taskid， value是该task已发包数目
        std::deque<Ptr<UbMemTask>> m_memStoreTaskQueue;
        std::deque<Ptr<UbMemTask>> m_memLoadTaskQueue;
    };
} // namespace ns3

#endif /* UB_APILDSTTHREAD_H */
