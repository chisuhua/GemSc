# GemSc 项目概述

GemSc 是一个轻量级、可配置、事件驱动的硬件仿真框架，灵感来自 Gem5，但使用纯 C++ 实现，支持模块化建模、周期驱动行为、请求/响应通信与反压机制。该项目适用于 CPU/GPU/内存子系统架构研究、SoC 前期验证、教学实验等场景。

## 核心特性

| 特性 | 说明 |
|------|------|
| 🔹 **类 Gem5 风格** | 模块继承 `SimObject`，每周期调用 `tick()` |
| 🔹 **事件驱动调度** | 基于 `EventQueue` 实现时间推进 |
| 🔹 **请求/响应通信** | 使用 `Packet` 封装 `tlm_generic_payload` |
| 🔹 **PortPair 连接机制** | 类似 Gem5 的端口抽象，支持双向通信 |
| 🔹 **反压支持** | 缓冲区满时返回 `false`，上游可处理背压 |
| 🔹 **配置驱动** | 使用 JSON 配置文件定义模块与连接关系 |
| 🔹 **无 SystemC 依赖** | 使用简化版 `tlm_fake.hh`，无需链接 `libsystemc.so` |

## 架构概览

```
+------------------+     +------------------+     +------------------+
|    CPUSim        |<--->|    CacheSim      |<--->|    MemorySim     |
| (Initiator)      |     | (Middle Module)  |     | (Target)         |
+------------------+     +------------------+     +------------------+
       ↑                       ↑                        ↑
       |                       |                        |
       +---------> EventQueue <-------------------------+
                    (全局事件调度器)
```

- 所有模块继承 `SimObject`，实现 `tick()`。
- 模块间通过 `PortPair` 连接，通信基于 `SimplePort` 接口。
- 使用 JSON 配置文件动态构建系统拓扑。

## 核心概念

### 1. `SimObject`
所有仿真模块的基类，必须实现 `tick()`，表示每周期的行为。

```cpp
class MyModule : public SimObject {
    void tick() override;
};
```

### 2. `SimplePort` 与 `PortPair`
- `SimplePort`：提供 `send()` 和纯虚 `recv()` 接口。
- `PortPair`：连接两个 `SimplePort`，实现双向通信。

```cpp
PortPair* pair = new PortPair(port_a, port_b);
```

- `port_a->send(pkt)` → 自动调用 `port_b->recv(pkt)`
- `port_b->send(pkt)` → 自动调用 `port_a->recv(pkt)`

> 这是 **请求/响应通信的基础**。

### 3. 请求（req）与响应（resp）流

| 流向 | 调用路径 |
|------|----------|
| **Req: CPU → Cache → Memory** | `cpu.out_port->send(req)` → `cache.recv(req)` → `memory.recv(req)` |
| **Resp: Memory → Cache → CPU** | `memory.send(resp)` → `cache.recv(resp)` → `cpu.out_port->recv(resp)` |

> `send()` 的本质是"触发对端的 `recv()`"。

### 4. 模块角色分类

| 角色 | 示例 | 是否继承 `SimplePort` | 是否有 `out_port` | `recv()` 用途 |
|------|------|------------------------|-------------------|----------------|
| **Initiator** | `CPUSim`, `TrafficGenerator` | ❌ 否 | ✅ 是 | 接收响应（resp） |
| **Target** | `MemorySim` | ✅ 是 | ❌ 否 | 接收请求（req） |
| **Middle** | `CacheSim` | ✅ 是 | ✅ 是 | 接收 req 和 resp |

### 5. `Packet` 类型

```cpp
enum PacketType {
    PKT_REQ_READ,
    PKT_REQ_WRITE,
    PKT_RESP,
    PKT_STREAM_DATA,
    PKT_CREDIT_RETURN
};
```

封装 `tlm_generic_payload`，支持多种通信语义。