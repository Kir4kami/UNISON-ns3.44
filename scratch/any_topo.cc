#include "ns3/core-module.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/internet-module.h"
#include "ns3/global-route-manager.h"
#include "ns3/applications-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/ipv4-routing-table-entry.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <map>

using namespace ns3;

// IP地址生成函数
Ipv4Address node_id_to_ip(uint32_t id) {
    return Ipv4Address(0x0b000001 + ((id / 256) * 0x00010000) + ((id % 256) * 0x00000100));
}

// 在main函数中添加路由表读取函数
void ReadAndApplyRoutingTable(const std::vector<Ptr<Node>>& node_list,
                              Ipv4StaticRoutingHelper& staticRoutingHelper,
                              const std::map<std::pair<uint32_t, uint32_t>, Ipv4InterfaceContainer>& interface_map) {
    
    std::string routing_file = "scratch/routes.txt";
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
                std::cout << "添加默认路由: 节点 " << src_node << " -> " << next_hop_ip << std::endl;
            }
        }
        else if (route_type == "host") {
            // 主机路由: src_node host dst_host next_hop interface_index metric
            std::string dst_host_str, next_hop_str;
            uint32_t interface_index, metric;
            iss >> dst_host_str >> next_hop_str >> interface_index >> metric;
            
            uint32_t dst_host = std::stoi(dst_host_str);
            Ipv4Address dst_ip = node_id_to_ip(dst_host);
            
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
            std::cout << "添加主机路由: 节点 " << src_node << " -> " << dst_ip << std::endl;
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
                std::cout << "添加网络路由: 节点 " << src_node << " -> " << dst_network << "/" << netmask << std::endl;
            }
        }
    }
    routef.close();
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


    std::map<std::pair<uint32_t, uint32_t>, Ipv4InterfaceContainer> interface_map;

    // 分配IP地址并保存接口信息
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    for (auto& link : links) {
        Ipv4InterfaceContainer interfaces = ipv4.Assign(link.second);
        interface_map[link.first] = interfaces;
        ipv4.NewNetwork();
    }

    // 设置路由
    //Ipv4GlobalRoutingHelper::PopulateRoutingTables();//Dijkstra算法计算最短路径

    // 添加自定义路由
    Ipv4StaticRoutingHelper staticRoutingHelper;
    ReadAndApplyRoutingTable(node_list, staticRoutingHelper, interface_map);

    std::cout << "拓扑构建完成!" << std::endl;
    std::cout << "总节点数: " << nodes.GetN() << std::endl;
    std::cout << "链路数: " << link_num << std::endl;

    // 运行仿真
    Simulator::Run();
    Simulator::Destroy();
    return 0;
}