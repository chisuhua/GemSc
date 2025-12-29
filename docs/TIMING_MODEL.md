# GemSc 时序模型

GemSc 的核心设计哲学是精确模拟真实硬件的行为。在硬件设计中，逻辑电路分为**组合逻辑**（Combinational Logic）和**时序逻辑**（Sequential Logic）。GemSc 通过 `pre_tick` 和 `tick` 两个阶段，完美地映射了这两种逻辑，并定义了它们之间清晰的协作契约。

## 1. 组合逻辑 (Combinational Logic) 与 `pre_tick()`

**定义**：组合逻辑的输出**仅取决于当前的输入**，没有内部状态，变化是瞬时的（在理想模型中）。

**在 GemSc 中的体现**：
*   **函数**：`pre_tick()` 阶段。
*   **行为**：处理所有在**上一周期结束时**到达的输入数据包。这个处理过程是"瞬时"的，不消耗仿真周期。
*   **关键机制**：**迭代执行**。如果在处理一个数据包的过程中，产生了新的输出，并且该输出通过零延迟 (`latency=0`) 的连接发送给了同一时钟域的其他模块，那么这些新到达的包会被视为"新的输入"，触发 `pre_tick` 的下一轮执行。这个过程会持续进行，直到系统内没有新的零延迟事件产生，达到稳定状态。
*   **用户责任**：在 `pre_tick` 中，您**必须**调用 `port->tick()` 来处理端口队列中的数据包，否则输入将永远不会被消费。

**适用场景**：
*   多路选择器 (Multiplexer)
*   解码器 (Decoder)
*   仲裁器 (Arbiter)
*   组合逻辑的 ALU 运算
*   任何输入到输出没有寄存器的路径

### 例子：零延迟仲裁器 (`ZeroLatencyArbiter`)

下面是一个典型的组合逻辑模块——仲裁器。它接收来自多个上游模块的请求，并在一个周期内（实际上是 `pre_tick` 的一次或多次迭代内）完成仲裁并将请求转发给下游。

```cpp
class ZeroLatencyArbiter : public SimObject {
private:
    // 为每个输入源维护一个队列
    std::unordered_map<std::string, std::queue<Packet*>> input_queues;
    UpstreamPort* port0;
    UpstreamPort* port1;
    DownstreamPort* output_port;

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
```

## 2. 时序逻辑 (Sequential Logic) 与 `tick()`

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

### 例子：带状态的计数器模块 (`CounterModule`)

```cpp
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
```

在这个例子中，`should_increment` 是一个内部状态。它在 `pre_tick` 阶段被设置（组合逻辑采样输入），但真正的自增操作是在 `tick` 阶段完成的（时序逻辑更新状态）。这清晰地分离了"输入采样"和"状态更新"。

## 3. 时序逻辑与组合逻辑的协作

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

## 4. 同步机制

GemSc 使用多线程同步机制来处理跨时钟域的通信：

- 全局同步变量 `g_input_ready_flag`：标记是否有新的零延迟输入
- 互斥锁 `g_sync_mutex`：保护共享资源
- 条件变量 `g_sync_cv`：协调多线程间的同步
- 原子计数器 `g_active_threads`：跟踪活跃线程数

## 5. 事件队列

[EventQueue](file:///mnt/ubuntu/chisuhua/github/GemSc/include/core/event_queue.hh#L43-L53) 是 GemSc 的核心调度组件，负责推进仿真时间并调度模块的执行。

## 6. 延迟建模

GemSc 支持不同类型的延迟建模：

- **固定延迟**：通过连接配置中的 `latency` 参数指定
- **可变延迟**：在模块内部根据状态动态计算
- **零延迟**：用于组合逻辑路径，触发迭代执行