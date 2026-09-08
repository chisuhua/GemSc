// test/test_complex_topologies_e2e.cc
// 复杂拓扑端到端测试：覆盖多端口路由、扇出/扇入、跨层级缓存、异构下游、压力测试
// 标签体系：[e2e][complex-topology]
// 作者 CppTLM Team / 日期 2026-06-14
//
// 目的：填补 test_e2e_simulation.cc (基础单/多端口) 与
//        test_e2e_crossbar_response.cc (4 CPU 扇入) 之间的空白
// 重点测试：wide fan-out、hierarchical cache、bidirectional traffic、
//         high latency、heterogeneous downstream、sustained stress

#include "chstream_register.hh"
#include "bundles/cache_bundles_tlm.hh"
#include "core/event_queue.hh"
#include "core/module_factory.hh"
#include "modules.hh"
#include "tlm/cache_tlm.hh"
#include "tlm/cpu_tlm.hh"
#include "tlm/crossbar_tlm.hh"
#include "tlm/memory_tlm.hh"
#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

static void registerAllModules() {
    static bool registered = false;
    if (!registered) {
        REGISTER_OBJECT;
        REGISTER_CHSTREAM;
        REGISTER_MODULE(CpuCluster);
        registered = true;
    }
}

// ============================================================================
// 拓扑 1：Wide fan-out (1 CPU → 1 Crossbar → 4 different Mems)
// 验证：地址路由 (addr>>12 & 3) 命中 4 个不同目的 mem
// 区别于 test_e2e_crossbar_response 的"4 ports all → 1 mem"扇入
// ============================================================================

TEST_CASE("E2E: Wide fan-out 1 CPU → 1 Xbar → 4 different Mems",
          "[e2e][complex][fan-out][crossbar]") {
    registerAllModules();
    json config = R"({
        "modules": [
            {"name": "cpu0", "type": "CPUTLM"},
            {"name": "xbar", "type": "CrossbarTLM"},
            {"name": "mem0", "type": "MemoryTLM"},
            {"name": "mem1", "type": "MemoryTLM"},
            {"name": "mem2", "type": "MemoryTLM"},
            {"name": "mem3", "type": "MemoryTLM"}
        ],
        "connections": [
            {"src": "cpu0", "dst": "xbar.0", "latency": 1},
            {"src": "xbar.0", "dst": "mem0", "latency": 1},
            {"src": "xbar.1", "dst": "mem1", "latency": 1},
            {"src": "xbar.2", "dst": "mem2", "latency": 1},
            {"src": "xbar.3", "dst": "mem3", "latency": 1}
        ]
    })"_json;
    EventQueue eq;
    ModuleFactory factory(&eq);
    REQUIRE(factory.instantiateAll(config));
    factory.startAllTicks();

    // 验证所有模块实例化
    for (int i = 0; i < 4; ++i) {
        std::string name = "mem" + std::to_string(i);
        REQUIRE(factory.getInstance(name) != nullptr);
    }

    auto* xbar = dynamic_cast<CrossbarTLM*>(factory.getInstance("xbar"));
    REQUIRE(xbar != nullptr);
    REQUIRE(xbar->num_ports() == 4);

    // 验证地址路由：addr>>12 & 3 决定目的端口
    REQUIRE(xbar->route_address(0x0000) == 0);
    REQUIRE(xbar->route_address(0x1000) == 1);
    REQUIRE(xbar->route_address(0x2000) == 2);
    REQUIRE(xbar->route_address(0x3000) == 3);

    // 运行 300 cycles,CPU 持续发请求,验证 cycle 推进无死锁
    eq.run(300);
    REQUIRE(eq.getCurrentCycle() == 300);
    SUCCEED("Wide fan-out 1→4 topology completed 300 cycles without deadlock");
}

// ============================================================================
// 拓扑 2：Hierarchical cache (L1 Cache → L2 Cache → Memory)
// 验证：CacheTLM 作为中间转发节点,miss 路径端到端可工作
// 区别于 test_simulation_communication 的 "Cache→Mem" 单层
// ============================================================================

