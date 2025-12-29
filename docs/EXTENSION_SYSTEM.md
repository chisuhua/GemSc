# GemSc 扩展系统

GemSc 扩展系统允许用户为数据包添加自定义信息，以支持更复杂的仿真需求，如一致性协议、性能监控、预取策略和QoS控制等。

## 1. 扩展系统概述

扩展系统基于 TLM (Transaction Level Modeling) 扩展机制，允许在 [Packet](file:///mnt/ubuntu/chisuhua/github/GemSc/include/packet.hh#L47-L95) 中附加额外的信息，而不需要修改核心数据包结构。

## 2. 基础扩展类型

GemSc 提供了几种常用的扩展类型：

### 2.1 一致性扩展 (`CoherenceExtension`)

用于实现缓存一致性协议，跟踪缓存行状态：

```cpp
class CoherenceExtension : public tlm::tlm_extension<CoherenceExtension> {
public:
    enum State { INVALID, SHARED, EXCLUSIVE, MODIFIED };
    
    State state = INVALID;
    uint64_t requester_id = 0;
    bool is_exclusive_request = false;
    
    CoherenceExtension() = default;
    
    tlm::tlm_extension_base* clone() const override {
        auto* ext = new CoherenceExtension();
        ext->state = this->state;
        ext->requester_id = this->requester_id;
        ext->is_exclusive_request = this->is_exclusive_request;
        return ext;
    }
    
    void copy_from(tlm::tlm_extension_base const &ext) override {
        const auto* src = dynamic_cast<const CoherenceExtension*>(&ext);
        if (src) {
            state = src->state;
            requester_id = src->requester_id;
            is_exclusive_request = src->is_exclusive_request;
        }
    }
};
```

### 2.2 性能扩展 (`PerformanceExtension`)

用于性能监控和分析：

```cpp
class PerformanceExtension : public tlm::tlm_extension<PerformanceExtension> {
public:
    uint64_t creation_cycle = 0;
    uint64_t processing_start_cycle = 0;
    std::vector<uint64_t> hop_cycles;  // 每跳的时间戳
    std::vector<std::string> visited_nodes;  // 访问的节点列表
    
    PerformanceExtension() {
        creation_cycle = event_queue->getCurrentCycle();
    }
    
    tlm::tlm_extension_base* clone() const override {
        auto* ext = new PerformanceExtension();
        ext->creation_cycle = this->creation_cycle;
        ext->processing_start_cycle = this->processing_start_cycle;
        ext->hop_cycles = this->hop_cycles;
        ext->visited_nodes = this->visited_nodes;
        return ext;
    }
    
    void copy_from(tlm::tlm_extension_base const &ext) override {
        const auto* src = dynamic_cast<const PerformanceExtension*>(&ext);
        if (src) {
            creation_cycle = src->creation_cycle;
            processing_start_cycle = src->processing_start_cycle;
            hop_cycles = src->hop_cycles;
            visited_nodes = src->visited_nodes;
        }
    }
};
```

### 2.3 预取扩展 (`PrefetchExtension`)

用于实现预取策略：

```cpp
class PrefetchExtension : public tlm::tlm_extension<PrefetchExtension> {
public:
    bool is_prefetch = false;
    uint64_t original_address = 0;
    uint8_t confidence = 0;  // 0-100
    uint8_t level = 0;       // 预取层级
    
    PrefetchExtension() = default;
    
    tlm::tlm_extension_base* clone() const override {
        auto* ext = new PrefetchExtension();
        ext->is_prefetch = this->is_prefetch;
        ext->original_address = this->original_address;
        ext->confidence = this->confidence;
        ext->level = this->level;
        return ext;
    }
    
    void copy_from(tlm::tlm_extension_base const &ext) override {
        const auto* src = dynamic_cast<const PrefetchExtension*>(&ext);
        if (src) {
            is_prefetch = src->is_prefetch;
            original_address = src->original_address;
            confidence = src->confidence;
            level = src->level;
        }
    }
};
```

### 2.4 QoS扩展 (`QoSServiceExtension`)

用于服务质量控制：

```cpp
class QoSServiceExtension : public tlm::tlm_extension<QoSServiceExtension> {
public:
    enum ServiceType { BEST_EFFORT, LOW_LATENCY, HIGH_THROUGHPUT, CRITICAL };
    
    ServiceType service_type = BEST_EFFORT;
    uint8_t priority = 0;  // 0-255
    uint64_t deadline = 0; // 截止周期
    bool is_critical = false;
    
    QoSServiceExtension() = default;
    
    tlm::tlm_extension_base* clone() const override {
        auto* ext = new QoSServiceExtension();
        ext->service_type = this->service_type;
        ext->priority = this->priority;
        ext->deadline = this->deadline;
        ext->is_critical = this->is_critical;
        return ext;
    }
    
    void copy_from(tlm::tlm_extension_base const &ext) override {
        const auto* src = dynamic_cast<const QoSServiceExtension*>(&ext);
        if (src) {
            service_type = src->service_type;
            priority = src->priority;
            deadline = src->deadline;
            is_critical = src->is_critical;
        }
    }
};
```

## 3. 扩展使用方法

### 3.1 添加扩展到数据包

```cpp
// 创建数据包
auto* payload = new tlm::tlm_generic_payload();
payload->set_address(0x1000);
auto* pkt = new Packet(payload, getCurrentCycle(), PKT_REQ_READ);

// 添加一致性扩展
auto* coherence_ext = new CoherenceExtension();
coherence_ext->state = CoherenceExtension::EXCLUSIVE;
coherence_ext->requester_id = 1;
pkt->payload->set_extension(coherence_ext);

// 添加性能扩展
auto* perf_ext = new PerformanceExtension();
pkt->payload->set_extension(perf_ext);
```

### 3.2 从数据包获取扩展

```cpp
bool MyModule::handleUpstreamRequest(Packet* pkt, int src_id, const std::string& src_label) {
    // 获取一致性扩展
    CoherenceExtension* coherence_ext = nullptr;
    pkt->payload->get_extension(coherence_ext);
    
    if (coherence_ext && coherence_ext->state == CoherenceExtension::EXCLUSIVE) {
        // 特殊处理独占状态的请求
        DPRINTF(MODULE, "[%s] Processing exclusive request\n", getName().c_str());
    }
    
    // 获取性能扩展
    PerformanceExtension* perf_ext = nullptr;
    pkt->payload->get_extension(perf_ext);
    
    if (perf_ext) {
        perf_ext->processing_start_cycle = getCurrentCycle();
        perf_ext->visited_nodes.push_back(getName());
    }
    
    // 处理请求...
    return true;
}
```

### 3.3 扩展传播

当数据包在模块间传递时，扩展会自动传播：

```cpp
bool MyModule::handleUpstreamRequest(Packet* pkt, int src_id, const std::string& src_label) {
    // 所有扩展都会自动传递到下一个模块
    // 可以修改扩展或添加新的扩展
    PerformanceExtension* perf_ext = nullptr;
    pkt->payload->get_extension(perf_ext);
    
    if (perf_ext) {
        perf_ext->hop_cycles.push_back(getCurrentCycle());
    }
    
    // 将数据包转发到下一个模块
    auto* out_port = getPortManager().getDownstreamPort("out_port");
    if (out_port) {
        return out_port->sendReq(pkt);
    }
    
    delete pkt;
    return false;
}
```

## 4. 自定义扩展

用户可以创建自定义扩展以满足特定需求：

```cpp
// 自定义安全扩展
class SecurityExtension : public tlm::tlm_extension<SecurityExtension> {
public:
    uint32_t security_level = 0;
    uint64_t encryption_key = 0;
    bool is_authenticated = false;
    
    SecurityExtension() = default;
    
    tlm::tlm_extension_base* clone() const override {
        auto* ext = new SecurityExtension();
        ext->security_level = this->security_level;
        ext->encryption_key = this->encryption_key;
        ext->is_authenticated = this->is_authenticated;
        return ext;
    }
    
    void copy_from(tlm::tlm_extension_base const &ext) override {
        const auto* src = dynamic_cast<const SecurityExtension*>(&ext);
        if (src) {
            security_level = src->security_level;
            encryption_key = src->encryption_key;
            is_authenticated = src->is_authenticated;
        }
    }
};
```

## 5. 扩展管理

### 5.1 扩展池

为提高性能，可以实现扩展池来管理扩展对象的生命周期：

```cpp
class ExtensionPool {
private:
    std::queue<std::unique_ptr<CoherenceExtension>> coherence_pool;
    std::mutex pool_mutex;

public:
    std::unique_ptr<CoherenceExtension> acquire() {
        std::lock_guard<std::mutex> lock(pool_mutex);
        if (!coherence_pool.empty()) {
            auto ext = std::move(coherence_pool.front());
            coherence_pool.pop();
            ext->reset();  // 重置为默认值
            return ext;
        }
        return std::make_unique<CoherenceExtension>();
    }
    
    void release(std::unique_ptr<CoherenceExtension> ext) {
        std::lock_guard<std::mutex> lock(pool_mutex);
        coherence_pool.push(std::move(ext));
    }
};
```

### 5.2 扩展验证

在关键系统中，可以实现扩展验证机制：

```cpp
bool validateExtensions(Packet* pkt) {
    CoherenceExtension* coherence_ext = nullptr;
    pkt->payload->get_extension(coherence_ext);
    
    if (coherence_ext) {
        // 验证一致性扩展的有效性
        if (coherence_ext->state < CoherenceExtension::INVALID || 
            coherence_ext->state > CoherenceExtension::MODIFIED) {
            DPRINTF(MODULE, "[%s] Invalid coherence state\n", getName().c_str());
            return false;
        }
    }
    
    return true;
}
```

通过扩展系统，GemSc 可以支持复杂的仿真场景，同时保持核心框架的简洁性。扩展可以轻松添加到现有系统中，而无需修改核心组件。