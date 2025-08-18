#include "ns3/core-module.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/internet-module.h"
#include "ns3/global-route-manager.h"
#include "ns3/applications-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/ipv4-routing-table-entry.h"
#include "ns3/packet.h"
#include "ns3/ipv4-header.h"
#include "ns3/udp-header.h"
#include "ns3/trace-helper.h"
#include "ns3/config.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <map>
#include <ctime>
#include <iomanip>
#include <sstream>

using namespace ns3;

// 全局日志文件流
std::ofstream g_logFile;

// 获取当前时间戳字符串
std::string getCurrentTimestamp() {
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

// 日志输出函数
void logMessage(const std::string& message) {
    std::string timestamp = getCurrentTimestamp();
    g_logFile << "[" << timestamp << "] " << message << std::endl;
    g_logFile.flush(); // 立即刷新到文件
}

// IP地址生成函数（修复版本）
// 注意：这个函数现在需要访问实际的server_addresses，所以需要重构
Ipv4Address node_id_to_ip(uint32_t id) {
    // 这个函数已废弃，应该使用实际的server_addresses
    // 临时返回错误的地址以便调试
    return Ipv4Address(0x0b000001 + ((id / 256) * 0x00010000) + ((id % 256) * 0x00000100));
}

// 在main函数中添加路由表读取函数
void ReadAndApplyRoutingTable(const std::vector<Ptr<Node>>& node_list,
                              Ipv4StaticRoutingHelper& staticRoutingHelper,
                              const std::map<std::pair<uint32_t, uint32_t>, Ipv4InterfaceContainer>& interface_map,
                              const std::vector<Ipv4Address>& server_addresses) {
    
    std::string routing_file = "scratch/dragonfly_routes.txt";
    std::ifstream routef(routing_file);
    
    if (!routef.is_open()) {
        std::cerr << "无法打开路由表文件: " << routing_file << std::endl;
        return;
    }
    
    std::string line;
    while (std::getline(routef, line)) {
        // 跳过注释和空行
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream iss(line);
        std::string src_node_str, route_type;
        iss >> src_node_str >> route_type;
        
        uint32_t src_node = std::stoi(src_node_str);
        
        if (src_node >= node_list.size()) {
            std::cerr << "无效的源节点ID: " << src_node << std::endl;
            continue;
        }
        
        Ptr<Ipv4StaticRouting> staticRouting = staticRoutingHelper.GetStaticRouting(
            node_list[src_node]->GetObject<Ipv4>());
        
        if (route_type == "default") {
            // 默认路由: src_node default next_hop interface_index metric
            std::string next_hop_str;
            uint32_t interface_index, metric;
            iss >> next_hop_str >> interface_index >> metric;
            
            uint32_t next_hop_node = std::stoi(next_hop_str);
            
            // 查找连接这两个节点的接口IP
            Ipv4Address next_hop_ip;
            bool found = false;
            for (auto& link : interface_map) {
                uint32_t src = link.first.first;
                uint32_t dst = link.first.second;
                
                if ((src == src_node && dst == next_hop_node) || 
                    (src == next_hop_node && dst == src_node)) {
                    if (src == src_node) {
                        next_hop_ip = link.second.GetAddress(1);
                    } else {
                        next_hop_ip = link.second.GetAddress(0);
                    }
                    found = true;
                    break;
                }
            }
            
            if (found) {
                // 使用网络路由替代默认路由，目标为0.0.0.0/0
                staticRouting->AddNetworkRouteTo(Ipv4Address("0.0.0.0"), 
                                                Ipv4Mask("0.0.0.0"), 
                                                next_hop_ip, 
                                                interface_index, 
                                                metric);
                std::ostringstream oss;
                oss << "添加默认路由: 节点 " << src_node << " -> " << next_hop_ip;
                logMessage(oss.str());
            } else {
                std::ostringstream oss;
                oss << "警告: 节点 " << src_node << " 到节点 " << next_hop_node << " 没有直接连接，跳过默认路由";
                logMessage(oss.str());
            }
        }
        else if (route_type == "host") {
            // 主机路由: src_node host dst_host next_hop interface_index metric
            std::string dst_host_str, next_hop_str;
            uint32_t interface_index, metric;
            iss >> dst_host_str >> next_hop_str >> interface_index >> metric;
            
            uint32_t dst_host = std::stoi(dst_host_str);
            // 使用实际分配的IP地址而不是计算的地址
            if (dst_host >= server_addresses.size() || server_addresses[dst_host].IsAny()) {
                std::ostringstream oss;
                oss << "警告: 服务器 " << dst_host << " 的IP地址未分配，跳过主机路由";
                logMessage(oss.str());
                continue;
            }
            Ipv4Address dst_ip = server_addresses[dst_host];
            
            Ipv4Address next_hop_ip;
            if (next_hop_str == "0.0.0.0") {
                // 直连主机，使用特殊地址
                next_hop_ip = Ipv4Address::GetZero();
            } else {
                uint32_t next_hop_node = std::stoi(next_hop_str);
                // 查找下一跳IP
                bool found = false;
                for (auto& link : interface_map) {
                    uint32_t src = link.first.first;
                    uint32_t dst = link.first.second;
                    
                    if ((src == src_node && dst == next_hop_node) || 
                        (src == next_hop_node && dst == src_node)) {
                        if (src == src_node) {
                            next_hop_ip = link.second.GetAddress(1);
                        } else {
                            next_hop_ip = link.second.GetAddress(0);
                        }
                        found = true;
                        break;
                    }
                }
                if (!found) continue;
            }
            
            staticRouting->AddHostRouteTo(dst_ip, next_hop_ip, interface_index, metric);
            std::ostringstream oss;
            oss << "添加主机路由: 节点 " << src_node << " -> " << dst_ip;
            logMessage(oss.str());
        }
        else if (route_type == "network") {
            // 网络路由: src_node network dst_network netmask next_hop interface_index metric
            std::string dst_network_str, netmask_str, next_hop_str;
            uint32_t interface_index, metric;
            iss >> dst_network_str >> netmask_str >> next_hop_str >> interface_index >> metric;
            
            Ipv4Address dst_network(dst_network_str.c_str());
            Ipv4Mask netmask(netmask_str.c_str());
            
            uint32_t next_hop_node = std::stoi(next_hop_str);
            
            // 查找下一跳IP
            Ipv4Address next_hop_ip;
            bool found = false;
            for (auto& link : interface_map) {
                uint32_t src = link.first.first;
                uint32_t dst = link.first.second;
                
                if ((src == src_node && dst == next_hop_node) || 
                    (src == next_hop_node && dst == src_node)) {
                    if (src == src_node) {
                        next_hop_ip = link.second.GetAddress(1);
                    } else {
                        next_hop_ip = link.second.GetAddress(0);
                    }
                    found = true;
                    break;
                }
            }
            
            if (found) {
                staticRouting->AddNetworkRouteTo(dst_network, netmask, next_hop_ip, interface_index, metric);
                std::ostringstream oss;
                oss << "添加网络路由: 节点 " << src_node << " -> " << dst_network << "/" << netmask << " via " << next_hop_ip;
                logMessage(oss.str());
            }
        }
    }
    routef.close();
}
// 数据包跟踪回调函数
void PacketTrace(std::string context, Ptr<const Packet> packet) {
    // 获取节点ID从context中
    std::size_t nodePos = context.find("/NodeList/");
    std::size_t devicePos = context.find("/DeviceList/");
    
    if (nodePos != std::string::npos && devicePos != std::string::npos) {
        std::string nodeStr = context.substr(nodePos + 10);
        std::size_t endPos = nodeStr.find("/");
        if (endPos != std::string::npos) {
            nodeStr = nodeStr.substr(0, endPos);
        }
        
        std::string deviceStr = context.substr(devicePos + 12);
        endPos = deviceStr.find("/");
        if (endPos != std::string::npos) {
            deviceStr = deviceStr.substr(0, endPos);
        }
        
        // 解析数据包头部信息
        Ptr<Packet> copy = packet->Copy();
        Ipv4Header ipv4Header;
        UdpHeader udpHeader;
        
        if (copy->PeekHeader(ipv4Header)) {
            std::ostringstream oss;
            oss << "📦 数据包跟踪 - 节点" << nodeStr << "/接口" << deviceStr 
                << ": " << ipv4Header.GetSource() << " -> " << ipv4Header.GetDestination()
                << " (大小: " << packet->GetSize() << " 字节, TTL: " << (int)ipv4Header.GetTtl() << ")";
            logMessage(oss.str());
            
            // 同时输出到控制台以便实时观察
            std::cout << "[实时] " << oss.str() << std::endl;
        }
    }
}

// IP转发跟踪回调函数
void IpForwardTrace(std::string context, const Ipv4Header &header, Ptr<const Packet> packet, uint32_t interface) {
    // 提取节点ID
    std::string nodeId = "未知";
    size_t nodePos = context.find("/NodeList/");
    if (nodePos != std::string::npos) {
        size_t start = nodePos + 10; // "/NodeList/"的长度
        size_t end = context.find("/", start);
        if (end != std::string::npos) {
            nodeId = context.substr(start, end - start);
        }
    }
    
    std::ostringstream oss;
    oss << "🔀 节点" << nodeId << " IP转发: " << header.GetSource() 
        << " -> " << header.GetDestination() << " 通过接口" << interface 
        << " (大小: " << packet->GetSize() << " 字节, TTL: " << (int)header.GetTtl() << ")";
    logMessage(oss.str());
    
    // 同时输出到控制台以便实时观察
    std::cout << "[实时] " << oss.str() << std::endl;
}

// IP丢包跟踪回调函数
void IpDropTrace(std::string context, const Ipv4Header &header, Ptr<const Packet> packet, 
                 Ipv4L3Protocol::DropReason reason, Ptr<Ipv4> ipv4, uint32_t interface) {
    std::string dropReason;
    switch(reason) {
        case Ipv4L3Protocol::DROP_TTL_EXPIRED: dropReason = "TTL过期"; break;
        case Ipv4L3Protocol::DROP_NO_ROUTE: dropReason = "无路由"; break;
        case Ipv4L3Protocol::DROP_BAD_CHECKSUM: dropReason = "校验和错误"; break;
        case Ipv4L3Protocol::DROP_INTERFACE_DOWN: dropReason = "接口关闭"; break;
        case Ipv4L3Protocol::DROP_ROUTE_ERROR: dropReason = "路由错误"; break;
        default: dropReason = "未知原因"; break;
    }
    
    // 提取节点ID
    std::string nodeId = "未知";
    size_t nodePos = context.find("/NodeList/");
    if (nodePos != std::string::npos) {
        size_t start = nodePos + 10; // "/NodeList/"的长度
        size_t end = context.find("/", start);
        if (end != std::string::npos) {
            nodeId = context.substr(start, end - start);
        }
    }
    
    std::ostringstream oss;
    oss << "❌ 节点" << nodeId << " 数据包丢弃: " << header.GetSource() 
        << " -> " << header.GetDestination() << " 原因: " << dropReason 
        << " (接口: " << interface << ", 大小: " << packet->GetSize() << " 字节)";
    logMessage(oss.str());
    
    // 同时输出到控制台以便实时观察
    std::cout << "[实时] " << oss.str() << std::endl;
}

void ReceivedPacket(Ptr<const Packet> packet, const Address &address) {
    // 解析发送方地址
    InetSocketAddress inetAddr = InetSocketAddress::ConvertFrom(address);
    Ipv4Address senderIP = inetAddr.GetIpv4();
    
    // 写入日志而不是控制台
    std::ostringstream oss;
    oss << "✅ 接收到数据包，大小: " << packet->GetSize() << " 字节，来自IP: " << senderIP;
    logMessage(oss.str());
    
    // 同时输出到控制台以便实时观察
    std::cout << "[实时] " << oss.str() << std::endl;
}

// 通用测试函数（基础版本）
void RunTest(uint32_t source_node, uint32_t dest_node, 
             const std::vector<Ptr<Node>>& node_list,
             const std::vector<Ipv4Address>& server_addresses,
             uint16_t sink_port, const std::string& test_name,
             uint32_t max_packets = 2, double interval = 2.0, 
             uint32_t packet_size = 512, double start_time = 3.0, 
             double stop_time = 10.0) {
    
    // 检查节点范围
    if (source_node >= node_list.size() || dest_node >= node_list.size()) {
        std::cerr << "错误: 节点序号超出范围" << std::endl;
        return;
    }
    
    // 检查IP地址是否已分配
    if (server_addresses[source_node].IsAny() || server_addresses[dest_node].IsAny()) {
        std::cerr << "警告: 源节点" << source_node << "或目标节点" << dest_node 
                  << "的IP地址未正确分配，跳过测试" << std::endl;
        return;
    }
    
    Ipv4Address dest_address = server_addresses[dest_node];
    
    // 记录测试信息
    logMessage("=== 🧪 " + test_name + " ===");
    std::ostringstream oss1, oss2;
    oss1 << "📤 源服务器 " << source_node << " IP: " << server_addresses[source_node];
    oss2 << "📥 目标服务器 " << dest_node << " IP: " << dest_address;
    logMessage(oss1.str());
    logMessage(oss2.str());
    
    std::cout << "=== " << test_name << " ===" << std::endl;
    std::cout << "源服务器 " << source_node << " IP: " << server_addresses[source_node] << std::endl;
    std::cout << "目标服务器 " << dest_node << " IP: " << dest_address << std::endl;
    
    // 创建UDP客户端
    UdpEchoClientHelper client(dest_address, sink_port);
    client.SetAttribute("MaxPackets", UintegerValue(max_packets));
    client.SetAttribute("Interval", TimeValue(Seconds(interval)));
    client.SetAttribute("PacketSize", UintegerValue(packet_size));
    
    ApplicationContainer client_apps = client.Install(node_list[source_node]);
    client_apps.Start(Seconds(start_time));
    client_apps.Stop(Seconds(stop_time));
    
    // 记录配置信息
    std::ostringstream config_info;
    config_info << "⚙️ 配置" << test_name << ": " << max_packets << "个数据包，每" 
                << interval << "秒发送一次，大小" << packet_size << "字节";
    logMessage(config_info.str());
    std::cout << "配置UDP数据传输: 服务器" << source_node << " -> 服务器" << dest_node << std::endl;
}

// 高级测试函数（包含预期路径信息）
void RunTestWithPath(uint32_t source_node, uint32_t dest_node, 
                     const std::vector<Ptr<Node>>& node_list,
                     const std::vector<Ipv4Address>& server_addresses,
                     uint16_t sink_port, const std::string& test_name,
                     const std::string& expected_path,
                     uint32_t max_packets = 2, double interval = 2.0, 
                     uint32_t packet_size = 512, double start_time = 3.0, 
                     double stop_time = 10.0) {
    
    // 检查节点范围
    if (source_node >= node_list.size() || dest_node >= node_list.size()) {
        std::cerr << "错误: 节点序号超出范围" << std::endl;
        return;
    }
    
    // 检查IP地址是否已分配
    if (server_addresses[source_node].IsAny() || server_addresses[dest_node].IsAny()) {
        std::cerr << "警告: 源节点" << source_node << "或目标节点" << dest_node 
                  << "的IP地址未正确分配，跳过测试" << std::endl;
        return;
    }
    
    Ipv4Address dest_address = server_addresses[dest_node];
    
    // 记录测试信息
    logMessage("=== 🧪 " + test_name + " ===");
    std::ostringstream oss1, oss2;
    oss1 << "📤 源服务器 " << source_node << " IP: " << server_addresses[source_node];
    oss2 << "📥 目标服务器 " << dest_node << " IP: " << dest_address;
    logMessage(oss1.str());
    logMessage(oss2.str());
    logMessage("📊 预期路径: " + expected_path);
    
    std::cout << "=== " << test_name << " ===" << std::endl;
    std::cout << "源服务器 " << source_node << " IP: " << server_addresses[source_node] << std::endl;
    std::cout << "目标服务器 " << dest_node << " IP: " << dest_address << std::endl;
    std::cout << "预期路径: " << expected_path << std::endl;
    
    // 创建UDP客户端
    UdpEchoClientHelper client(dest_address, sink_port);
    client.SetAttribute("MaxPackets", UintegerValue(max_packets));
    client.SetAttribute("Interval", TimeValue(Seconds(interval)));
    client.SetAttribute("PacketSize", UintegerValue(packet_size));
    
    ApplicationContainer client_apps = client.Install(node_list[source_node]);
    client_apps.Start(Seconds(start_time));
    client_apps.Stop(Seconds(stop_time));
    
    // 记录配置信息
    std::ostringstream config_info;
    config_info << "⚙️ 配置" << test_name << ": " << max_packets << "个数据包，每" 
                << interval << "秒发送一次，大小" << packet_size << "字节";
    logMessage(config_info.str());
    std::cout << "配置UDP数据传输: 服务器" << source_node << " -> 服务器" << dest_node << std::endl;
}

// 测试结果结构体
struct TestResult {
    uint32_t source_node;
    uint32_t dest_node;
    std::string test_name;
    uint32_t expected_bytes;
    uint64_t actual_bytes;
    bool success;
};

// 通用测试结果分析函数
TestResult AnalyzeTestResult(uint32_t dest_node, const std::vector<Ptr<Node>>& node_list,
                           const std::string& test_name, uint32_t source_node,
                           uint32_t expected_bytes) {
    TestResult result;
    result.source_node = source_node;
    result.dest_node = dest_node;
    result.test_name = test_name;
    result.expected_bytes = expected_bytes;
    
    if (dest_node >= node_list.size()) {
        result.actual_bytes = 0;
        result.success = false;
        return result;
    }
    
    Ptr<Application> app = node_list[dest_node]->GetApplication(0);
    Ptr<PacketSink> sink = DynamicCast<PacketSink>(app);
    result.actual_bytes = sink->GetTotalRx();
    result.success = (result.actual_bytes >= expected_bytes);
    
    return result;
}

// 打印测试结果
void PrintTestResult(const TestResult& result) {
    if (result.success) {
        std::string successMsg = "✅ " + result.test_name + "成功: 服务器" + 
                               std::to_string(result.source_node) + " -> 服务器" + 
                               std::to_string(result.dest_node);
        logMessage(successMsg);
        std::cout << successMsg << std::endl;
    } else {
        std::string failMsg = "❌ " + result.test_name + "失败: 服务器" + 
                            std::to_string(result.source_node) + " -> 服务器" + 
                            std::to_string(result.dest_node) + "，实际接收: " + 
                            std::to_string(result.actual_bytes) + " 字节，期望: " + 
                            std::to_string(result.expected_bytes) + " 字节";
        logMessage(failMsg);
        std::cout << "❌ " + result.test_name + "失败: 服务器" + 
                     std::to_string(result.source_node) + " -> 服务器" + 
                     std::to_string(result.dest_node) << std::endl;
        std::cout << "   实际接收: " << result.actual_bytes 
                  << " 字节，期望: " << result.expected_bytes << " 字节" << std::endl;
    }
}

int main(int argc, char *argv[]) {
    // 初始化日志文件
    std::string logFileName = "scratch/LOG_any_topo.log";
    g_logFile.open(logFileName, std::ios::out | std::ios::app);
    if (!g_logFile.is_open()) {
        std::cerr << "无法创建日志文件: " << logFileName << std::endl;
        return 1;
    }
    
    logMessage("\n\n=== 🚀🚀🚀🚀网络仿真测试开始🚀🚀🚀🚀 ===\n\n");
    std::cout << "日志文件已创建: " << logFileName << std::endl;
    
    // 命令行参数
    std::string topology_file = "scratch/dragonfly_topology.txt";
    CommandLine cmd;
    cmd.AddValue("topology", "拓扑文件路径", topology_file);
    cmd.Parse(argc, argv);

    // 打开拓扑文件
    std::ifstream topof(topology_file);
    if (!topof.is_open()) {
        std::cerr << "无法打开拓扑文件: " << topology_file << std::endl;
        return 1;
    }

    // 读取基本信息（Dragonfly格式：节点数 交换机数 链路数）
    uint32_t node_num, switch_num, link_num;
    topof >> node_num >> switch_num >> link_num;
    
    std::ostringstream oss;
    oss << "拓扑参数: 节点数=" << node_num << ", 交换机数=" << switch_num << ", 链路数=" << link_num;
    logMessage(oss.str());
    
    // Dragonfly拓扑不需要跳过第二行，直接读取链路信息
    
    // 计算服务器数量
    uint32_t server_num = node_num - switch_num;

    {
        std::ostringstream oss;
        oss << "节点数: " << node_num << ", 服务器数: " << server_num << ", 交换机数: " << switch_num << ", 链路数: " << link_num;
        logMessage(oss.str());
    }

    // 创建节点容器
    NodeContainer nodes;
    std::vector<Ptr<Node>> node_list(node_num);
    
    // 创建节点 (0: 服务器, 1: 交换机)
    std::vector<uint32_t> node_types(node_num, 0); // 默认为服务器
    
    // 标记交换机节点（Dragonfly中交换机是48-63）
    for (uint32_t i = server_num; i < node_num; i++) {
        node_types[i] = 1; // 标记为交换机
        {
            std::ostringstream oss;
            oss << "标记节点 " << i << " 为交换机";
            logMessage(oss.str());
        }
    }

    // 创建NS-3节点
    for (uint32_t i = 0; i < node_num; i++) {
        node_list[i] = CreateObject<Node>();
        nodes.Add(node_list[i]);
    }

    // 安装互联网协议栈
    InternetStackHelper internet;
    internet.Install(nodes);
    
    // 为所有交换机节点启用IP转发功能和路由功能
    for (uint32_t i = server_num; i < node_num; i++) {
        Ptr<Ipv4> ipv4 = node_list[i]->GetObject<Ipv4>();
        // 启用IP转发功能（允许数据包在接口间转发）
        ipv4->SetAttribute("IpForward", BooleanValue(true));
        // 启用强端系统模型（推荐的新属性）
        ipv4->SetAttribute("StrongEndSystemModel", BooleanValue(false));
        {
            std::ostringstream oss;
            oss << "启用节点 " << i << " 的IP转发和路由功能";
            logMessage(oss.str());
        }
    }

    // 创建网络连接
    PointToPointHelper p2p;
    Ipv4AddressHelper ipv4;
    
    // 存储网络接口信息
    std::map<std::pair<uint32_t, uint32_t>, NetDeviceContainer> links;
    std::map<std::pair<uint32_t, uint32_t>, Ipv4InterfaceContainer> interface_map;
    
    // 读取并创建链路
    for (uint32_t i = 0; i < link_num; i++) {
        uint32_t src, dst;
        std::string data_rate, link_delay;
        double error_rate;
        
        topof >> src >> dst >> data_rate >> link_delay >> error_rate;
        
        Ptr<Node> src_node = node_list[src];
        Ptr<Node> dst_node = node_list[dst];
        
        // 设置链路属性
        p2p.SetDeviceAttribute("DataRate", StringValue(data_rate));
        p2p.SetChannelAttribute("Delay", StringValue(link_delay));
        
        // 创建连接
        NetDeviceContainer devices = p2p.Install(src_node, dst_node);
        links[std::make_pair(src, dst)] = devices;
        
        {
            std::ostringstream oss;
            oss << "连接 " << src << " <-> " << dst << " (带宽: " << data_rate << ", 延迟: " << link_delay << ")";
            logMessage(oss.str());
        }
    }
    
    topof.close();

    // 分配IP地址并保存接口信息 - 恢复点对点链路方式
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    std::vector<Ipv4Address> server_addresses(node_num);
    
    for (auto& link : links) {
        Ipv4InterfaceContainer interfaces = ipv4.Assign(link.second);
        interface_map[link.first] = interfaces;
        
        // 记录服务器的IP地址
        uint32_t src = link.first.first;
        uint32_t dst = link.first.second;
        
        if (node_types[src] == 0) { // 如果src是服务器
            server_addresses[src] = interfaces.GetAddress(0);
            std::ostringstream oss;
            oss << "服务器 " << src << " 的IP地址: " << server_addresses[src];
            logMessage(oss.str());
        }
        if (node_types[dst] == 0) { // 如果dst是服务器
            server_addresses[dst] = interfaces.GetAddress(1);
            std::ostringstream oss;
            oss << "服务器 " << dst << " 的IP地址: " << server_addresses[dst];
            logMessage(oss.str());
        }
        
        ipv4.NewNetwork();
    }
    // 先应用静态路由，然后用全局路由作为补充
    // 这样可以确保跨网段通信正常工作

    // 🔄 启用静态路由策略 - 根据dragonfly_routes.txt文件进行路由
    // 禁用全局路由，完全依赖静态路由表配置
    logMessage("🔄 启用静态路由策略（基于dragonfly_routes.txt）");
    logMessage("❌ 禁用全局路由，使用手动配置的静态路由表");
    
    // 创建静态路由助手并应用路由表
    Ipv4StaticRoutingHelper staticRoutingHelper;
    ReadAndApplyRoutingTable(node_list, staticRoutingHelper, interface_map, server_addresses);
    
    // ❌ 完全禁用全局路由，仅使用静态路由
    // Ipv4GlobalRoutingHelper::PopulateRoutingTables(); // 已禁用
    logMessage("✅ 静态路由配置完成，已禁用全局路由算法");
    
    // 🔍 启用简化的数据包跟踪功能
    logMessage("🔍 启用简化的数据包跟踪功能");
    
    // 启用关键节点的数据包跟踪
    std::vector<uint32_t> trace_nodes = {0, 1, 8, 13, 48, 50, 51}; // 测试相关的关键节点，添加13和51
    for (uint32_t node_id : trace_nodes) {
        if (node_id < node_num) {
            // 启用点对点设备的发送和接收跟踪
            for (uint32_t dev = 0; dev < node_list[node_id]->GetNDevices(); dev++) {
                Ptr<NetDevice> device = node_list[node_id]->GetDevice(dev);
                Ptr<PointToPointNetDevice> p2pDevice = DynamicCast<PointToPointNetDevice>(device);
                if (p2pDevice) {
                    std::ostringstream path_tx, path_rx;
                    path_tx << "/NodeList/" << node_id << "/DeviceList/" << dev << "/$ns3::PointToPointNetDevice/TxQueue/Enqueue";
                    path_rx << "/NodeList/" << node_id << "/DeviceList/" << dev << "/$ns3::PointToPointNetDevice/MacRx";
                    
                    // 连接跟踪回调
                    Config::Connect(path_tx.str(), MakeCallback(&PacketTrace));
                    Config::Connect(path_rx.str(), MakeCallback(&PacketTrace));
                    
                    std::ostringstream oss;
                    oss << "启用节点" << node_id << "设备" << dev << "的数据包跟踪 (TX/RX)";
                    logMessage(oss.str());
                }
            }
            
            // 启用IP层转发和丢包跟踪
            std::ostringstream path_forward, path_drop;
            path_forward << "/NodeList/" << node_id << "/$ns3::Ipv4L3Protocol/UnicastForward";
            path_drop << "/NodeList/" << node_id << "/$ns3::Ipv4L3Protocol/Drop";
            
            Config::Connect(path_forward.str(), MakeCallback(&IpForwardTrace));
            Config::Connect(path_drop.str(), MakeCallback(&IpDropTrace));
            
            std::ostringstream oss2;
            oss2 << "启用节点" << node_id << "的IP转发和丢包跟踪";
            logMessage(oss2.str());
        }
    }
    
    logMessage("✅ 简化数据包跟踪功能已启用");
    
    // 打印路由表到日志文件 (使用立即打印而不是调度)
    logMessage("=== 打印路由表 ===");
    
    // 立即打印路由表到日志
    for (uint32_t node_id : {0, 1, 8, 13, 48, 49, 50, 51, 53}) {
        std::ostringstream route_output;
        Ptr<OutputStreamWrapper> log_stream = Create<OutputStreamWrapper>(&route_output);
        
        std::string node_type = (node_id < 48) ? "服务器" : "交换机";
        route_output << "\n--- " << node_type << node_id << "路由表 ---\n";
        
        // 获取路由表并立即打印
        Ptr<Ipv4> ipv4 = node_list[node_id]->GetObject<Ipv4>();
        Ptr<Ipv4RoutingProtocol> routing = ipv4->GetRoutingProtocol();
        routing->PrintRoutingTable(log_stream);
        
        logMessage(route_output.str());
    }
    
    logMessage("=================");

    // 添加UDP应用: 服务器之间互相发送数据包
    uint16_t sink_port = 9;  // Discard端口
    ApplicationContainer sink_apps;
    std::vector<uint32_t> server_nodes; // 收集所有服务器节点
    
    // 收集服务器节点
    for (uint32_t i = 0; i < server_num; i++) {
        server_nodes.push_back(i);
    }
    
    {
        std::ostringstream oss;
        oss << "找到 " << server_nodes.size() << " 个服务器节点";
        logMessage(oss.str());
    }
    
    // 在所有服务器节点上安装PacketSink应用
    for (uint32_t server_id : server_nodes) {
        PacketSinkHelper sink_helper("ns3::UdpSocketFactory", 
                                    InetSocketAddress(Ipv4Address::GetAny(), sink_port));
        ApplicationContainer sink_app = sink_helper.Install(node_list[server_id]);
        sink_app.Start(Seconds(0.0));
        sink_app.Stop(Seconds(55.0)); // 延长到55秒以接收第三次测试的数据包
        sink_apps.Add(sink_app);
        
        // 获取PacketSink指针并添加接收回调
        Ptr<PacketSink> packetSink = DynamicCast<PacketSink>(sink_app.Get(0));
        packetSink->TraceConnectWithoutContext("Rx", MakeCallback(&ReceivedPacket));
        
        {
            std::ostringstream oss;
            oss << "在服务器 " << server_id << " 上安装PacketSink应用";
            logMessage(oss.str());
        }
    }
    
    // 使用高级测试函数进行三个测试，包含预期路径信息
    // 测试1:  - 服务器0和服务器1 (都连接到交换机48)
    RunTestWithPath(0, 1, node_list, server_addresses, sink_port, 
                   "测试1: 服务器0 -> 服务器1", 
                   "服务器0和服务器1同时直连交换机48",
                   2, 2.0, 512, 3.0, 10.0);
    
    // 测试2:  - 服务器1向服务器8发送数据包 (交换机48→交换机50)
    RunTestWithPath(1, 8, node_list, server_addresses, sink_port, 
                   "测试2: 服务器1 -> 服务器8", 
                   "服务器1与交换机48直连，服务器8与交换机50直连，交换机48和交换机50直连",
                   3, 3.0, 768, 15.0, 30.0);
    
    // 测试3:  - 服务器13向服务器1发送数据包 (交换机51→交换机48)
    RunTestWithPath(13, 1, node_list, server_addresses, sink_port, 
                   "测试3: 服务器13 -> 服务器1", 
                   "服务器13与交换机51直连，服务器1与交换机48直连，交换机51与交换机48不直连，有多跳路由",
                   4, 2.5, 1024, 35.0, 50.0);
    //测试4： 服务器16向服务器42发送数据包
    RunTestWithPath(16, 42, node_list, server_addresses, sink_port, 
                   "测试4: 服务器16 -> 服务器42", 
                   "服务器16与交换机52直连，服务器42与交换机58直连，交换机52和交换机58不直连，有多跳路由",
                   2, 2.0, 512, 3.0, 10.0);           

    logMessage("拓扑构建完成!");
    {
        std::ostringstream oss;
        oss << "总节点数: " << nodes.GetN() << ", 链路数: " << link_num;
        logMessage(oss.str());
    }

    // 运行仿真
    Simulator::Stop(Seconds(55.0)); // 设置仿真停止时间，确保第三次测试完成
    Simulator::Run();
    
    // 仿真结束后打印统计信息
    logMessage("=== 仿真结束后的统计信息 ===");
    std::cout << "=== 仿真结束后的统计信息 ===" << std::endl;
    
    // 重点关注测试节点的统计信息
    std::vector<uint32_t> test_nodes = {1, 8, 13, 16, 42}; // 测试中涉及的关键节点
    
    for (uint32_t i = 0; i < server_num; i++) {
        Ptr<Application> app = node_list[i]->GetApplication(0);
        Ptr<PacketSink> sink = DynamicCast<PacketSink>(app);
        if (sink->GetTotalRx()) {// 如果接收到数据
            std::string logMsg = "服务器 " + std::to_string(i) + " 接收到 " + std::to_string(sink->GetTotalRx()) + " 字节";
            logMessage(logMsg);
            std::cout << "服务器 " << i <<  " 接收到 " << sink->GetTotalRx() << " 字节" << std::endl;
        }
    }
    
    logMessage("测试结果分析:");
    // 终端显示测试结果
    std::cout << std::endl;
    std::cout << "测试结果分析:" << std::endl;
    
    // 使用通用测试结果分析函数
    // 测试1:  (服务器0 -> 服务器1)
    TestResult test1 = AnalyzeTestResult(1, node_list, "测试1", 0, 1024); // 2个包 × 512字节 = 1024字节
    PrintTestResult(test1);
    
    // 测试2:  (服务器1 -> 服务器8)
    TestResult test2 = AnalyzeTestResult(8, node_list, "测试2", 1, 2304); // 3个包 × 768字节 = 2304字节
    PrintTestResult(test2);
    
    // 测试3:  (服务器13 -> 服务器1)
    // 注意：服务器1会同时接收来自测试1和测试3的数据包
    // 测试1: 2个包 × 512字节 = 1024字节
    // 测试3: 4个包 × 1024字节 = 4096字节
    // 总计期望: 1024 + 4096 = 5120字节
    TestResult test3 = AnalyzeTestResult(1, node_list, "测试3", 13, 5120); // 测试1 + 测试3的总和
    if (test3.success) {
        logMessage("✅ 测试3成功: 服务器13 -> 服务器1");
        std::cout << "✅ 测试3成功: 服务器13 -> 服务器1" << std::endl;
        std::cout << "   服务器1总接收: " << test3.actual_bytes << " 字节 (包含测试1和测试3)" << std::endl;
    } else {
        std::string failMsg = "❌ 测试3可能失败: 服务器13 -> 服务器1，服务器1总接收: " + 
                            std::to_string(test3.actual_bytes) + " 字节，期望至少: " + 
                            std::to_string(test3.expected_bytes) + " 字节";
        logMessage(failMsg);
        std::cout << "❌测试3可能失败: 服务器13 -> 服务器1" << std::endl;
        std::cout << "   服务器1总接收: " << test3.actual_bytes << " 字节，期望至少: " 
                  << test3.expected_bytes << " 字节 (测试1+测试3)" << std::endl;
    }
    
    // 测试4:  (服务器16 -> 服务器42)
    TestResult test4 = AnalyzeTestResult(42, node_list, "测试4", 16, 1024); // 2个包 × 512字节 = 1024字节
    PrintTestResult(test4);
    
    logMessage("\n\n=== 🚀🚀🚀🚀网络仿真测试结束🚀🚀🚀🚀 ===\n\n");
    std::cout << "========================" << std::endl;
    
    // 关闭日志文件
    g_logFile.close();
    std::cout << "测试日志已保存到: " << logFileName << std::endl;
    
    Simulator::Destroy();
    return 0;
}