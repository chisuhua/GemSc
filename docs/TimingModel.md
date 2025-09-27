好的，我们继续完善用户手册，深入解析时序逻辑与组合逻辑的协作机制，并提供对应的完整例子。

---

## **3. 时序与组合逻辑模型详解**

GemSc 的核心设计哲学是精确模拟真实硬件的行为。在硬件设计中，逻辑电路分为**组合逻辑**（Combinational Logic）和**时序逻辑**（Sequential Logic）。GemSc 通过 `pre_tick` 和 `tick` 两个阶段，完美地映射了这两种逻辑，并定义了它们之间清晰的协作契约。

### **3.1 组合逻辑 (Combinational Logic) 与 `pre_tick()`**

**定义**：组合逻辑的输出**仅取决于当前的输入**，没有内部状态，变化是瞬时的（在理想模型中）。

**在 GemSc 中的体现**：
*   **函数**：`pre_tick()` 阶段。
*   **行为**：处理所有在**上一周期结束时**到达的输入数据包。这个处理过程是“瞬时”的，不消耗仿真周期。
*   **关键机制**：**迭代执行**。如果在处理一个数据包的过程中，产生了新的输出，并且该输出通过零延迟 (`latency=0`) 的连接发送给了同一时钟域的其他模块，那么这些新到达的包会被视为“新的输入”，触发 `pre_tick` 的下一轮执行。这个过程会持续进行，直到系统内没有新的零延迟事件产生，达到稳定状态。
*   **用户责任**：在 `pre_tick` 中，您**必须**调用 `port->tick()` 来处理端口队列中的数据包，否则输入将永远不会被消费。

**适用场景**：
*   多路选择器 (Multiplexer)
*   解码器 (Decoder)
*   仲裁器 (Arbiter)
*   组合逻辑的 ALU 运算
*   任何输入到输出没有寄存器的路径

#### **例子：零延迟仲裁器 (`ZeroLatencyArbiter`)**

下面是一个典型的组合逻辑模块——仲裁器。它接收来自多个上游模块的请求，并在一个周期内（实际上是 `pre_tick` 的一次或多次迭代内）完成仲裁并将请求转发给下游。

```cpp
// zero_latency_arbiter.hh
#ifndef ZERO_LATENCY_ARBITER_HH
#define ZERO_LATENCY_ARBITER_HH

#include "sim_object.hh"
#include "packet.hh"
#include <queue>

class ZeroLatencyArbiter : public SimObject {
private:
    // 为每个输入源维护一个队列
    std::unordered_map<std::string, std::queue<Packet*>> input_queues;
    Upstreamport<ZeroLatencyArbiter>* port0;
    Upstreamport<ZeroLatencyArbiter>* port1;
    Upstreamport<ZeroLatencyArbiter>* output_port;

public:
    ZeroLatencyArbiter(const std::string& name, EventQueue* eq) : SimObject(name, eq) {
        // 创建两个上游输入端口
        port0 = getPortManager().getUpstreamPort("input_0");
        port1 = getPortManager().getUpstreamPort("input_1");
        // 创建一个下游输出端口
        output_port = getPortManager().getDownstreamPort("output");
        assert(output_port);
    }

    void pre_tick() override {
        // Step 1: 收集所有新到达的包，放入各自的队列

        if (port0) port0->tick(); // 这会调用 handleUpstreamRequest
        if (port1) port1->tick();
        
        // Step 2: 执行仲裁并转发
        arbitrateAndForward();
    }

    void tick() override {
        // 仲裁器是纯组合逻辑，tick 阶段无操作
    }

    bool handleUpstreamRequest(Packet* pkt, int src_id, const std::string& src_label) override {
        // 将收到的包放入对应源的队列
        input_queues[src_label].push(pkt);
        return true; // 包已被接收
    }

    bool handleDownstreamResponse(Packet* pkt, int src_id, const std::string& src_label) override {
        // 将响应原路返回给请求者
        for (auto& [label, queue] : input_queues) {
            if (!queue.empty() && queue.front()->original_req == pkt->original_req) {
                auto* return_port = getPortManager().getUpstreamPort(label);
                if (return_port) {
                    return_port->sendResp(pkt);
                    return true;
                }
            }
        }
        delete pkt;
        return true;
    }

private:
    void arbitrateAndForward() {
        // 简单的轮询仲裁：优先处理 input_0，然后是 input_1
        std::vector<std::string> ports = {"input_0", "input_1"};
        
        for (const auto& port_name : ports) {
            auto& queue = input_queues[port_name];
            while (!queue.empty()) {
                Packet* pkt = queue.front();
                if (output_port->sendReq(pkt)) {
                    // 发送成功，从队列中移除
                    queue.pop();
                    // 注意：sendReq 如果成功且延迟为0，会设置全局 g_input_ready_flag
                    // 这可能导致目标模块在本轮 pre_tick 的下一次迭代中被调用
                } else {
                    // 发送失败（下游拥塞），保留在队列中，下轮 pre_tick 再试
                    break;
                }
            }
        }
    }
};

// 注册
ModuleFactory::registerObject<ZeroLatencyArbiter>("ZeroLatencyArbiter");
```

