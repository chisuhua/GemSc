# **GemSc 电路仿真建模用户使用手册 (Final Release)**

本手册旨在指导用户如何使用 GemSc 框架，通过 JSON 配置文件构建层次化、多线程、事件驱动的电路仿真模型。框架的核心是通过 `pre_tick` 和 `tick` 两阶段精确模拟硬件中的组合逻辑与时序逻辑。

## **1. 核心概念**

在开始之前，请理解以下核心概念：

*   **`SimObject`**: 仿真的基本单元，代表一个硬件模块（如 CPU、Cache）。每个 `SimObject` **必须**实现 `pre_tick()` 和 `tick()` 两个生命周期函数。
    *   **用户契约**：在 `pre_tick` 中，您**必须**调用 `port->tick()` 来处理端口队列中的数据包，否则输入将永远不会被消费。
*   **`SimModule`**: `SimObject` 的容器，用于构建层次化设计。它通过内部的 `ModuleFactory` 实例化和管理一组子模块，并通过 `inputs`/`outputs` 配置将其内部端口暴露给上层。
*   **`EventQueue`**: 事件队列，代表一个独立的执行线程。仿真时间以“周期”(cycle) 为单位推进。
*   **`ClockDomain`**: 一个逻辑时钟域，包含一个或多个并行的 `EventQueue` (线程)。属于同一 `ClockDomain` 的模块共享相同的时钟频率。
*   **`pre_tick()`**: 在每个仿真周期开始时调用，**专门用于处理所有新到达的输入数据包**。此阶段会迭代执行，直到系统内没有新的零延迟输入产生。
    *   **模拟对象**：组合逻辑 (Combinational Logic)。
*   **`tick()`**: 在 `pre_tick` 阶段结束后调用，用于执行模块的核心逻辑，如状态更新、发起新的请求等。
    *   **用户契约**：在 `tick` 中，您**应该**调用 `output_port->tick()` 来尝试发送在之前周期中因拥塞而滞留的数据包。
    *   **模拟对象**：时序逻辑 (Sequential Logic)。
*   **端口 (`Port`)**:
    *   **`MasterPort` (下游/输出端口)**: 用于**发送请求** (`sendReq`) 和**接收响应** (`recvResp`)。
    *   **`SlavePort` (上游/输入端口)**: 用于**接收请求** (`recvReq`) 和**发送响应** (`sendResp`)。
*   **`Packet`**: 模块间传递的数据包，封装了 TLM 有效载荷和仿真元数据。
    *   **重要**：`Packet` 类本身**没有** `addr` 成员。地址信息应通过 `payload->get_address()` 或 TLM 扩展（如 `ReadCmdExt`）传递。
*   **JSON 配置**: 完全通过 JSON 文件描述系统结构、模块参数、连接关系和时钟域/线程绑定。

## **2. 使用流程**

GemSc 的使用遵循以下标准化流程：

### **步骤 1: 定义和注册你的模块**

首先，使用 C++ 编写你的硬件模块，并将其注册到 `ModuleFactory`。

**示例 1: 定义一个简单的 CPU 模块 (`SimpleCPU`)**

