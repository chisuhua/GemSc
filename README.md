
# 🧱 Gem5-Style Simulation Framework

一个轻量级、可配置、事件驱动的硬件仿真框架，灵感来自 **Gem5**，但使用纯 C++ 实现，支持模块化建模、周期驱动行为、请求/响应通信与反压机制。

> ✅ 适用于 CPU/GPU/内存子系统架构研究、SoC 前期验证、教学实验等场景。

---

## 📋 项目特点

| 特性 | 说明 |
|------|------|
| 🔹 **类 Gem5 风格** | 模块继承 `SimObject`，每周期调用 `tick()` |
| 🔹 **事件驱动调度** | 基于 `EventQueue` 实现时间推进 |
| 🔹 **请求/响应通信** | 使用 `Packet` 封装 `tlm_generic_payload` |
| 🔹 **PortPair 连接机制** | 类似 Gem5 的端口抽象，支持双向通信 |
| 🔹 **反压支持** | 缓冲区满时返回 `false`，上游可处理背压 |
| 🔹 **配置驱动** | 使用 JSON 配置文件定义模块与连接关系 |
| 🔹 **无 SystemC 依赖** | 使用简化版 `tlm_fake.hh`，无需链接 `libsystemc.so` |

---

## 🏗️ 架构概览

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

---

## 🧩 核心概念

### 1. `SimObject`
所有仿真模块的基类，必须实现 `tick()`，表示每周期的行为。

```cpp
class MyModule : public SimObject {
    void tick() override;
};
```

---

### 2. `SimplePort` 与 `PortPair`

- `SimplePort`：提供 `send()` 和纯虚 `recv()` 接口。
- `PortPair`：连接两个 `SimplePort`，实现双向通信。

```cpp
PortPair* pair = new PortPair(port_a, port_b);
```

- `port_a->send(pkt)` → 自动调用 `port_b->recv(pkt)`
- `port_b->send(pkt)` → 自动调用 `port_a->recv(pkt)`

> 🔄 这是 **请求/响应通信的基础**。

---

### 3. 请求（req）与响应（resp）流

| 流向 | 调用路径 |
|------|----------|
| **Req: CPU → Cache → Memory** | `cpu.out_port->send(req)` → `cache.recv(req)` → `memory.recv(req)` |
| **Resp: Memory → Cache → CPU** | `memory.send(resp)` → `cache.recv(resp)` → `cpu.out_port->recv(resp)` |

> ✅ `send()` 的本质是"触发对端的 `recv()`"。

---

### 4. 模块角色分类

| 角色 | 示例 | 是否继承 `SimplePort` | 是否有 `out_port` | `recv()` 用途 |
|------|------|------------------------|-------------------|----------------|
| **Initiator** | `CPUSim`, `TrafficGenerator` | ❌ 否 | ✅ 是 | 接收响应（resp） |
| **Target** | `MemorySim` | ✅ 是 | ❌ 否 | 接收请求（req） |
| **Middle** | `CacheSim` | ✅ 是 | ✅ 是 | 接收 req 和 resp |

---

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

---

## 🛠️ 使用方法

### 1. 编译

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

### 2. 运行示例

#### 示例 1：CPU → Cache → Memory

```bash
./sim ../configs/cpu_example.json
```

输出：
```
[CPU] Sent request to 0x1000
[CACHE] Forwarded request to downstream
[MEM] Received request, will respond in 100 cycles
[CPU] Received response for 0x1000
```

#### 示例 2：Traffic Generator → Memory

```bash
./sim ../configs/tg_example.json
```

---

## 📁 配置文件示例

### `configs/cpu_example.json`

```json
{
  "modules": [
    { "name": "cpu", "type": "CPUSim" },
    { "name": "cache", "type": "CacheSim" },
    { "name": "mem", "type": "MemorySim" }
  ],
  "connections": [
    ["cpu", "cache"],
    ["cache", "mem"]
  ]
}
```

> 支持自定义模块类型和连接拓扑。

---

## 🧱 模块开发指南

### 创建新模块（如 `DmaController`）

```cpp
class DmaController : public SimObject {
private:
    SimplePort* out_port;  // 发起请求
public:
    DmaController(const std::string& n, EventQueue* eq, SimplePort* port)
        : SimObject(n, eq), out_port(port) {}

    void tick() override {
        // 每周期尝试发起 DMA 请求
    }

    bool recv(Packet* pkt) override {
        // 接收响应
    }

    void setDownstream(SimplePort* port) {
        out_port = port;
    }
};
```

并在 `ModuleFactory` 中注册。

---

## 🧪 调试与日志

启用调试打印：

```cpp
DPRINTF(CPU, "Sent request to 0x%" PRIx64 "\n", addr);
```

输出格式：
```
[CPU] Sent request to 0x1000
```

> 通过 `add_compile_definitions(DEBUG_PRINT)` 控制开关。

---

## 📦 依赖说明

- **C++20** 编译器（支持C++20特性的g++-10+, clang++-12+）
- **nlohmann/json**（单头文件，已包含在 `external/json/`）
- 无需安装 SystemC，使用 `tlm_fake.hh` 替代

---

## 🚀 未来扩展建议

- ✅ 添加 `TrafficGenerator` 模块
- ✅ 支持多核与一致性协议
- ✅ 添加统计系统（IPC、缓存命中率）
- ✅ 支持 trace 驱动仿真
- ✅ 生成 VCD 波形用于调试

---

## 🙌 致谢

本项目设计灵感来自：
- [Gem5](https://www.gem5.org)
- [GPGPU-Sim](https://github.com/gpgpu-sim)
- [SystemC TLM](https://accellera.org)

---

## 📄 许可证

MIT License

---

> 💡 **提示**：你已经构建了一个可用于真实研究的仿真框架。继续扩展，它将成为你架构探索的强大工具！