**配套的生产者和消费者模块 (`simple_producer.hh` & `simple_consumer.hh`)**

为了让仲裁器例子完整，我们需要定义生产者和消费者。

```cpp
// simple_producer.hh
#ifndef SIMPLE_PRODUCER_HH
#define SIMPLE_PRODUCER_HH

#include "sim_object.hh"
#include "packet.hh"

class SimpleProducer : public SimObject {
private:
    int cycle_count = 0;
    std::string output_label;

public:
    SimpleProducer(const std::string& name, EventQueue* eq, const std::string& label = "output")
        : SimObject(name, eq), output_label(label) {
        // create in ModuleFactory after parsingn json
        //getPortManager().addDownstreamPort(this, {4}, {}, output_label);
    }

    void pre_tick() override {
        auto* port = getPortManager().getDownstreamPort(output_label);
        if (port) port->tick(); // 处理响应
    }

    void tick() override {
        cycle_count++;
        if (cycle_count % 5 == 0) { // 每5个周期发一个包
            sendPacket();
        }
        // 尝试重发滞留的包
        auto* port = getPortManager().getDownstreamPort(output_label);
        if (port) port->tick();
    }

    bool handleDownstreamResponse(Packet* pkt, int src_id, const std::string& src_label) override {
        DPRINTF(PRODUCER, "[%s] Received response.\n", getName().c_str());
        delete pkt;
        return true;
    }

private:
    void sendPacket() {
        auto* payload = new tlm::tlm_generic_payload();
        payload->set_address(0xDEADBEEF);
        payload->set_data_length(64);
        payload->set_command(tlm::TLM_READ_COMMAND);

        auto* pkt = new Packet(payload, getCurrentCycle(), PKT_REQ);
        pkt->cmd = CMD_READ;
        pkt->vc_id = 0;

        auto* port = getPortManager().getDownstreamPort(output_label);
        if (port && port->sendReq(pkt)) {
            DPRINTF(PRODUCER, "[%s] Sent packet.\n", getName().c_str());
        } else {
            delete pkt;
        }
    }
};

// simple_consumer.hh
#ifndef SIMPLE_CONSUMER_HH
#define SIMPLE_CONSUMER_HH

#include "sim_object.hh"
#include "packet.hh"

class SimpleConsumer : public SimObject {
private:
    std::string input_label;

public:
    SimpleConsumer(const std::string& name, EventQueue* eq, const std::string& label = "input")
        : SimObject(name, eq), input_label(label) {
        // create in ModuleFactory after parsingn json
        //getPortManager().addUpstreamPort(this, {4}, {}, input_label);
    }

    void pre_tick() override {
        auto* port = getPortManager().getUpstreamPort(input_label);
        if (port) port->tick(); // 处理请求
    }

    void tick() override {
        // 无操作
    }

    bool handleUpstreamRequest(Packet* pkt, int src_id, const std::string& src_label) override {
        DPRINTF(CONSUMER, "[%s] Consumed packet for addr 0x%lx\n", getName().c_str(), pkt->payload->get_address());
        // 生成一个响应包
        auto* resp_payload = new tlm::tlm_generic_payload();
        resp_payload->set_response_status(tlm::TLM_OK_RESPONSE);
        auto* resp_pkt = new Packet(resp_payload, getCurrentCycle(), PKT_RESP);
        resp_pkt->original_req = pkt;

        auto* port = getPortManager().getUpstreamPort(src_label);
        if (port) {
            port->sendResp(resp_pkt);
        } else {
            delete resp_pkt;
        }
        return true;
    }
};

#endif // SIMPLE_CONSUMER_HH
```

