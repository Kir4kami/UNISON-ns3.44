// SPDX-License-Identifier: GPL-2.0-only
#ifndef UB_PORT_H
#define UB_PORT_H

#include <map>
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>
#include "ns3/ipv4-header.h"
#include "ns3/ipv4.h"
#include "ns3/udp-header.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/ub-transport.h"
#include "ns3/ub-link.h"
#include "ns3/packet.h"
#include "ns3/boolean.h"
#include "ns3/uinteger.h"
#include "ns3/enum.h"
#include "ns3/ub-datalink.h"
#include "ns3/ub-controller.h"
#include "ns3/ub-switch.h"
#include "ns3/ub-flow-control.h"

#define TIME_TO_LIVE   64


namespace ns3 {
class UbTransportChannel;
class UbEgressQueue;
class UbSwitch;
class UbDataLink;
class UbIngressQueue;
class UbLink;
class UbFlowControl;
class UbCbfc;
class UbPfc;

/**
 * \class UbEgressQueue
 * \brief Port Egress Queue Management
 */
class UbEgressQueue : public Object {
public:
    static const uint32_t qCnt = 8;
    uint32_t m_rrLast; // 上次优先级轮询的最后一个TP
    uint32_t m_priLast; // 上次访问的最后一个优先级
    std::queue<Ptr<UbIngressQueue>> m_egressQ; // 通过算法分配到的包

    uint32_t m_numsInQueue;        // eq里面存储的包数量
    uint32_t m_maxIngressQueues;   // eq存储最大包数

    static TypeId GetTypeId(void);
    explicit UbEgressQueue();

    bool DoEnqueue(Ptr<UbIngressQueue> igq);  // 向端口eq塞入队列
    Ptr<UbIngressQueue> DoPeekqueue(void);    // 查看eq队列
    Ptr<UbIngressQueue> DoDequeue(void);      // 端口从eq出队列
    // 为报文添加UDP、IPV4、DL packet头
    void AddPacketHeader(Ptr<UbTransportChannel> tp, Ptr<Packet> p, bool credit, bool ack);

    bool IsEmpty();

    TracedCallback<Ptr<const Packet>, uint32_t> m_traceUbEnqueue;
    TracedCallback<Ptr<const Packet>, uint32_t> m_traceUbDequeue;
};

enum class SendState {
    READY,        // The transmitter is ready to begin transmission of a packet
    BUSY,         // The transmitter is busy transmitting a packet
    ALLOCATION    // The transmitter is waiting allocation
};

/**
 * \class UbPort
 * \brief A Device for Unified-bus
 */
class UbPort : public PointToPointNetDevice {
public:

    static TypeId GetTypeId(void);

    UbPort();

    virtual ~UbPort();

    void Receive(Ptr<Packet> p);

    bool Attach(Ptr<UbLink> ch);

    void NotifyLinkUp(void);

    void TriggerTransmit();
    void NotifyAllocationFinish();

    // 判断
    bool IsUb(void) const;

    bool IsReady();

    bool IsBusy();

    void SetCredits(int index, uint8_t value); // 设置用于恢复的信用证值
    void ResetCredits();
    uint8_t GetCredits(int index);
    void SetDataRate(DataRate bps);

    // 获取
    DataRate GetDataRate();

    Time GetInterframeGap();

    Ptr<Channel> GetChannel(void) const override;

    Ptr<UbEgressQueue> GetUbQueue();

    uint64_t GetTxBytes();

    void SetIfIndex(const uint32_t index) override;

    void SetSendState(SendState state);

    uint32_t GetIfIndex() const override;

    void IncreaseRcvQueueSize(Ptr<Packet> p, Ptr<UbPort> port);
    void DecreaseRcvQueueSize(Ptr<Packet> p, uint32_t portId);

    void CreateAndInitFc(const std::string& type);

    Ptr<UbFlowControl> GetFlowControl()
    {
        return m_flowControl;
    }

