# GemSc AI 辅助开发规则和提示词指南

本文档定义了在GemSc项目中进行AI辅助开发需要遵循的规则和最佳实践。

## 1. 项目概述

GemSc是一个轻量级、可配置、事件驱动的硬件仿真框架，灵感来自Gem5，但使用纯C++实现，支持模块化建模、周期驱动行为、请求/响应通信与反压机制。

### 1.1 技术栈
- **语言**: C++17 (项目要求，不使用C++20)
- **构建系统**: CMake (最低版本3.16)
- **依赖管理**: nlohmann/json (单头文件库位于external/json/)
- **仿真模型**: 事件驱动，基于pre_tick/tick两阶段时序模型

### 1.2 项目结构
```
.
├── include/          # 头文件
│   ├── core/         # 核心组件
│   ├── ext/          # 扩展组件
│   ├── modules/      # 预定义模块
│   ├── sc_core/      # SystemC相关组件
│   └── utils/        # 工具类
├── src/              # 源文件
├── configs/          # 配置文件
├── docs/             # 文档
├── test/             # 测试文件
├── samples/          # 示例
└── external/         # 外部依赖
```

## 2. 代码风格规范

### 2.1 命名规范
- **类名**: PascalCase (例如 `SimObject`, `Packet`, `PortManager`)
- **函数名**: camelCase (例如 `handleUpstreamRequest`, `getPortManager`)
- **变量名**: camelCase (例如 `srcCycle`, `dstCycle`, `packetType`)
- **常量名**: SCREAMING_SNAKE_CASE (例如 `PKT_REQ_READ`, `PKT_RESP`)
- **文件名**: 小写+下划线或PascalCase (例如 `sim_object.hh`, `packet.hh`)

### 2.2 代码格式
- **缩进**: 使用4个空格，不使用Tab
- **行长度**: 最大120字符
- **大括号**: K&R风格，左括号与声明在同一行

```cpp
class ExampleClass {
public:
    void exampleMethod(int param) {
        if (param > 0) {
            // 代码实现
        }
    }
};
```

### 2.3 注释规范
- **文件头**: 包含版权信息和文件功能描述
- **类注释**: 使用Doxygen风格描述类的功能
- **函数注释**: 使用Doxygen风格描述参数、返回值和异常
- **行内注释**: 解释复杂逻辑，使用//风格

### 2.4 头文件包含顺序
1. 对应的头文件（如果有的话）
2. C系统头文件
3. C++系统头文件
4. 其他库头文件
5. 项目头文件

```cpp
#include "sim_object.hh"      // 对应头文件
#include <cstdint>            // C++系统头文件
#include <unordered_map>
#include <string>
#include "packet.hh"          // 项目头文件
#include "port_manager.hh"
```

## 3. C++语言标准

### 3.1 使用C++17
- **禁止使用C++20特性**，项目严格使用C++17
- **推荐特性**:
  - `std::optional<T>` 替代指针或异常来表示可能不存在的值
  - `std::filesystem` (如果可用)
  - `if constexpr` 进行编译时条件判断
  - 结构化绑定: `auto [key, value] = ...;`
  - `std::variant` 和 `std::visit` 实现类型安全的联合体

### 3.2 RAII原则
- 所有资源管理应遵循RAII（获取即初始化）原则
- 使用智能指针管理动态分配的内存
- 避免手动`new`/`delete`

### 3.3 异常处理
- **不使用异常**，通过返回值或断言处理错误
- 使用断言进行调试时检查

## 4. 设计模式和架构原则

### 4.1 核心架构
- **SimObject**: 所有模块的基类，实现`pre_tick()`和`tick()`方法
- **Port系统**: `MasterPort`（输出端口）和`SlavePort`（输入端口）实现通信
- **Packet**: 模块间通信的数据单元
- **EventQueue**: 事件驱动调度器
- **ModuleFactory**: 模块创建和配置管理

### 4.2 两阶段时序模型
- **pre_tick()**: 组合逻辑阶段，处理输入数据包，必须调用`port->tick()`
- **tick()**: 时序逻辑阶段，更新内部状态，发起新的请求

### 4.3 模块类型
- **Initiator**: 发起请求的模块（如CPUSim），有输出端口，处理响应
- **Target**: 接收请求的模块（如MemorySim），有输入端口，返回响应
- **Middle**: 中间模块（如CacheSim），有输入和输出端口，转发请求和响应

