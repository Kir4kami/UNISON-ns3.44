// SPDX-License-Identifier: GPL-2.0-only
#ifndef UB_APP_H
#define UB_APP_H

#include <vector>
#include <set>
#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/ptr.h"
#include "ns3/ipv4-address.h"
#include "ns3/ub-datatype.h"
#include "ns3/ub-controller.h"
#include "ns3/ub-api-ldst.h"
#include "ub-tp-connection-manager.h"
#include "ub-network-address.h"

using namespace utils;
namespace ns3 {
/**
 * @brief 任务图应用,管理多个wqe任务及依赖关系
 */
class UbApp : public Application {
public:
    static TypeId GetTypeId(void);

    UbApp();
    virtual ~UbApp();

    void SendTraffic(TrafficRecord record);
    void SendTrafficForTest(TrafficRecord record);

    void SetNode(Ptr<Node> node); // 设置当前节点

    void GetTpnConn(TpConnectionManager tpConnection); // 获取tpnconnection

    void SetGetTpnRule(GetTpnRuleT type)
    {
        m_getTpnRule = type;
    }

    void SetUseShortestPath(bool useShortestPath)
    {
        m_useShortestPath = useShortestPath;
    }

    /**
     * @brief 任务完成回调
     */
    void OnTaskCompleted(uint32_t taskId, uint32_t jettyNum);
    void OnTestTaskCompleted(uint32_t taskId, uint32_t jettyNum);
    void OnMemTaskCompleted(uint32_t taskId);

    // ========== 回调函数 ==========
    void SetFinishCallback(Callback<void, uint32_t, uint32_t> cb, Ptr<UbJetty> jetty);
    void SetFinishCallback(Callback<void, uint32_t> cb, Ptr<UbApiLdst> UbApiLdst);

protected:
    void DoDispose(void) override;

private:
    TracedCallback<uint32_t, uint32_t> m_traceMemTaskStartsNotify;
    TracedCallback<uint32_t, uint32_t> m_traceMemTaskCompletesNotify;
    TracedCallback<uint32_t, uint32_t, uint32_t> m_traceWqeTaskStartsNotify;
    TracedCallback<uint32_t, uint32_t, uint32_t> m_traceWqeTaskCompletesNotify;

    void MemTaskStartsNotify(uint32_t nodeId, uint32_t taskId);
    void MemTaskCompletesNotify(uint32_t nodeId, uint32_t taskId);
    void WqeTaskStartsNotify(uint32_t nodeId, uint32_t jettyNum, uint32_t taskId);
    void WqeTaskCompletesNotify(uint32_t nodeId, uint32_t jettyNum, uint32_t taskId);

    map<std::string, TaOpcode> TaOpcodeMap = {
        {"URMA_WRITE", TaOpcode::TA_OPCODE_WRITE},
        {"MEM_STORE", TaOpcode::TA_OPCODE_WRITE},
        {"MEM_LOAD", TaOpcode::TA_OPCODE_READ}
    };

    // 控制器
    bool m_multiPathEnable;

    GetTpnRuleT m_getTpnRule = GetTpnRuleT::BY_PEERNODE_PRIORITY; // 获取Tpn的方式
    bool m_useShortestPath = true; // 是否根据跳数寻找最短路径

    Ptr<Node> m_node;              // 当前节点
    TpConnectionManager m_tpnConn; // 当前节点维护的tpnConn
    uint32_t m_jettyNum = 0;       // 当前节点维护的jettynum,不会重复
    uint32_t threadId = 0;         // 当前节点维护的threadId,不会重复
};

} // namespace ns3

#endif // UB_APP_H
