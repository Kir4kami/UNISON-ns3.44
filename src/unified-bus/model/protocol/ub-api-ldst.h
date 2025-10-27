// SPDX-License-Identifier: GPL-2.0-only
#ifndef UB_APILDST_H
#define UB_APILDST_H

#include <ns3/node.h>
#include <ns3/ub-api-ldst-thread.h>
#include "ns3/ub-datatype.h"
#include "ns3/ub-network-address.h"
#include "ns3/node-list.h"
namespace ns3 {
    class UbController;
    /**
    * @brief 接收内存语义包
    */
    class UbApiLdst : public Object {
    public:
        static TypeId GetTypeId(void);

        UbApiLdst();
        virtual ~UbApiLdst();

        void SetUbLdst(uint32_t nodeId);
        void PushMemTask(uint32_t src, uint32_t dest, uint32_t size, uint32_t taskId,
                         UbMemOperationType type, uint32_t threadId);
        void RecvResponse(Ptr<Packet> p);
        void SetClientCallback(Callback<void, uint32_t> cb);
        Callback<void, uint32_t> FinishCallback;
        void RecvDataPacket(Ptr<Packet> p, uint8_t vlIndex, uint8_t vl, uint8_t type);
        Ptr<UbApiLdstThread> GetUbLdstThreadByThreadId(uint32_t threadId);
        uint32_t GetThreadNum();
        std::vector<Ptr<UbApiLdstThread>> GetLdstThreads();

    private:
        void DoDispose() override;
        TracedCallback<uint32_t, uint32_t> m_traceLastPacketACKsNotify;
        TracedCallback<uint32_t, uint32_t> m_traceMemTaskCompletesNotify;
        TracedCallback<uint32_t, uint32_t, uint32_t> m_tracePeerSendFirstPacketACKsNotify;

        void LastPacketACKsNotify(uint32_t nodeId, uint32_t taskId);
        void MemTaskCompletesNotify(uint32_t nodeId, uint32_t taskId);
        void PeerSendFirstPacketACKsNotify(uint32_t nodeId, uint32_t taskId, uint32_t type);

        uint32_t m_nodeId;
        std::deque<Ptr<UbMemTask>> m_memTaskQueue;
        uint32_t m_threadNum;
        uint32_t m_storeReqSize;
        uint32_t m_loadRspSize;
        uint32_t m_queuePriority;

        std::map<uint32_t, uint32_t> m_taskReplyRsp; // taskid和返回ack的个数
        std::vector<Ptr<UbApiLdstThread>> m_ldstThreadVector;
        std::map<uint32_t, UbMemOperationType> m_taskTypeMap; // taskid和任务类型对应关系
        std::map<uint32_t, uint32_t> m_taskThreadMap; // taskid和threadid对应关系
        std::map<uint32_t, uint32_t> m_taskAckcountMap; // taskid和收到的ack数目
    };
} // namespace ns3

#endif /* UB_APILDST_H */