## 5. 开发实践

### 5.1 内存管理
- 使用`std::unique_ptr`或`std::shared_ptr`管理对象生命周期
- [Packet](file:///mnt/ubuntu/chisuhua/github/GemSc/include/packet.hh#L47-L95)的创建和销毁需要特别注意，响应包通常由接收方负责删除
- 避免内存泄漏，特别是在错误处理路径上

### 5.2 调试支持
- 使用`DPRINTF`宏进行调试输出
- 在关键路径上添加适当的调试信息
- 保持调试输出的模块化和可控制

### 5.3 JSON配置
- 所有模块应支持通过JSON配置文件进行参数设置
- 使用`nlohmann/json`库进行配置解析
- 配置验证应在初始化阶段完成

## 6. AI辅助开发提示词

### 6.1 通用提示词模板
当要求AI辅助开发时，应明确指定以下要素：

```
请基于GemSc仿真框架开发以下功能：

1. 使用C++17，不使用C++20特性
2. 遵循项目命名规范（类名PascalCase，函数名camelCase）
3. 继承SimObject或适当的基类
4. 实现pre_tick()和tick()方法
5. 使用Port系统进行模块间通信
6. 通过JSON配置支持模块参数设置
7. 添加适当的调试输出（DPRINTF）
8. 包含必要的头文件
9. 使用RAII原则管理资源
10. 遵循两阶段时序模型（pre_tick处理输入，tick更新状态）
```

### 6.2 模块开发提示词
当开发新模块时使用：

```
开发一个继承自SimObject的新模块类，实现以下要求：
- 构造函数接受name和EventQueue*参数
- 在构造函数中设置必要的端口
- 实现pre_tick()方法，处理输入端口的数据包
- 实现tick()方法，更新内部状态和发起新请求
- 实现必要的handle*方法处理数据包
- 使用PortManager管理端口
- 添加适当的调试输出
- 在模块注册表中注册该模块
```

### 6.3 连接和通信提示词
当处理模块间连接时使用：

```
在GemSc框架中，模块间通信通过端口系统实现：
- MasterPort用于发送请求和接收响应
- SlavePort用于接收请求和发送响应
- 在pre_tick()中调用port->tick()处理输入
- 在tick()中调用port->tick()处理滞留数据包
- 使用handleUpstreamRequest处理上游请求
- 使用handleDownstreamResponse处理下游响应
```

## 7. 常见陷阱和最佳实践

### 7.1 常见陷阱
- **忘记调用port->tick()**: 在pre_tick中必须调用端口的tick方法
- **时序错误**: 在pre_tick中发起的请求将在下一周期处理
- **内存泄漏**: Packet的创建和销毁需要配对
- **死锁**: 确保端口缓冲区有适当的反压机制

### 7.2 最佳实践
- **模块化设计**: 每个模块只负责单一职责
- **配置驱动**: 尽可能通过JSON配置实现灵活性
- **性能考虑**: 避免不必要的拷贝，使用引用传递大对象
- **测试覆盖**: 为每个新功能编写相应的测试

## 8. 代码审查检查清单

在代码审查时，使用以下清单：

- [ ] 遵循命名规范
- [ ] 使用C++17特性，不使用C++20
- [ ] 正确实现pre_tick和tick方法
- [ ] 正确使用端口系统
- [ ] 合理的内存管理
- [ ] 适当的调试输出
- [ ] JSON配置支持
- [ ] 遵循两阶段时序模型
- [ ] 适当的错误处理
- [ ] 充分的注释

## 9. 项目特定约定

### 9.1 调试宏定义
- `DPRINTF(CATEGORY, ...)` 用于调试输出
- 调试类别应使用大写，如CPU、CACHE、MEM等

### 9.2 统计系统
- 所有端口应支持统计收集
- 统计信息应包括请求数、响应数、字节数、延迟等
- 提供toString()方法格式化输出统计信息

### 9.3 扩展系统
- 使用TLM扩展机制添加自定义信息到数据包
- 扩展应实现clone()和copy_from()方法
- 扩展应有清晰的用途和文档

这份指南为AI辅助开发GemSc项目提供了完整的规范和提示词，可以作为后续开发的参考标准。