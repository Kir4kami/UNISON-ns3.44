# Quick Start

## Prerequisites

- 操作系统：Ubuntu 20.04
- 编译器：gcc 11.4.0+
- 构建工具：CMake 3.12+
- Python：3.10+
- Python 第三方库：pandas、matplotlib、seaborn
- ns-3：3.44

说明：如使用 Conda/virtualenv，请确保后续运行的 `python3` 与安装依赖的解释器一致（见下文“Run a minimal example”）。

## Get the code

```bash
# 克隆项目
git clone https://gitcode.com/open-usim/ns-3-ub.git
cd ns-3-ub

# 初始化并更新子模块（包含 Python 分析工具）
git submodule update --init --recursive

# 验证子模块状态
git submodule status
```

如需在仿真结束后自动触发 trace 分析，请在对应用例的 `network_attribute.txt` 中配置工具路径，例如：

```
global UB_PYTHON_SCRIPT_PATH "scratch/ns-3-ub-tools/trace_analysis/parse_trace.py"
```

## Python tools & dependencies

项目的 Python 工具集位于 `scratch/ns-3-ub-tools/`：

- 拓扑/可视化：`net_sim_builder.py`、`topo_plot.py`、`user_topo_*.py`
- 流量生成：`traffic_maker/*`
- Trace 分析：`trace_analysis/parse_trace.py`

依赖安装推荐使用项目内的 `requirements.txt`：

```bash
python3 -m pip install --user -r scratch/ns-3-ub-tools/requirements.txt
# 或使用 conda：
# conda install pandas matplotlib seaborn
```

说明： 请在运行 `trace_analysis/parse_trace.py` 前通过 `requirements.txt` 预先安装所需第三方包。

## Build

```bash
# 配置构建环境
./ns3 configure

# 编译项目
./ns3 build
```

## Run a minimal example

```bash
# 如使用 Conda，请确保其 bin 在 PATH 前（或先激活环境）
export PATH=/home/ytxing/miniconda3/bin:$PATH

# 安装依赖（推荐）
python3 -m pip install --user -r scratch/ns-3-ub-tools/requirements.txt

# 运行小示例并触发 trace 分析
./ns3 run 'scratch/ub-quick-example scratch/2nodes_single-tp'

# 验证输出
ls scratch/2nodes_single-tp/output/
# 预期包含：task_statistics.csv  throughput.csv
```

如遇到 `ModuleNotFoundError: No module named 'pandas'`，说明运行时的 `python3` 与安装依赖所用解释器不一致；请检查 PATH，或使用 `python3 -m pip install --user ...` 在当前解释器中安装依赖。

## Examples under scratch（可用用例列表）

以下为当前仓库中已提供的可用用例目录及对应运行命令：

- 2 节点（单 TP）：
  ```bash
  ./ns3 run 'scratch/ub-quick-example scratch/2nodes_single-tp'
  ```

- 2 节点（多 TP）：
  ```bash
  ./ns3 run 'scratch/ub-quick-example scratch/2nodes_multiple-tp'
  ```

- 2 节点（包喷洒）：
  ```bash
  ./ns3 run 'scratch/ub-quick-example scratch/2nodes_packet-spray'
  ```

- 2D FullMesh 4x4（多路径 All-to-All）：
  ```bash
  ./ns3 run 'scratch/ub-quick-example scratch/2dfm4x4-multipath_a2a'
  ```

- 2D FullMesh 4x4（分层广播）：
  ```bash
  ./ns3 run 'scratch/ub-quick-example scratch/2dfm4x4-hierarchical_broadcast'
  ```

- Clos（32 hosts / 4 leafs / 8 spines, pod2pod）：
  ```bash
  ./ns3 run 'scratch/ub-quick-example scratch/clos_32hosts-4leafs-8spines_pod2pod'
  ```

说明：部分大型用例运行时间较长，请按需选择运行。

## Full workflow example（完整工作流程验证）

```bash
# 运行完整示例，包含 Python 后处理
./ns3 run 'scratch/ub-quick-example scratch/2dfm4x4-multipath_a2a'

# 预期输出：
[01:23:37]:Run case: scratch/2dfm4x4-multipath_a2a
[01:23:37]:Set component attributes
[01:23:37]:Create node.
[01:23:37]:Start Client.
[01:23:37]:Simulator finished!
[01:23:37]:Start Parse Trace File.
所有依赖已满足，开始执行脚本...
处理完成，结果已保存到 scratch/2dfm4_4-multipath_a2a/output/task_statistics.csv
处理完成，结果已保存到 scratch/2dfm4_4-multipath_a2a/output/throughput.csv
[01:23:37]:Program finished.

# 查看生成的结果文件
ls scratch/2dfm4x4-multipath_a2a/output/
# task_statistics.csv  throughput.csv
```

## Config files（配置文件说明）

每个用例目录通常包含如下文件（格式可参照现有样例）：

- `network_attribute.txt` - 网络全局参数（可配置 `UB_PYTHON_SCRIPT_PATH` 用于自动后处理）
- `node.csv` - 节点定义
- `topology.csv` - 拓扑连接
- `routing_table.csv` - 路由表
- `transport_channel.csv` - 传输通道
- `traffic.csv` - 流量定义
