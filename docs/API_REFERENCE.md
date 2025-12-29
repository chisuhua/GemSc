# GemSc API 参考文档

本文档提供了 GemSc 框架的主要 API 接口参考。

## 1. 核心类

### 1.1 SimObject

[SimObject](file:///mnt/ubuntu/chisuhua/github/GemSc/include/sim_object.hh#L45-L88) 是所有仿真模块的基类。

#### 公共方法

```cpp
class SimObject {
public:
    // 构造函数
    SimObject(const std::string& name, EventQueue* eq);
    
    // 获取模块名称
    const std::string& getName() const;
    
    // 获取当前周期数
    uint64_t getCurrentCycle() const;
    
    // 绑定事件队列
    void bindEventQueue(EventQueue* eq);
    
    // 获取端口管理器
    PortManager& getPortManager();
    
    // 检查是否有端口管理器
    bool hasPortManager() const;
    
    // 仿真周期函数 - 组合逻辑阶段
    virtual void pre_tick();
    
    // 仿真周期函数 - 时序逻辑阶段
    virtual void tick();
    
    // 初始化阶段
    virtual void initiate_tick();
    
    // 处理上游请求
    virtual bool handleUpstreamRequest(Packet* pkt, int src_id, const std::string& src_label);
    
    // 处理下游响应
    virtual bool handleDownstreamResponse(Packet* pkt, int src_id, const std::string& src_label);
    
    // 处理上游响应
    virtual bool handleUpstreamResponse(Packet* pkt, int src_id, const std::string& src_label);
    
    // 处理下游请求
    virtual bool handleDownstreamRequest(Packet* pkt, int src_id, const std::string& src_label);
    
    virtual ~SimObject();
};
```

### 1.2 Packet

[Packet](file:///mnt/ubuntu/chisuhua/github/GemSc/include/packet.hh#L47-L95) 是模块间通信的数据单元。

#### 公共成员

```cpp
class Packet {
public:
    tlm::tlm_generic_payload* payload;  // TLM 有效载荷
    uint64_t src_cycle;                 // 源周期
    uint64_t dst_cycle;                 // 目标周期
    PacketType type;                    // 数据包类型
    Packet* original_req;               // 原始请求（用于响应）
    std::vector<Packet*> dependents;    // 依赖数据包
    uint64_t stream_id;                 // 流ID
    uint64_t seq_num;                   // 序列号
    int credits;                        // 信用值
    std::vector<std::string> route_path; // 路由路径
    int hop_count;                      // 跳数
    uint8_t priority;                   // 优先级
    uint64_t flow_id;                   // 流ID
    int vc_id;                          // 虚拟通道ID
    
    // 构造函数
    Packet(tlm::tlm_generic_payload* p, uint64_t cycle, PacketType t);
    
    // 检查数据包类型
    bool isRequest() const;
    bool isResponse() const;
    bool isStream() const;
    bool isCredit() const;
    
    // 获取延迟周期数
    uint64_t getDelayCycles() const;
    
    // 获取端到端周期数
    uint64_t getEnd2EndCycles() const;
};
```

### 1.3 EventQueue

[EventQueue](file:///mnt/ubuntu/chisuhua/github/GemSc/include/core/event_queue.hh#L43-L53) 管理仿真时间推进。

#### 公共方法

```cpp
class EventQueue {
public:
    // 获取当前周期
    uint64_t getCurrentCycle() const;
    
    // 推进一个周期
    void tick();
    
    // 推进指定数量的周期
    void tick(uint64_t cycles);
    
    // 重置队列
    void reset();
    
    virtual ~EventQueue();
};
```

### 1.4 ModuleFactory

[ModuleFactory](file:///mnt/ubuntu/chisuhua/github/GemSc/include/core/module_factory.hh#L52-L109) 负责根据配置创建模块实例。

#### 公共方法

```cpp
class ModuleFactory {
public:
    // 构造函数
    explicit ModuleFactory(EventQueue* eq);
    
    // 注册对象类型
    template<typename T>
    static void registerObject(const std::string& name);
    
    // 实例化所有模块
    void instantiateAll(const json& config);
    
    // 获取实例
    SimObject* getInstance(const std::string& name);
    
    // 获取所有实例
    const std::unordered_map<std::string, SimObject*>& getAllInstances() const;
    
    // 解析连接
    void parseConnections(const json& connections);
    
    virtual ~ModuleFactory();
};
```

## 2. 端口系统

### 2.1 SimplePort

[SimplePort](file:///mnt/ubuntu/chisuhua/github/GemSc/include/simple_port.hh#L44-L67) 是端口系统的基础类。

#### 公共方法

```cpp
class SimplePort {
public:
    // 发送数据包
    virtual bool send(Packet* pkt);
    
    // 接收数据包（纯虚函数）
    virtual bool recv(Packet* pkt) = 0;
    
    // 端口时钟（处理队列）
    virtual void tick();
    
    // 绑定事件队列
    void bindEventQueue(EventQueue* eq);
    
    virtual ~SimplePort();
};
```

### 2.2 MasterPort

[MasterPort](file:///mnt/ubuntu/chisuhua/github/GemSc/include/master_port.hh#L45-L88) 用于发起请求和接收响应。

#### 公共方法

```cpp
class MasterPort : public SimplePort {
public:
    // 构造函数
    explicit MasterPort(const std::string& name);
    
    // 获取名称
    const std::string& getName() const;
    
    // 发送请求
    bool sendReq(Packet* pkt);
    
    // 接收响应（纯虚函数）
    virtual bool recvResp(Packet* pkt) = 0;
    
    // 重写接收方法
    bool recv(Packet* pkt) override;
    
    // 获取统计信息
    const PortStats& getStats() const;
    
    // 重置统计信息
    void resetStats();
    
    virtual ~MasterPort();
};
```

### 2.3 SlavePort

[SlavePort](file:///mnt/ubuntu/chisuhua/github/GemSc/include/slave_port.hh#L45-L85) 用于接收请求和发送响应。

#### 公共方法

```cpp
class SlavePort : public SimplePort {
public:
    // 构造函数
    explicit SlavePort(const std::string& name);
    
    // 获取名称
    const std::string& getName() const;
    
    // 发送响应
    bool sendResp(Packet* pkt);
    
    // 接收请求（纯虚函数）
    virtual bool recvReq(Packet* pkt) = 0;
    
    // 获取统计信息
    const PortStats& getStats() const;
    
    // 重置统计信息
    void resetStats();
    
    virtual ~SlavePort();
};
```

### 2.4 PortManager

[PortManager](file:///mnt/ubuntu/chisuhua/github/GemSc/include/port_manager.hh#L48-L106) 管理模块的端口。

#### 公共方法

```cpp
class PortManager {
public:
    // 添加上游端口
    UpstreamPort* addUpstreamPort(SimObject* owner, 
                                  std::vector<int> vc_sizes, 
                                  std::vector<int> credit_amounts, 
                                  const std::string& name);
    
    // 添加下游端口
    DownstreamPort* addDownstreamPort(SimObject* owner, 
                                      std::vector<int> vc_sizes, 
                                      std::vector<int> credit_amounts, 
                                      const std::string& name);
    
    // 获取上游端口
    UpstreamPort* getUpstreamPort(const std::string& name);
    
    // 获取下游端口
    DownstreamPort* getDownstreamPort(const std::string& name);
    
    // 获取上游统计信息
    PortStats getUpstreamStats() const;
    
    // 获取下游统计信息
    PortStats getDownstreamStats() const;
    
    // 绑定事件队列
    void bindEventQueue(EventQueue* eq);
    
    virtual ~PortManager();
};
```

## 3. 数据包类型

### 3.1 PacketType 枚举

```cpp
enum PacketType {
    PKT_REQ_READ,       // 读请求
    PKT_REQ_WRITE,      // 写请求
    PKT_RESP,           // 响应
    PKT_STREAM_DATA,    // 流数据
    PKT_CREDIT_RETURN   // 信用返回
};
```

## 4. 调试宏

### 4.1 DPRINTF 宏

用于调试输出：

```cpp
DPRINTF(CATEGORY, "Format string with args: %d %s\n", arg1, arg2);
```

## 5. 工具类

### 5.1 PortStats

[PortStats](file:///mnt/ubuntu/chisuhua/github/GemSc/include/port_stats.hh#L41-L77) 存储端口统计信息。

#### 公共成员

```cpp
struct PortStats {
    uint64_t req_count;              // 请求计数
    uint64_t resp_count;             // 响应计数
    uint64_t byte_count;             // 字节计数
    uint64_t total_delay_cycles;     // 总延迟周期
    uint64_t min_delay_cycles;       // 最小延迟周期
    uint64_t max_delay_cycles;       // 最大延迟周期
    uint64_t credit_sent;            // 发送信用数
    uint64_t credit_received;        // 接收信用数
    uint64_t credit_value;           // 信用值
    
    // 重置统计
    void reset();
    
    // 合并统计
    void merge(const PortStats& other);
    
    // 转换为字符串
    std::string toString() const;
};
```