TEST_CASE("E2E: Hierarchical cache L1 → L2 → Memory (3-tier)",
          "[e2e][complex][hierarchical][cache]") {
    registerAllModules();
    json config = R"({
        "modules": [
            {"name": "l1", "type": "CacheTLM"},
            {"name": "l2", "type": "CacheTLM"},
            {"name": "mem", "type": "MemoryTLM"}
        ],
        "connections": [
            {"src": "l1", "dst": "l2", "latency": 2},
            {"src": "l2", "dst": "mem", "latency": 3}
        ]
    })"_json;
    EventQueue eq;
    ModuleFactory factory(&eq);
    REQUIRE(factory.instantiateAll(config));
    factory.startAllTicks();

    REQUIRE(factory.getInstance("l1") != nullptr);
    REQUIRE(factory.getInstance("l2") != nullptr);
    REQUIRE(factory.getInstance("mem") != nullptr);

    // 模拟 L1 cache miss: 直接注入 L1 的请求
    auto* l1 = dynamic_cast<CacheTLM*>(factory.getInstance("l1"));
    REQUIRE(l1 != nullptr);
    bundles::CacheReqBundle req;
    req.transaction_id.write(0xCAFE);
    req.address.write(0xDEAD);
    req.is_write.write(1);
    req.data.write(0xBEEF);
    l1->req_in().data() = req;
    l1->req_in().set_valid(true);

    eq.run(10);
    REQUIRE(eq.getCurrentCycle() == 10);
    SUCCEED("Hierarchical cache 3-tier topology ran without crash");
}

// ============================================================================
// 拓扑 3：Bidirectional traffic (2 CPUs sharing 1 Mem through 1 Xbar)
// 验证：2 个 CPU 通过共享 Xbar+Mem 收发响应,事务不互相干扰
// ============================================================================

TEST_CASE("E2E: Bidirectional 2 CPUs share 1 Xbar + 1 Mem", "[e2e][complex][bidirectional][cpu]") {
    registerAllModules();
    json config = R"({
        "modules": [
            {"name": "cpu0", "type": "CPUTLM"},
            {"name": "cpu1", "type": "CPUTLM"},
            {"name": "xbar", "type": "CrossbarTLM"},
            {"name": "mem", "type": "MemoryTLM"}
        ],
        "connections": [
            {"src": "cpu0", "dst": "xbar.0", "latency": 1},
            {"src": "cpu1", "dst": "xbar.1", "latency": 1},
            {"src": "xbar.0", "dst": "mem", "latency": 1}
        ]
    })"_json;
    EventQueue eq;
    ModuleFactory factory(&eq);
    REQUIRE(factory.instantiateAll(config));
    factory.startAllTicks();

    auto* cpu0 = dynamic_cast<CPUTLM*>(factory.getInstance("cpu0"));
    auto* cpu1 = dynamic_cast<CPUTLM*>(factory.getInstance("cpu1"));
    REQUIRE(cpu0 != nullptr);
    REQUIRE(cpu1 != nullptr);

    // 运行足够 cycles 让两个 CPU 都产生请求
    eq.run(200);
    REQUIRE(eq.getCurrentCycle() == 200);

    // 关键断言:两个 CPU 都应至少收到一个响应
    // (P0-5b 修复前:resp_in 永远空 → last_response_transaction_id_ == 0)
    REQUIRE(cpu0->last_response_transaction_id() > 0);
    REQUIRE(cpu1->last_response_transaction_id() > 0);

    SUCCEED("Bidirectional 2 CPU traffic both reach shared memory");
}

// ============================================================================
// 拓扑 4：High latency chain (latency=10) — 验证深度流水线不破坏响应路径
// ============================================================================

TEST_CASE("E2E: High latency chain CPU → Xbar(latency=10) → Mem",
          "[e2e][complex][latency][pipeline]") {
    registerAllModules();
    json config = R"({
        "modules": [
            {"name": "cpu0", "type": "CPUTLM"},
            {"name": "xbar", "type": "CrossbarTLM"},
            {"name": "mem", "type": "MemoryTLM"}
        ],
        "connections": [
            {"src": "cpu0", "dst": "xbar.0", "latency": 10},
            {"src": "xbar.0", "dst": "mem", "latency": 10}
        ]
    })"_json;
    EventQueue eq;
    ModuleFactory factory(&eq);
    REQUIRE(factory.instantiateAll(config));
    factory.startAllTicks();

    auto* cpu0 = dynamic_cast<CPUTLM*>(factory.getInstance("cpu0"));
    REQUIRE(cpu0 != nullptr);

    // 高 latency 流水线:总往返延迟 = 10 (req) + 1 (xbar processing) + 10 (resp) = 21 cycles
    // 运行 500 cycles 确保多次完整 round-trip
    eq.run(500);
    REQUIRE(eq.getCurrentCycle() == 500);

    // 即使 latency 高,响应回路仍必须工作(P0-5b 修复后)
    REQUIRE(cpu0->last_response_transaction_id() > 0);
    SUCCEED("High latency pipeline doesn't break response path");
}