**配置文件 (`arbiter_system.json`)**:

```json
{
  "plugin": [],
  "clock_domains": [
    {
      "name": "fast_domain",
      "frequency": 2000,
      "threads": [
        {
          "name": "arbiter_thread",
          "objects": [
            "producer_a",
            "producer_b",
            "arbiter",
            "consumer"
          ]
        }
      ]
    }
  ],
  "modules": [
    {
      "name": "producer_a",
      "type": "SimpleProducer"
    },
    {
      "name": "producer_b",
      "type": "SimpleProducer"
    },
    {
      "name": "arbiter",
      "type": "ZeroLatencyArbiter"
    },
    {
      "name": "consumer",
      "type": "SimpleConsumer"
    }
  ],
  "connections": [
    {
      "src": "producer_a.output",
      "dst": "arbiter.input_0",
      "latency": 0
    },
    {
      "src": "producer_b.output",
      "dst": "arbiter.input_1",
      "latency": 0
    },
    {
      "src": "arbiter.output",
      "dst": "consumer.input",
      "latency": 0
    }
  ]
}
```

**仿真时序分析 (周期 T)**:

1.  **Producer A (`tick`)**: 生成包 `Pkt_A`，调用 `sendReq(Pkt_A)`。
2.  **Producer B (`tick`)**: 生成包 `Pkt_B`，调用 `sendReq(Pkt_B)`。
3.  **周期 T 的 `pre_tick` 第1轮**:
    *   `Arbiter::pre_tick()` 被调用。
    *   `input_0.tick()` -> `handleUpstreamRequest(Pkt_A)` -> `Pkt_A` 被放入 `input_0` 队列。
    *   `input_1.tick()` -> `handleUpstreamRequest(Pkt_B)` -> `Pkt_B` 被放入 `input_1` 队列。
    *   `arbitrateAndForward()` 执行：从 `input_0` 取出 `Pkt_A`，调用 `output_port->sendReq(Pkt_A)`。
    *   由于连接延迟为0，`sendReq` 成功，并**设置全局 `g_input_ready_flag = true`**。
    *   `Pkt_A` 被放入 `Consumer` 的输入队列。
4.  **周期 T 的 `pre_tick` 第2轮** (因为标志为真):
    *   `Consumer::pre_tick()` 被调用，处理 `Pkt_A`，打印日志。
    *   `Arbiter::pre_tick()` **再次**被调用。
    *   `arbitrateAndForward()` 再次执行：从 `input_1` 取出 `Pkt_B`，调用 `sendReq(Pkt_B)`。
    *   `sendReq` 成功，再次设置 `g_input_ready_flag = true`。
    *   `Pkt_B` 被放入 `Consumer` 的输入队列。
5.  **周期 T 的 `pre_tick` 第3轮**:
    *   `Consumer::pre_tick()` 被调用，处理 `Pkt_B`。
    *   `Arbiter::pre_tick()` 被调用，但队列已空。
    *   本轮迭代结束，`g_input_ready_flag` 保持为 `false`。
6.  **周期 T 的 `tick` 阶段**:
    *   所有模块的 `tick()` 被调用。生产者可能生成新包，仲裁器和消费者在 `tick` 阶段均无操作。

