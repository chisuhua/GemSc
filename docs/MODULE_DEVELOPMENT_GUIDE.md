# GemSc 模块开发指南

本文档详细介绍了如何在 GemSc 框架中开发自定义模块。

## 1. 模块基础

在 GemSc 中，所有仿真模块都必须继承自 [SimObject](file:///mnt/ubuntu/chisuhua/github/GemSc/include/sim_object.hh#L45-L88) 基类。每个模块需要实现以下核心方法：

- `tick()` - 模块每周期执行的逻辑
- `pre_tick()` - 处理输入数据包的组合逻辑阶段
- 各种 `handle*` 方法 - 处理不同类型的数据包

## 2. 模块类型

### 2.1 Initiator 模块（发起者）
- 示例：[CPUSim](file:///mnt/ubuntu/chisuhua/github/GemSc/include/modules/cpu_sim.hh#L44-L65)、[TrafficGenerator](file:///mnt/ubuntu/chisuhua/github/GemSc/include/modules/traffic_gen.hh#L43-L66)
- 特点：不继承 [SimplePort](file:///mnt/ubuntu/chisuhua/github/GemSc/include/simple_port.hh#L44-L67)，但拥有输出端口
- 主要功能：发起请求，处理响应

```cpp
class MyInitiator : public SimObject {
private:
    MasterPort* out_port;  // 发起请求的端口

public:
    MyInitiator(const std::string& name, EventQueue* eq) 
        : SimObject(name, eq) {
        getPortManager().addDownstreamPort(this, {4}, {}, "out_port");
    }

    void pre_tick() override {
        // 处理响应数据包
        auto* port = getPortManager().getDownstreamPort("out_port");
        if (port) port->tick();
    }

    void tick() override {
        // 在此发起请求
        sendRequest();
    }

    bool handleDownstreamResponse(Packet* pkt, int src_id, const std::string& src_label) override {
        // 处理响应数据包
        delete pkt;
        return true;
    }
};
```

### 2.2 Target 模块（目标）
- 示例：[MemorySim](file:///mnt/ubuntu/chisuhua/github/GemSc/include/modules/memory_sim.hh#L42-L60)
- 特点：继承 [SimplePort](file:///mnt/ubuntu/chisuhua/github/GemSc/include/simple_port.hh#L44-L67)，但没有输出端口
- 主要功能：接收请求，返回响应

```cpp
class MyTarget : public SimObject {
public:
    MyTarget(const std::string& name, EventQueue* eq) 
        : SimObject(name, eq) {
        getPortManager().addUpstreamPort(this, {4}, {}, "in_port");
    }

    void pre_tick() override {
        // 处理请求数据包
        auto* port = getPortManager().getUpstreamPort("in_port");
        if (port) port->tick();
    }

    void tick() override {
        // 处理内部逻辑
    }

    bool handleUpstreamRequest(Packet* pkt, int src_id, const std::string& src_label) override {
        // 处理请求并生成响应
        auto* resp_pkt = createResponse(pkt);
        auto* port = getPortManager().getUpstreamPort(src_label);
        if (port) {
            port->sendResp(resp_pkt);
        } else {
            delete resp_pkt;
        }
        return true;
    }
};
```

### 2.3 Middle 模块（中间件）
- 示例：[CacheSim](file:///mnt/ubuntu/chisuhua/github/GemSc/include/modules/cache_sim.hh#L43-L75)
- 特点：继承 [SimplePort](file:///mnt/ubuntu/chisuhua/github/GemSc/include/simple_port.hh#L44-L67)，同时拥有输入和输出端口
- 主要功能：处理请求和响应

```cpp
class MyMiddle : public SimObject {
public:
    MyMiddle(const std::string& name, EventQueue* eq) 
        : SimObject(name, eq) {
        getPortManager().addUpstreamPort(this, {4}, {}, "upstream_port");
        getPortManager().addDownstreamPort(this, {4}, {}, "downstream_port");
    }

    void pre_tick() override {
        auto* up_port = getPortManager().getUpstreamPort("upstream_port");
        auto* down_port = getPortManager().getDownstreamPort("downstream_port");
        if (up_port) up_port->tick();
        if (down_port) down_port->tick();
    }

    void tick() override {
        // 处理内部逻辑
    }

    bool handleUpstreamRequest(Packet* pkt, int src_id, const std::string& src_label) override {
        // 处理来自上游的请求
        auto* down_port = getPortManager().getDownstreamPort("downstream_port");
        if (down_port && down_port->sendReq(pkt)) {
            return true;
        } else {
            delete pkt;
            return false;
        }
    }

    bool handleDownstreamResponse(Packet* pkt, int src_id, const std::string& src_label) override {
        // 处理来自下游的响应
        auto* up_port = getPortManager().getUpstreamPort("upstream_port");
        if (up_port) {
            up_port->sendResp(pkt);
        } else {
            delete pkt;
        }
        return true;
    }
};
```

## 3. 模块注册

创建模块后，需要在 [ModuleFactory](file:///mnt/ubuntu/chisuhua/github/GemSc/include/core/module_factory.hh#L52-L109) 中注册，以便通过 JSON 配置文件使用：

```cpp
// 在 modules.hh 中添加注册宏
#define REGISTER_OBJECT \
    ModuleFactory::registerObject<CPUSim>("CPUSim"); \
    ModuleFactory::registerObject<CacheSim>("CacheSim"); \
    ModuleFactory::registerObject<MemorySim>("MemorySim"); \
    // ... 其他模块 ...
    ModuleFactory::registerObject<MyInitiator>("MyInitiator"); \
    ModuleFactory::registerObject<MyTarget>("MyTarget"); \
    ModuleFactory::registerObject<MyMiddle>("MyMiddle");
```

## 4. 端口管理

GemSc 使用 [PortManager](file:///mnt/ubuntu/chisuhua/github/GemSc/include/port_manager.hh#L48-L106) 来管理模块的端口：

- `addUpstreamPort()` - 添加上游端口（接收请求）
- `addDownstreamPort()` - 添加下游端口（发送请求）
- `getUpstreamPort()` - 获取上游端口
- `getDownstreamPort()` - 获取下游端口

## 5. 时序模型

GemSc 使用两阶段时序模型：

- `pre_tick()` - 组合逻辑阶段，处理输入数据包
- `tick()` - 时序逻辑阶段，更新内部状态

在 `pre_tick()` 中必须调用端口的 `tick()` 方法来处理数据包。

## 6. 数据包处理

数据包类型：
- `PKT_REQ_READ` - 读请求
- `PKT_REQ_WRITE` - 写请求
- `PKT_RESP` - 响应
- `PKT_STREAM_DATA` - 流数据
- `PKT_CREDIT_RETURN` - 信用返回

## 7. 调试与日志

使用 `DPRINTF` 宏输出调试信息：

```cpp
DPRINTF(MY_MODULE, "Processing packet with address 0x%lx\n", pkt->payload->get_address());
```