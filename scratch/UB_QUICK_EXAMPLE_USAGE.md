# UB Quick Example 使用说明

## 概述
`ub-quick-example` 已经优化，提供了更友好的命令行界面和更清晰的输出显示。

## 主要改进

### 1. 命令行参数支持
现在支持以下命令行参数：

- `--case-dir <路径>`: 指定用例文件夹路径（默认: `scratch/test_CLOS`）
- `--trace-dir <路径>`: 指定ParseTrace使用的输出文件夹（默认: 与用例文件夹相同）
- `--help` 或 `-h`: 显示帮助信息
- `--ClassName <类名>`: 查询指定类的属性信息
- `--AttributeName <属性>`: 查询指定属性的详细信息（需配合--ClassName使用）

### 2. 改进的进度显示
`CheckExampleProcess` 现在在同一行更新显示，不再产生大量重复输出：
- 使用 `\r` 进行同行刷新
- 显示检查次数计数器
- 完成后自动换行

### 3. 灵活的Trace目录配置
`ParseTrace` 函数现在支持自定义trace输出目录，方便将trace结果保存到不同位置。

## 使用示例

### 基本使用（使用默认参数）
```bash
./ns3 run scratch/ub-quick-example
```

### 指定用例文件夹
```bash
./ns3 run 'scratch/ub-quick-example --case-dir scratch/test_CLOS'
```

### 指定用例文件夹和trace输出目录
```bash
./ns3 run 'scratch/ub-quick-example --case-dir scratch/test_CLOS --trace-dir /tmp/trace'
```

### 查看帮助信息
```bash
./ns3 run 'scratch/ub-quick-example --help'
```

### 查询类属性信息
```bash
./ns3 run 'scratch/ub-quick-example --ClassName ns3::UbPort'
```

### 查询特定属性详情
```bash
./ns3 run 'scratch/ub-quick-example --ClassName ns3::UbPort --AttributeName UbDataRate'
```

## 输出示例

运行时输出：
```
[20:08:48]:Run case: scratch/test_CLOS
[20:08:48]:Set component attributes
[20:08:48]:Create node.
[20:08:48]:Start Client.
[20:08:48]:Check Example Process... (2) | Sim Time: 4200.00 us
[20:08:48]:Simulator finished.
[20:08:48]:Start Parse Trace File.
[20:08:48]:Program finished.
[20:08:48]:程序运行时间: 53 毫秒
```

注意 `Check Example Process... (2)` 现在只显示一行，数字表示检查次数。

## 技术细节

### 修改的文件

1. **scratch/ub-quick-example.cc**
   - 添加了 `PrintHelp()` 函数显示帮助信息
   - 改进了 `CheckExampleProcess()` 使用同行刷新显示进度
   - 增强了 `main()` 函数的命令行参数解析

2. **src/unified-bus/model/ub-utils.h**
   - 更新了 `ParseTrace()` 函数签名，支持自定义trace目录参数
   - 添加了路径规范化处理（确保以/结尾）

### 向后兼容性
所有更改都保持向后兼容：
- 不提供参数时使用默认值
- 保留了原有的 `--ClassName` 和 `--AttributeName` 功能
- `ParseTrace()` 函数的默认行为不变

## 注意事项

1. 使用带参数的命令时，需要用单引号包裹整个命令：
   ```bash
   ./ns3 run 'scratch/ub-quick-example --case-dir scratch/test_CLOS'
   ```

2. trace-dir 参数是可选的，如果不指定，将使用用例文件夹作为trace输出目录

3. 确保指定的用例文件夹包含所有必需的配置文件：
   - network_attribute.txt
   - node.csv
   - topology.csv
   - routing_table.csv
   - transport_channel.csv
   - traffic.csv
   - fault.csv（如果启用故障模块）

## 故障排除

如果遇到 "Can not open File" 错误，请检查：
- 指定的 case-dir 路径是否正确
- 配置文件是否存在且格式正确
- 路径权限是否正确

## 更新日志

### 2025-10-12
- ✅ 添加命令行参数支持（--case-dir, --trace-dir, --help）
- ✅ 优化 CheckExampleProcess 输出为同行更新
- ✅ 更新 ParseTrace 函数支持自定义trace目录