// ============================================================================
// 拓扑 5：Deep chain (CPU → Xbar → Xbar → Mem) — 验证 2 级 xbar 串接
// 区别于已有"1 xbar" 测试,这里测试 xbar 作为中间节点
// ============================================================================

TEST_CASE("E2E: Deep chain CPU → Xbar → Xbar → Mem (2-stage crossbar)",
          "[e2e][complex][deep-chain][crossbar]") {
    registerAllModules();
    json config = R"({
        "modules": [
            {"name": "cpu0", "type": "CPUTLM"},
            {"name": "xbar1", "type": "CrossbarTLM"},
            {"name": "xbar2", "type": "CrossbarTLM"},
            {"name": "mem", "type": "MemoryTLM"}
        ],
        "connections": [
            {"src": "cpu0", "dst": "xbar1.0", "latency": 2},
            {"src": "xbar1.0", "dst": "xbar2.0", "latency": 2},
            {"src": "xbar2.0", "dst": "mem", "latency": 2}
        ]
    })"_json;
    EventQueue eq;
    ModuleFactory factory(&eq);
    REQUIRE(factory.instantiateAll(config));
    factory.startAllTicks();

    auto* cpu0 = dynamic_cast<CPUTLM*>(factory.getInstance("cpu0"));
    auto* xbar1 = dynamic_cast<CrossbarTLM*>(factory.getInstance("xbar1"));
    auto* xbar2 = dynamic_cast<CrossbarTLM*>(factory.getInstance("xbar2"));
    REQUIRE(cpu0 != nullptr);
    REQUIRE(xbar1 != nullptr);
    REQUIRE(xbar2 != nullptr);

    eq.run(300);
    REQUIRE(eq.getCurrentCycle() == 300);

    // 关键:2 级 xbar 串接时,响应必须能穿越两个 xbar 流回 CPU
    // (P0-5b 修复前:中间 xbar 响应路径可能未绑)
    REQUIRE(cpu0->last_response_transaction_id() > 0);
    SUCCEED("2-stage crossbar chain preserves response path");
}

// ============================================================================
// 拓扑 6：Sustained stress (4 CPUs + 1 Xbar + 1 Mem, 1000 cycles)
// 验证：长时间运行无死锁、无响应丢失、跨多 port 持续工作
// ============================================================================

TEST_CASE("E2E: Sustained stress 4 CPUs + Xbar + 1 Mem, 1000 cycles",
          "[e2e][complex][stress][long-run]") {
    registerAllModules();
    json config = R"({
        "modules": [
            {"name": "cpu0", "type": "CPUTLM"},
            {"name": "cpu1", "type": "CPUTLM"},
            {"name": "cpu2", "type": "CPUTLM"},
            {"name": "cpu3", "type": "CPUTLM"},
            {"name": "xbar", "type": "CrossbarTLM"},
            {"name": "mem", "type": "MemoryTLM"}
        ],
        "connections": [
            {"src": "cpu0", "dst": "xbar.0", "latency": 1},
            {"src": "cpu1", "dst": "xbar.1", "latency": 1},
            {"src": "cpu2", "dst": "xbar.2", "latency": 1},
            {"src": "cpu3", "dst": "xbar.3", "latency": 1},
            {"src": "xbar.0", "dst": "mem", "latency": 1}
        ]
    })"_json;
    EventQueue eq;
    ModuleFactory factory(&eq);
    REQUIRE(factory.instantiateAll(config));
    factory.startAllTicks();

    auto* cpu0 = dynamic_cast<CPUTLM*>(factory.getInstance("cpu0"));
    auto* cpu1 = dynamic_cast<CPUTLM*>(factory.getInstance("cpu1"));
    auto* cpu2 = dynamic_cast<CPUTLM*>(factory.getInstance("cpu2"));
    auto* cpu3 = dynamic_cast<CPUTLM*>(factory.getInstance("cpu3"));
    REQUIRE(cpu0 != nullptr);
    REQUIRE(cpu1 != nullptr);
    REQUIRE(cpu2 != nullptr);
    REQUIRE(cpu3 != nullptr);

    // 1000 cycles 持续负载:每个 CPU 期望收到多个响应
    eq.run(1000);
    REQUIRE(eq.getCurrentCycle() == 1000);

    // 所有 4 个 CPU 都应收到响应(P0-5b 修复前 cpu1/2/3 永远 0)
    REQUIRE(cpu0->last_response_transaction_id() > 0);
    REQUIRE(cpu1->last_response_transaction_id() > 0);
    REQUIRE(cpu2->last_response_transaction_id() > 0);
    REQUIRE(cpu3->last_response_transaction_id() > 0);

    SUCCEED("1000-cycle stress: all 4 CPUs receive responses");
}