    static const uint32_t qCnt = 16;

    static std::unordered_map<UbNodeType_t, std::string> g_node_type_map;

    static uint32_t ParseHeader(Ptr<Packet> p, Header& h);

    static void AddUdpHeader(Ptr<Packet> p, Ptr<UbTransportChannel> tp);

    static void AddUdpHeader(Ptr<Packet> p, uint16_t sPort, uint16_t dPort);

    static void AddIpv4Header(Ptr<Packet> p, Ptr<UbTransportChannel> tp);

    static void AddIpv4Header(Ptr<Packet> p, Ipv4Address sIp, Ipv4Address dIp);

    static void AddNetHeader(Ptr<Packet> p);

    uint8_t m_credits[qCnt]; // 需要返回的信用证

    uint64_t GetRcvQueueSize(uint8_t vlId);
    void SetRcvQueueSize(Ptr<Packet> p);
    Ptr<UbFlowControl> m_flowControl; // 使用的流量算法

    uint32_t GetRcvVlQueueSize(uint8_t vlId);
    std::vector<uint32_t> GetRcvQueueSize();

    void SetFaultCallBack(Callback<int, Ptr<Packet>, uint32_t, uint32_t, Ptr<UbPort>> cb)
    {
        m_faultCallBack = cb;
    }
    
    void TransmitPacket(Ptr<Packet>, Time delay);
    
    /// Reset the channel into READY state and try transmit again
    void TransmitComplete();
private:

    TracedCallback<uint32_t, uint32_t, uint32_t> m_tracePortTxNotify;
    TracedCallback<uint32_t, uint32_t, uint32_t> m_tracePortRxNotify;

    void PortTxNotify(uint32_t nodeId, uint32_t mPortId, uint32_t size);
    void PortRxNotify(uint32_t nodeId, uint32_t mPortId, uint32_t size);

    bool TransmitStart(Ptr<Packet> p);

    /// Look for an available packet and send it using TransmitStart(p)
    void DequeuePacket(void);

    void PktRcvNotify(Ptr<Packet> p);

    void TraComEventNotify(Ptr<Packet> p, Time t);

    void UpdateTxBytes(uint64_t bytes);

    void DoDispose() override;

    uint32_t m_portId;

    Ptr<UbEgressQueue> m_ubEQ;

    Ptr<UbLink> m_channel;

    uint64_t m_txBytes;

    DataRate m_bps;

    SendState m_ubSendState;

    bool m_linkUp;

    Time m_tInterframeGap;

    Ptr<Packet> m_currentPkt;
    Ptr<UbIngressQueue> m_currentIgQ;

    Ptr<UbDataLink> m_datalink;

    TracedCallback<Ptr<Packet>> m_tracePktRcvNotify;
    Callback<int, Ptr<Packet>, uint32_t, uint32_t, Ptr<UbPort>> m_faultCallBack;

    TracedCallback<Ptr<Packet>, Time> m_traceTraComEventNotify;
    std::vector<uint32_t> m_revQueueSize;  // 接收缓存，用于pfc

    // cbfc
    uint8_t m_cbfcFlitLen;                      // flit长度，默认 20 Bytes
    uint8_t m_cbfcFlitsPerCell;                 // 每个Cell包含的flit数量 取值范围：{1, 2, 4, 8, 16, 32}
    uint8_t m_cbfcRetCellGrainDataPacket;       // 数据包返回的CRD的粒度，通常从以下选项中选择 {1, 2, 4, 8, 16, 32, 64, 128}
    uint8_t m_cbfcRetCellGrainControlPacket;    // 控制报文返回的CRD的粒度，通常从以下选项中选择 {1, 2, 4, 8, 16, 32, 64, 128}
    int32_t m_cbfcPortTxfree;

    // pfc
    int32_t m_pfcUpThld;
    int32_t m_pfcLowThld;

    bool m_pktTraceEnabled = false;
};


} // namespace ns3

#endif // UB_PORT_H