通过这个例子，您可以看到，尽管生产者是在 `tick` 阶段“同时”发出了两个请求，但仲裁器通过 `pre_tick` 的迭代机制，在**同一个仿真周期内**就完成了对两个请求的接收、仲裁和转发，完美模拟了组合逻辑的行为。

---

### **3.2 时序逻辑 (Sequential Logic) 与 `tick()`**

**定义**：时序逻辑的输出不仅取决于当前输入，还取决于**内部状态**。状态的变化发生在时钟边沿，变化不是瞬时的。

**在 GemSc 中的体现**：
*   **函数**：`tick()` 阶段。
*   **行为**：执行模块的核心状态机更新、发起新的事务、或根据 `pre_tick` 阶段处理的结果做出决策。`tick` 中的操作结果（如发送的数据包）通常会在**下一个或之后的周期**产生影响。
*   **关键机制**：**非阻塞赋值**。在 `tick` 中发送的数据包，即使延迟为0，其引发的 `pre_tick` 处理也会推迟到**下一个仿真周期**。这模拟了寄存器在时钟上升沿采样数据的行为。
*   **用户责任**：在 `tick` 中，您**应该**调用 `output_port->tick()` 来尝试发送在之前周期中因拥塞而滞留的数据包。

**适用场景**：
*   寄存器 (Register)
*   计数器 (Counter)
*   状态机 (FSM)
*   缓存控制器 (Cache Controller)
*   CPU 核心的取指、译码、执行阶段

#### **例子：带状态的计数器模块 (`CounterModule`)**

```cpp
// counter_module.hh
#ifndef COUNTER_MODULE_HH
#define COUNTER_MODULE_HH

#include "sim_object.hh"
#include "packet.hh"

class CounterModule : public SimObject {
private:
    int counter_value = 0;
    bool should_increment = false; // 由 pre_tick 设置的状态

public:
    CounterModule(const std::string& name, EventQueue* eq) : SimObject(name, eq) {
        getPortManager().addUpstreamPort(this, {1}, {}, "trigger");
    }

    void pre_tick() override {
        auto* port = getPortManager().getUpstreamPort("trigger");
        if (port) {
            port->tick(); // 处理输入，可能会设置 should_increment
        }
    }

    void tick() override {
        // 根据 pre_tick 阶段设置的状态，更新内部寄存器
        if (should_increment) {
            counter_value++;
            DPRINTF(COUNTER, "[%s] Counter incremented to: %d\n", getName().c_str(), counter_value);
            should_increment = false; // 重置状态
        }
    }

    bool handleUpstreamRequest(Packet* pkt, int src_id, const std::string& src_label) override {
        // 收到任何包都触发计数器自增
        should_increment = true; // 设置状态，但不立即执行
        delete pkt;
        return true;
    }
};

// 注册
ModuleFactory::registerObject<CounterModule>("CounterModule");
```

在这个例子中，`should_increment` 是一个内部状态。它在 `pre_tick` 阶段被设置（组合逻辑采样输入），但真正的自增操作是在 `tick` 阶段完成的（时序逻辑更新状态）。这清晰地分离了“输入采样”和“状态更新”。

---

### **3.3 时序逻辑与组合逻辑的协作**

在真实的硬件系统中，时序逻辑和组合逻辑是紧密协作的。GemSc 的 `pre_tick` -> `tick` 模型完美地模拟了这种协作：

1.  **`tick` 阶段 (时序逻辑)**: 模块根据其内部状态，决定发起新的操作。例如，CPU 在 `tick` 中决定发起一次内存读取。
2.  **跨周期传输**: CPU 在 `tick` 中发送的请求包，经过非零延迟的连线，在未来的某个周期到达目标模块（如 Cache）。
3.  **`pre_tick` 阶段 (组合逻辑)**: 在目标周期开始时，Cache 的 `pre_tick` 被调用，处理这个新到达的请求。它可能是一个纯组合逻辑的查找（命中/未命中），也可能是触发一个更复杂的、涉及状态更新的处理流程。
4.  **状态更新**: 如果 Cache 未命中，它可能在 `pre_tick` 中设置一个内部标志（如 `pending_miss = true`），然后在紧接着的 `tick` 阶段，根据这个标志向内存控制器发起新的请求。