```cpp
// simple_cpu.hh
#ifndef SIMPLE_CPU_HH
#define SIMPLE_CPU_HH

#include "sim_object.hh"
#include "packet.hh"
#include "ext/cmd_exts.hh" // 用于高级命令

class SimpleCPU : public SimObject {
private:
    uint64_t next_addr = 0x1000;
    int cycle_count = 0;

public:
    SimpleCPU(const std::string& name, EventQueue* eq) : SimObject(name, eq) {
        // 创建一个下游端口，用于向缓存发送请求
        getPortManager().addDownstreamPort(this, {4}, {}, "cache_port");
    }

    void pre_tick() override {
        // 处理来自缓存的响应
        auto* port = getPortManager().getDownstreamPort("cache_port");
        if (port) {
            port->tick(); // 这会自动调用 handleDownstreamResponse
        }
    }

    void tick() override {
        cycle_count++;
        // 每3个周期发起一次新的读请求
        if (cycle_count % 3 == 0) {
            sendReadRequest();
        }
        // 尝试发送之前因拥塞而滞留的包
        auto* port = getPortManager().getDownstreamPort("cache_port");
        if (port) {
            port->tick();
        }
    }

    bool handleDownstreamResponse(Packet* pkt, int src_id, const std::string& src_label) override {
        DPRINTF(CPU, "[%s] Received response for addr 0x%lx\n", getName().c_str(), pkt->payload->get_address());
        // 模拟处理响应
        delete pkt;
        return true;
    }

private:
    void sendReadRequest() {
        // 1. 创建 TLM 有效载荷
        auto* payload = new tlm::tlm_generic_payload();
        payload->set_address(next_addr);
        payload->set_data_length(64); // 读取64字节
        payload->set_command(tlm::TLM_READ_COMMAND);

        // 【推荐】使用 TLM 扩展传递更复杂的命令信息
        auto* cmd_ext = new ReadCmdExt(ReadCmd(next_addr, 64));
        payload->set_extension(cmd_ext);

        // 2. 创建 GemSc Packet
        auto* pkt = new Packet(payload, getCurrentCycle(), PKT_REQ);
        pkt->cmd = CMD_READ;
        pkt->vc_id = 0;

        // 3. 通过端口发送
        auto* port = getPortManager().getDownstreamPort("cache_port");
        if (port && port->sendReq(pkt)) {
            DPRINTF(CPU, "[%s] Sent read request to 0x%lx\n", getName().c_str(), next_addr);
            next_addr += 64;
        } else {
            DPRINTF(CPU, "[%s] Failed to send request, backpressure\n", getName().c_str());
            delete pkt;
        }
    }
};

// 注册
ModuleFactory::registerObject<SimpleCPU>("SimpleCPU");
```

**示例 2: 定义一个简单的缓存模块 (`SimpleCache`)**

```cpp
// simple_cache.hh
#ifndef SIMPLE_CACHE_HH
#define SIMPLE_CACHE_HH

#include "sim_object.hh"
#include "packet.hh"

class SimpleCache : public SimObject {
private:
    std::unordered_map<uint64_t, bool> cache_data; // 简化的缓存模型

public:
    SimpleCache(const std::string& name, EventQueue* eq) : SimObject(name, eq) {
        getPortManager().addUpstreamPort(this, {4}, {}, "cpu_req");
        getPortManager().addDownstreamPort(this, {4}, {}, "mem_port");
    }

    void pre_tick() override {
        auto* upstream = getPortManager().getUpstreamPort("cpu_req");
        auto* downstream = getPortManager().getDownstreamPort("mem_port");
        if (upstream) upstream->tick();
        if (downstream) downstream->tick();
    }

    void tick() override {
        // 缓存模块在 tick 阶段通常不主动发起请求
    }

    bool handleUpstreamRequest(Packet* pkt, int src_id, const std::string& src_label) override {
        uint64_t addr = pkt->payload->get_address();
        DPRINTF(CACHE, "[%s] Handling CPU request for addr 0x%lx\n", getName().c_str(), addr);

        if (isHit(addr)) {
            DPRINTF(CACHE, "[%s] Cache Hit!\n", getName().c_str());
            auto* resp_payload = new tlm::tlm_generic_payload();
            resp_payload->set_response_status(tlm::TLM_OK_RESPONSE);
            auto* resp_pkt = new Packet(resp_payload, getCurrentCycle(), PKT_RESP);
            resp_pkt->original_req = pkt;
            resp_pkt->addr = addr; // 用于统计，非传输

            auto* port = getPortManager().getUpstreamPort(src_label);
            if (port) {
                port->sendResp(resp_pkt);
            } else {
                delete resp_pkt;
            }
        } else {
            DPRINTF(CACHE, "[%s] Cache Miss, forwarding to memory\n", getName().c_str());
            auto* mem_port = getPortManager().getDownstreamPort("mem_port");
            if (mem_port && mem_port->sendReq(pkt)) {
                return true;
            } else {
                DPRINTF(CACHE, "[%s] Memory port busy, dropping request\n", getName().c_str());
                delete pkt;
            }
        }
        return true;
    }

    bool handleDownstreamResponse(Packet* pkt, int src_id, const std::string& src_label) override {
        DPRINTF(CACHE, "[%s] Handling memory response for addr 0x%lx\n", getName().c_str(), pkt->addr);
        auto* upstream_port = getPortManager().getUpstreamPort("cpu_req");
        if (upstream_port) {
            upstream_port->sendResp(pkt);
        } else {
            delete pkt;
        }
        return true;
    }

private:
    bool isHit(uint64_t addr) {
        return (rand() % 2) == 0;
    }
};

ModuleFactory::registerObject<SimpleCache>("SimpleCache");
```

