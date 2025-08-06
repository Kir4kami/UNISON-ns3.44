#include "ns3/core-module.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/internet-module.h"
#include "ns3/global-route-manager.h"
#include "ns3/applications-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <map>

using namespace ns3;

// IP地址生成函数
Ipv4Address node_id_to_ip(uint32_t id) {
    return Ipv4Address(0x0b000001 + ((id / 256) * 0x00010000) + ((id % 256) * 0x00000100));
}

int main(int argc, char *argv[]) {
    // 命令行参数
    std::string topology_file = "scratch/topology.txt";
    CommandLine cmd;
    cmd.AddValue("topology", "拓扑文件路径", topology_file);
    cmd.Parse(argc, argv);

    // 打开拓扑文件
    std::ifstream topof(topology_file);
    if (!topof.is_open()) {
        std::cerr << "无法打开拓扑文件: " << topology_file << std::endl;
        return 1;
    }

    // 读取基本信息
    uint32_t node_num, switch_num, tors, link_num;
    uint64_t leaf_server_capacity, spine_leaf_capacity;
    topof >> node_num >> switch_num >> tors >> link_num >> leaf_server_capacity >> spine_leaf_capacity;

    std::cout << "节点数: " << node_num << ", 交换机数: " << switch_num 
              << ", 链路数: " << link_num << std::endl;

    // 读取交换机ID
    std::vector<uint32_t> switch_ids(switch_num);
    for (uint32_t i = 0; i < switch_num; i++) {
        topof >> switch_ids[i];
    }

    // 创建节点容器
    NodeContainer nodes;
    std::vector<Ptr<Node>> node_list(node_num);
    
    // 创建节点 (0: 服务器, 1: ToR交换机, 2: 脊交换机)
    std::vector<uint32_t> node_types(node_num, 0); // 默认为服务器
    
    // 标记交换机节点类型
    for (uint32_t i = 0; i < switch_num; i++) {
        uint32_t sid = switch_ids[i];
        if (i < tors) {
            node_types[sid] = 1; // ToR交换机
        } else {
            node_types[sid] = 2; // 脊交换机
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

    // 为服务器分配IP地址
    std::vector<Ipv4Address> server_addresses(node_num);
    for (uint32_t i = 0; i < node_num; i++) {
        if (node_types[i] == 0) { // 服务器
            server_addresses[i] = node_id_to_ip(i);
        }
    }

    // 创建网络连接
    PointToPointHelper p2p;
    Ipv4AddressHelper ipv4;
    
    // 存储网络接口信息
    std::map<std::pair<uint32_t, uint32_t>, NetDeviceContainer> links;
    
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
        
        std::cout << "连接 " << src << " <-> " << dst 
                  << " (带宽: " << data_rate << ", 延迟: " << link_delay << ")" << std::endl;
    }
    
    topof.close();

    // 分配IP地址
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    for (auto& link : links) {
        ipv4.Assign(link.second);
        ipv4.NewNetwork();
    }

    // 设置路由
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    std::cout << "拓扑构建完成!" << std::endl;
    std::cout << "总节点数: " << nodes.GetN() << std::endl;
    std::cout << "链路数: " << link_num << std::endl;

    // 运行仿真
    Simulator::Run();
    Simulator::Destroy();
    return 0;
}