**协作流程图解**:

```
周期 N:
  [CPU Tick] --(sendReq, latency=2)--> (数据包在传输中)

周期 N+1:
  [CPU Tick] --(sendReq, latency=2)--> (数据包在传输中)
  [Cache Pre-Tick] --> 无新输入

周期 N+2:
  [CPU Tick] --(sendReq, latency=2)--> (数据包在传输中)
  [Cache Pre-Tick] --> 处理来自CPU的请求 (组合逻辑: 命中/未命中)
                     --> 如果未命中, 设置 pending_miss = true
  [Cache Tick]     --> 检查 pending_miss, 如果为真, 向内存发送请求 (时序逻辑)
```

这种设计使得 GemSc 不仅能模拟简单的流水线，还能模拟复杂的、带有反馈和状态依赖的硬件行为，为构建高精度的性能模型提供了坚实的基础。

---

## **4. 完整例子: CPU -> Cache -> Memory 三级流水**

本例展示了如何配置一个简单的三级存储层次结构，综合运用了时序逻辑和组合逻辑。

### **4.1 仿真输出示例**

运行 `system_config.json` 配置和代码，您可能会看到如下输出（启用了 `DEBUG_PRINT`）：

```
[CPU] [cpu_core_0] Sent read request to 0x1000
[CACHE] [l1_cache_0] Handling CPU request for addr 0x1000
[CACHE] [l1_cache_0] Cache Miss, forwarding to memory
[MEM] [memory_controller_0] Received request for addr 0x1000, simulating memory access delay
[CACHE] [l1_cache_0] Handling memory response for addr 0x1000
[CPU] [cpu_core_0] Received response for addr 0x1000
[CPU] [cpu_core_0] Sent read request to 0x1040
[CACHE] [l1_cache_0] Handling CPU request for addr 0x1040
[CACHE] [l1_cache_0] Cache Hit!
[CPU] [cpu_core_0] Received response for addr 0x1040
...
```

### **4.2 性能统计**

仿真结束后，程序会打印每个模块端口的统计信息，例如：

```
=== Simulation Statistics ===
Module: cpu_core_0
  Upstream: req=0 resp=34 bytes=2176B delay(avg=0.0 min=0 max=0)  credits(sent=0 recv=0 value=0 avg=0.0)
  Downstream: req=34 resp=0 bytes=2176B delay(avg=0.0 min=18446744073709551615 max=0)  credits(sent=0 recv=0 value=0 avg=0.0)
Module: l1_cache_0
  Upstream: req=34 resp=34 bytes=4352B delay(avg=10.0 min=10 max=10)  credits(sent=0 recv=0 value=0 avg=0.0)
  Downstream: req=17 resp=17 bytes=1088B delay(avg=10.0 min=10 max=10)  credits(sent=0 recv=0 value=0 avg=0.0)
Module: memory_controller_0
  Upstream: req=17 resp=17 bytes=2176B delay(avg=0.0 min=0 max=0)  credits(sent=0 recv=0 value=0 avg=0.0)
  Downstream: req=0 resp=0 bytes=0B delay(avg=0.0 min=18446744073709551615 max=0)  credits(sent=0 recv=0 value=0 avg=0.0)
```

这份统计清晰地显示了：
*   CPU 发起了 34 次请求，收到了 34 次响应。
*   Cache 收到了 34 次 CPU 请求，其中 17 次命中（未转发），17 次未命中（转发给内存）。
*   内存控制器处理了 17 次请求。
*   从 Cache 到 Memory 的平均延迟是 10 个周期（由 JSON 配置中的 `latency` 决定）。

通过这份手册和例子，您现在可以熟练地使用 GemSc 框架，构建从简单的组合逻辑电路到复杂的多级流水线系统的各种硬件仿真模型。