**示例 3: 定义一个简单的内存控制器模块 (`SimpleMemoryController`)**

```cpp
// simple_mem_ctrl.hh
#ifndef SIMPLE_MEM_CTRL_HH
#define SIMPLE_MEM_CTRL_HH

#include "sim_object.hh"
#include "packet.hh"

class SimpleMemoryController : public SimObject {
public:
    SimpleMemoryController(const std::string& name, EventQueue* eq) : SimObject(name, eq) {
        getPortManager().addUpstreamPort(this, {4}, {}, "cache_req");
    }

    void pre_tick() override {
        auto* port = getPortManager().getUpstreamPort("cache_req");
        if (port) port->tick();
    }

    void tick() override {}

    bool handleUpstreamRequest(Packet* pkt, int src_id, const std::string& src_label) override {
        uint64_t addr = pkt->payload->get_address();
        DPRINTF(MEM, "[%s] Received request for addr 0x%lx\n", getName().c_str(), addr);

        auto* resp_payload = new tlm::tlm_generic_payload();
        resp_payload->set_response_status(tlm::TLM_OK_RESPONSE);
        auto* resp_pkt = new Packet(resp_payload, getCurrentCycle(), PKT_RESP);
        resp_pkt->original_req = pkt;
        resp_pkt->addr = addr;

        auto* port = getPortManager().getUpstreamPort(src_label);
        if (port) {
            port->sendResp(resp_pkt);
        } else {
            delete resp_pkt;
        }

        return true;
    }
};

ModuleFactory::registerObject<SimpleMemoryController>("SimpleMemoryController");
```

### **步骤 2: 编写 JSON 配置文件**

配置文件定义了系统的静态结构和动态绑定。

**顶层配置文件: `system_config.json`**

```json
{
  "plugin": [],
  "clock_domains": [
    {
      "name": "core_domain",
      "frequency": 2000,
      "threads": [
        {
          "name": "core_thread",
          "objects": [
            "cpu_core_0",
            "l1_cache_0"
          ]
        }
      ]
    },
    {
      "name": "memory_domain",
      "frequency": 500,
      "threads": [
        {
          "name": "memory_thread",
          "objects": [
            "memory_controller_0"
          ]
        }
      ]
    }
  ],
  "modules": [
    {
      "name": "cpu_core_0",
      "type": "SimpleCPU"
    },
    {
      "name": "l1_cache_0",
      "type": "SimpleCache"
    },
    {
      "name": "memory_controller_0",
      "type": "SimpleMemoryController"
    }
  ],
  "connections": [
    {
      "src": "cpu_core_0.cache_port",
      "dst": "l1_cache_0.cpu_req",
      "latency": 1
    },
    {
      "src": "l1_cache_0.mem_port",
      "dst": "memory_controller_0.cache_req",
      "latency": 10
    }
  ]
}
```

### **步骤 3: 编写主函数并启动仿真**

主函数负责解析配置、实例化模块、绑定线程并启动仿真循环。

