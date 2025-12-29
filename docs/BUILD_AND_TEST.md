# GemSc 构建和测试指南

本文档提供了 GemSc 项目的构建、测试和部署说明。

## 1. 系统要求

### 1.1 硬件要求
- CPU: 支持 C++17 的处理器
- 内存: 至少 2GB RAM（推荐 4GB 或更多）
- 存储: 至少 500MB 可用空间

### 1.2 软件要求
- 操作系统: Linux (推荐 Ubuntu 18.04+), macOS, 或 Windows (WSL/MSYS2)
- 编译器: GCC 7+ 或 Clang 6+ (支持 C++17)
- CMake: 3.16 或更高版本
- Git: 2.0 或更高版本

## 2. 依赖项

GemSc 的依赖项包括：
- **nlohmann/json**: 单头文件库，已包含在 `external/json/`
- **C++17 标准库**: 线程、智能指针、可选类型等
- **SystemC** (可选): 项目包含 `systemc-3.0.1/` 目录，但核心框架不依赖它

## 3. 构建步骤

### 3.1 克隆项目

```bash
git clone https://github.com/yourname/gem5-mini.git
cd gem5-mini
```

### 3.2 创建构建目录

```bash
mkdir build && cd build
```

### 3.3 生成构建文件

```bash
cmake ..
```

### 3.4 编译项目

```bash
make
```

### 3.5 完整构建命令

```bash
# 1. 克隆项目
git clone https://github.com/yourname/gem5-mini.git
cd gem5-mini

# 2. 创建构建目录
mkdir build && cd build

# 3. 生成 Makefile 并编译
cmake ..
make
```

## 4. CMake 配置选项

### 4.1 调试版本
```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
```

### 4.2 发布版本
```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

### 4.3 启用调试打印
```bash
cmake -DDEBUG_PRINT=ON ..
```

### 4.4 启用 AddressSanitizer
取消 CMakeLists.txt 中的注释：
```cmake
target_compile_options(sim PRIVATE -fsanitize=address -fno-omit-frame-pointer)
target_link_options(sim PRIVATE -fsanitize=address)
```

## 5. 运行示例

### 5.1 CPU → Cache → Memory 示例
```bash
./sim ../configs/cpu_example.json
```

### 5.2 Traffic Generator 示例
```bash
./sim ../configs/tg_example.json
```

### 5.3 层次化示例
```bash
./sim_hierarchy ../configs/example_hierarchy/system.json
```

## 6. 测试系统

GemSc 包含全面的测试套件：

### 6.1 运行所有测试
```bash
make test
```

### 6.2 运行特定测试
```bash
./test_config_loader
./test_connection_resolution
./test_credit_flow
# ... 等等
```

### 6.3 测试列表

GemSc 提供了以下测试：

- `test_config_loader`: 配置加载测试
- `test_connection_resolution`: 连接解析测试
- `test_credit_flow`: 信用流控测试
- `test_end_to_end_delay`: 端到端延迟测试
- `test_json_includer`: JSON 包含功能测试
- `test_latency_injection`: 延迟注入测试
- `test_layout_styles`: 布局样式测试
- `test_module_group`: 模块组测试
- `test_module_registration`: 模块注册测试
- `test_packet_pool`: 数据包池测试
- `test_regex_matching`: 正则匹配测试
- `test_response_path`: 响应路径测试
- `test_stats_accuracy`: 统计准确性测试
- `test_tlm_ext`: TLM 扩展测试
- `test_valid_only`: 仅验证测试
- `test_valid_ready`: 验证就绪测试
- `test_virtual_channel`: 虚拟通道测试
- `test_wildcard_matching`: 通配符匹配测试

## 7. Python 工具

项目包含 Python 脚本来辅助开发：

### 7.1 NoC 生成器
```bash
python3 ../python/noc_builder.py
```

### 7.2 网格网络生成器
```bash
python3 ../python/noc_mesh.py
```

## 8. 部署

### 8.1 可执行文件
构建完成后，主要的可执行文件包括：
- `sim`: 基本仿真器
- `sim_hierarchy`: 层次化仿真器
- `sim_layout`: 布局仿真器
- `traffic_sim`: 流量仿真器

### 8.2 配置文件
所有配置文件位于 `configs/` 目录下，支持：
- JSON 配置文件
- 包含指令 (`$include`)
- 通配符和正则匹配

## 9. 故障排除

### 9.1 编译错误
如果遇到 C++17 特性错误，请确保使用支持 C++17 的编译器：
```bash
g++ --version
cmake .. -DCMAKE_CXX_COMPILER=g++-8
```

### 9.2 链接错误
如果遇到链接错误，确保所有依赖项都已正确安装。

### 9.3 运行时错误
启用调试打印以获取更多信息：
```bash
cmake -DDEBUG_PRINT=ON .. && make
./sim ../configs/cpu_example.json
```

## 10. 性能优化

### 10.1 编译优化
使用 Release 模式进行性能测试：
```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

### 10.2 内存优化
- 使用 AddressSanitizer 检测内存错误
- 使用 Valgrind 进行内存分析

### 10.3 并行构建
使用多核加速构建：
```bash
make -j$(nproc)
```

## 11. 开发工作流

### 11.1 添加新模块
1. 在 `include/modules/` 中创建头文件
2. 在 `src/modules/` 中创建源文件（如果需要）
3. 在 `modules.hh` 中注册模块
4. 编译并测试

### 11.2 添加新测试
1. 在 `test/` 目录中创建测试文件
2. 在 CMakeLists.txt 中添加测试
3. 运行测试以验证功能

### 11.3 代码规范
- 使用 C++17 特性和 RAII 模式
- 遵循命名约定（类名使用 PascalCase，变量名使用 camelCase）
- 添加适当的注释和文档
- 编写单元测试

## 12. CI/CD 集成

在 CI/CD 环境中，可以使用以下脚本进行自动化构建和测试：

```bash
#!/bin/bash
set -e  # 遇到错误时退出

# 克隆并构建
git clone https://github.com/yourname/gem5-mini.git
cd gem5-mini
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)

# 运行测试
ctest --output-on-failure

# 运行示例
./sim ../configs/cpu_example.json
```