# GemSc 统计系统

GemSc 提供了一个强大的统计系统，用于收集和分析仿真过程中的各种性能指标，包括数据包计数、延迟统计、带宽使用情况和信用流控信息。

## 1. 统计结构

统计信息通过 [PortStats](file:///mnt/ubuntu/chisuhua/github/GemSc/include/port_stats.hh#L41-L77) 结构进行组织：

```cpp
struct PortStats {
    // 请求/响应
    uint64_t req_count = 0;      // 请求包数量
    uint64_t resp_count = 0;     // 响应包数量
    uint64_t byte_count = 0;     // 传输的字节数

    // 延迟统计
    uint64_t total_delay_cycles = 0;  // 总延迟周期数
    uint64_t min_delay_cycles = UINT64_MAX;  // 最小延迟周期数
    uint64_t max_delay_cycles = 0;   // 最大延迟周期数

    // 信用流控专用
    uint64_t credit_sent = 0;     // 发出的信用包数
    uint64_t credit_received = 0; // 接收的信用包数
    uint64_t credit_value = 0;    // 实际传递的信用额度
};
```

## 2. 数据包时间戳

为了支持延迟统计，[Packet](file:///mnt/ubuntu/chisuhua/github/GemSc/include/packet.hh#L47-L95) 类包含时间戳信息：

```cpp
class Packet {
public:
    uint64_t src_cycle;           // 包生成时间（cycle）
    uint64_t dst_cycle;           // 接收时间（用于延迟统计）
    
    // 辅助函数
    uint64_t getDelayCycles() const {
        return dst_cycle > src_cycle ? dst_cycle - src_cycle : 0;
    }
    
    uint64_t getEnd2EndCycles() const {
        return original_req ? dst_cycle - original_req->src_cycle : getDelayCycles();
    }
};
```

## 3. 统计更新

### 3.1 SlavePort 统计更新

[SlavePort](file:///mnt/ubuntu/chisuhua/github/GemSc/include/slave_port.hh#L45-L85) 负责处理请求包，其统计更新方法如下：

```cpp
void SlavePort::updateStats(Packet* pkt) {
    if (pkt->isRequest()) {
        stats.req_count++;
        stats.byte_count += pkt->payload ? pkt->payload->get_data_length() : 0;
    } else if (pkt->isStream()) {
        stats.req_count++;
    }
}

bool SlavePort::recv(Packet* pkt) {
    updateStats(pkt);
    pkt->dst_cycle = event_queue->getCurrentCycle();  // 记录接收时间

    // 更新延迟
    if (pkt->original_req && !pkt->isCredit()) {
        uint64_t delay = pkt->getDelayCycles();
        stats.total_delay_cycles += delay;
        if (delay < stats.min_delay_cycles) stats.min_delay_cycles = delay;
        if (delay > stats.max_delay_cycles) stats.max_delay_cycles = delay;
    }

    return recvReq(pkt);
}
```

### 3.2 MasterPort 统计更新

[MasterPort](file:///mnt/ubuntu/chisuhua/github/GemSc/include/master_port.hh#L45-L88) 负责处理响应包，其统计更新方法如下：

```cpp
void MasterPort::updateStats(Packet* pkt) {
    if (pkt->isResponse()) {
        stats.resp_count++;
        if (pkt->payload) {
            stats.byte_count += pkt->payload->get_data_length();
        }
        // 记录响应延迟
        if (pkt->original_req) {
            uint64_t delay = pkt->getDelayCycles();
            stats.total_delay_cycles += delay;
            if (delay < stats.min_delay_cycles) stats.min_delay_cycles = delay;
            if (delay > stats.max_delay_cycles) stats.max_delay_cycles = delay;
        }
    } else if (pkt->isCredit()) {
        stats.credit_received++;
        stats.credit_value += pkt->credits;
    }
}

bool MasterPort::recv(Packet* pkt) {
    pkt->dst_cycle = event_queue->getCurrentCycle();
    updateStats(pkt);
    return recvResp(pkt);
}

bool MasterPort::send(Packet* pkt) {
    if (pkt->isRequest()) {
        stats.req_count++;
        if (pkt->payload) {
            stats.byte_count += pkt->payload->get_data_length();
        }
    } else if (pkt->isCredit()) {
        stats.credit_sent++;
    }
    return SimplePort::send(pkt);
}
```

## 4. 统计输出格式

统计信息以格式化的字符串输出，包含以下信息：

```
req=20 resp=20 bytes=80B delay(avg=105.0 cyc, min=100, max=110) credits(sent=5 recv=3 value=20 avg=6.7)
```

输出包含：
- `req` - 请求包数量
- `resp` - 响应包数量
- `bytes` - 传输的字节数（自动转换为B/KB/MB单位）
- `delay` - 延迟统计（平均值、最小值、最大值）
- `credits` - 信用流控统计信息

## 5. 统计收集与输出

在仿真结束后，可以通过以下代码收集和输出统计信息：

```cpp
// 仿真结束后
for (auto& [name, obj] : instances) {
    if (obj->hasPortManager()) {
        auto& pm = obj->getPortManager();
        printf("[%s] Upstream: %s\n", name.c_str(), pm.getUpstreamStats().toString().c_str());
        printf("[%s] Downstream: %s\n", name.c_str(), pm.getDownstreamStats().toString().c_str());
    }
}
```

## 6. 统计合并

[PortStats](file:///mnt/ubuntu/chisuhua/github/GemSc/include/port_stats.hh#L41-L77) 支持合并操作，可以将多个统计信息合并到一起：

```cpp
void PortStats::merge(const PortStats& other) {
    req_count += other.req_count;
    resp_count += other.resp_count;
    byte_count += other.byte_count;
    total_delay_cycles += other.total_delay_cycles;
    credit_sent += other.credit_sent;
    credit_received += other.credit_received;
    credit_value += other.credit_value;

    if (other.min_delay_cycles < min_delay_cycles) {
        min_delay_cycles = other.min_delay_cycles;
    }
    if (other.max_delay_cycles > max_delay_cycles) {
        max_delay_cycles = other.max_delay_cycles;
    }
}
```

## 7. 统计重置

可以重置统计信息以进行分段统计：

```cpp
void PortStats::reset() {
    req_count = resp_count = byte_count = 0;
    total_delay_cycles = 0;
    min_delay_cycles = UINT64_MAX;
    max_delay_cycles = 0;
    credit_sent = credit_received = credit_value = 0;
}
```

## 8. 扩展统计

用户可以根据需要扩展统计系统：

- 添加自定义统计字段
- 实现特定模块的统计逻辑
- 创建统计报告生成工具
- 集成可视化工具

通过这个统计系统，用户可以深入分析系统的性能特征，包括带宽利用率、延迟分布、拥塞情况等关键指标。