```cpp
// main.cpp
#include "sim_object.hh"
#include "module_factory.hh"
#include "event_queue.hh"
#include "simple_cpu.hh"
#include "simple_cache.hh"
#include "simple_mem_ctrl.hh"
#include "zero_latency_arbiter.hh" // 新增
#include "simple_producer.hh"      // 新增
#include "simple_consumer.hh"      // 新增

#include <nlohmann/json.hpp>
#include <fstream>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

// 全局同步变量
std::atomic<bool> g_input_ready_flag{false};
std::mutex g_sync_mutex;
std::condition_variable g_sync_cv;
std::atomic<int> g_active_threads{0};

// 线程执行函数
void run_simulation_thread(EventQueue* eq, const std::vector<SimObject*>& objects, uint64_t total_cycles) {
    for (SimObject* obj : objects) {
        obj->initiate_tick();
    }

    for (uint64_t cycle = 0; cycle < total_cycles; ++cycle) {
        {
            std::lock_guard<std::mutex> lock(g_sync_mutex);
            g_active_threads++;
        }

        // Phase 1: 迭代执行 pre_tick 直到稳定
        bool local_input_ready;
        do {
            local_input_ready = false;
            for (SimObject* obj : objects) {
                obj->pre_tick();
            }
            local_input_ready = g_input_ready_flag.exchange(false);
        } while (local_input_ready);

        // Phase 2: 执行 tick
        for (SimObject* obj : objects) {
            obj->tick();
        }

        // 同步屏障
        {
            std::unique_lock<std::mutex> lock(g_sync_mutex);
            g_active_threads--;
            if (g_active_threads == 0) {
                g_sync_cv.notify_all();
            } else {
                g_sync_cv.wait(lock, [] { return g_active_threads == 0; });
            }
        }
    }
}

int main() {
    // 注册所有模块
    ModuleFactory::registerObject<SimpleCPU>("SimpleCPU");
    ModuleFactory::registerObject<SimpleCache>("SimpleCache");
    ModuleFactory::registerObject<SimpleMemoryController>("SimpleMemoryController");
    ModuleFactory::registerObject<ZeroLatencyArbiter>("ZeroLatencyArbiter"); // 新增
    ModuleFactory::registerObject<SimpleProducer>("SimpleProducer");          // 新增
    ModuleFactory::registerObject<SimpleConsumer>("SimpleConsumer");          // 新增

    // 读取配置
    std::ifstream config_file("system_config.json");
    nlohmann::json config = nlohmann::json::parse(config_file);

    ModuleFactory top_factory(nullptr);
    top_factory.instantiateAll(config);

    // 创建线程和事件队列
    std::vector<std::thread> threads;
    std::vector<std::unique_ptr<EventQueue>> event_queues;

    for (const auto& cd_config : config["clock_domains"]) {
        for (const auto& thread_config : cd_config["threads"]) {
            auto eq = std::make_unique<EventQueue>();
            event_queues.push_back(std::move(eq));
            EventQueue* thread_eq = event_queues.back().get();

            std::vector<SimObject*> thread_objects;
            for (const auto& obj_name : thread_config["objects"]) {
                SimObject* obj = top_factory.getInstance(obj_name.get<std::string>());
                if (obj) {
                    obj->bindEventQueue(thread_eq);
                    thread_objects.push_back(obj);
                }
            }

            threads.emplace_back(run_simulation_thread, thread_eq, thread_objects, 100);
        }
    }

    for (auto& t : threads) {
        if (t.joinable()) {
            t.join();
        }
    }

    // 打印统计信息
    printf("\n=== Simulation Statistics ===\n");
    for (const auto& [name, obj] : top_factory.getAllInstances()) {
        if (obj->hasPortManager()) {
            auto& pm = obj->getPortManager();
            printf("Module: %s\n", name.c_str());
            printf("  Upstream: %s\n", pm.getUpstreamStats().toString().c_str());
            printf("  Downstream: %s\n", pm.getDownstreamStats().toString().c_str());
        }
    }

    return 0;
}
```

## **3. 时序与组合逻辑模型详解**

GemSc 通过 `pre_tick` 和 `tick` 两个阶段，精确映射硬件设计中的组合逻辑与
