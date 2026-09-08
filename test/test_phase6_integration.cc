// test/test_phase6_integration.cc
// Phase 6: End-to-end integration — Cache→Crossbar→Memory
// 功能描述：验证 ChStream 模块端到端数据通路 + ModuleFactory 完整集成
// 作者 CppTLM Team / 日期 2026-04-13
#include "chstream_register.hh"
#include "bundles/cache_bundles_tlm.hh"
#include "core/event_queue.hh"
#include "core/module_factory.hh"
#include "framework/chstream_adapter_factory.hh"
#include "metrics/stats_manager.hh"
#include "modules.hh"
#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

static void registerChStreamModules() {
    static bool registered = false;
    if (!registered) {
        REGISTER_OBJECT;
        REGISTER_CHSTREAM;
        registered = true;
    }
}

TEST_CASE("Phase 6: Full integration — Cache→Crossbar→Memory", "[phase6][integration]") {
    EventQueue eq;
    registerChStreamModules();
    ModuleFactory factory(&eq);

    json config = R"({
        "modules": [
            {"name": "cache", "type": "CacheTLM"},
            {"name": "xbar", "type": "CrossbarTLM"},
            {"name": "mem", "type": "MemoryTLM"}
        ],
        "connections": [
            {"src": "cache", "dst": "xbar.0", "latency": 1},
            {"src": "xbar.0", "dst": "mem", "latency": 2}
        ]
    })"_json;

    factory.instantiateAll(config);
    factory.startAllTicks();

    // Verify all instances exist
    auto* cache = factory.getInstance("cache");
    auto* xbar = factory.getInstance("xbar");
    auto* mem = factory.getInstance("mem");
    REQUIRE(cache != nullptr);
    REQUIRE(xbar != nullptr);
    REQUIRE(mem != nullptr);

    // Verify types
    REQUIRE(cache->get_module_type() == "CacheTLM");
    REQUIRE(xbar->get_module_type() == "CrossbarTLM");
    REQUIRE(mem->get_module_type() == "MemoryTLM");

    // Verify XbarTLM has correct port count
    auto* xbar_tlm = dynamic_cast<CrossbarTLM*>(xbar);
    REQUIRE(xbar_tlm != nullptr);
    REQUIRE(xbar_tlm->num_ports() == 4);

    // Verify routing logic
    REQUIRE(xbar_tlm->route_address(0x1000) == 1);

    // Run simulation for 50 cycles
    eq.run(50);
    REQUIRE(eq.getCurrentCycle() == 50);
}

TEST_CASE("Phase 6: Multi-port Crossbar with 4 Memory modules", "[phase6][integration]") {
    EventQueue eq;
    registerChStreamModules();
    ModuleFactory factory(&eq);

    json config = R"({
        "modules": [
            {"name": "cache", "type": "CacheTLM"},
            {"name": "xbar", "type": "CrossbarTLM"},
            {"name": "mem0", "type": "MemoryTLM"},
            {"name": "mem1", "type": "MemoryTLM"},
            {"name": "mem2", "type": "MemoryTLM"},
            {"name": "mem3", "type": "MemoryTLM"}
        ],
        "connections": [
            {"src": "cache", "dst": "xbar.0", "latency": 1},
            {"src": "xbar.0", "dst": "mem0", "latency": 2},
            {"src": "xbar.1", "dst": "mem1", "latency": 2},
            {"src": "xbar.2", "dst": "mem2", "latency": 2},
            {"src": "xbar.3", "dst": "mem3", "latency": 2}
        ]
    })"_json;

    factory.instantiateAll(config);

    // All 6 modules should exist
    REQUIRE(factory.getInstance("cache") != nullptr);
    REQUIRE(factory.getInstance("xbar") != nullptr);
    REQUIRE(factory.getInstance("mem0") != nullptr);
    REQUIRE(factory.getInstance("mem1") != nullptr);
    REQUIRE(factory.getInstance("mem2") != nullptr);
    REQUIRE(factory.getInstance("mem3") != nullptr);

    auto* xbar = dynamic_cast<CrossbarTLM*>(factory.getInstance("xbar"));
    REQUIRE(xbar != nullptr);
    REQUIRE(xbar->num_ports() == 4);

    eq.run(20);
    REQUIRE(eq.getCurrentCycle() == 20);
}

TEST_CASE("Phase 6: ChStreamAdapterFactory multi-port detection", "[phase6][factory]") {
    auto& factory = ChStreamAdapterFactory::get();
    registerChStreamModules();
    REQUIRE(factory.knows("CacheTLM"));
    REQUIRE(factory.knows("MemoryTLM"));
    REQUIRE(factory.knows("CrossbarTLM"));

    REQUIRE_FALSE(factory.isMultiPort("CacheTLM"));
    REQUIRE_FALSE(factory.isMultiPort("MemoryTLM"));
    REQUIRE(factory.isMultiPort("CrossbarTLM"));

    REQUIRE(factory.getPortCount("CacheTLM") == 1);
    REQUIRE(factory.getPortCount("MemoryTLM") == 1);
    REQUIRE(factory.getPortCount("CrossbarTLM") == 4);
}

TEST_CASE("Phase 6: CrossbarTLM routing verification", "[phase6][crossbar]") {
    EventQueue eq;
    CrossbarTLM xbar("xbar", &eq);

    REQUIRE(xbar.route_address(0x0000) == 0);
    REQUIRE(xbar.route_address(0x0FFF) == 0);
    REQUIRE(xbar.route_address(0x1000) == 1);
    REQUIRE(xbar.route_address(0x1FFF) == 1);
    REQUIRE(xbar.route_address(0x2000) == 2);
    REQUIRE(xbar.route_address(0x2FFF) == 2);
    REQUIRE(xbar.route_address(0x3000) == 3);
    REQUIRE(xbar.route_address(0x3FFF) == 3);
}

TEST_CASE("Phase 6: E2E data flow cache→xbar→mem", "[phase6][e2e][regression]") {
    // F11 新风险#1: E2E 回归守护
    // 走 StreamAdapter/MasterPort/SlavePort/PortPair 标准通路 (tg→cache→xbar→mem)
    // 若 P0-#3 (CrossbarTLM 单指针化 multi_adapter_ + set_stream_adapter) regress,
    // xbar 多 port 接线失败 → instantiation 或 simulation 失败 → 本测试失败
    EventQueue eq;
    registerChStreamModules();
    ModuleFactory factory(&eq);

    json config = R"({
        "modules": [
            {"name": "tg", "type": "TrafficGenTLM", "params": {"num_requests": 5}},
            {"name": "cache", "type": "CacheTLM"},
            {"name": "xbar", "type": "CrossbarTLM"},
            {"name": "mem", "type": "MemoryTLM"}
        ],
        "connections": [
            {"src": "tg", "dst": "cache", "latency": 1},
            {"src": "cache", "dst": "xbar.0", "latency": 1},
            {"src": "xbar.0", "dst": "mem", "latency": 1}
        ]
    })"_json;

    REQUIRE(factory.instantiateAll(config));
    factory.startAllTicks();

    // 验证拓扑 — 全部 4 个模块通过 ModuleFactory 创建
    auto* tg = factory.getInstance("tg");
    auto* cache = factory.getInstance("cache");
    auto* xbar = factory.getInstance("xbar");
    auto* mem = factory.getInstance("mem");
    REQUIRE(tg != nullptr);
    REQUIRE(cache != nullptr);
    REQUIRE(xbar != nullptr);
    REQUIRE(mem != nullptr);

    // 验证 xbar 4-port 接线（P0-#3 fix 验证点）
    auto* xbar_tlm = dynamic_cast<CrossbarTLM*>(xbar);
    REQUIRE(xbar_tlm != nullptr);
    REQUIRE(xbar_tlm->num_ports() == 4);

    // 运行仿真 — 走完整 E2E 通路（tg→cache→xbar→mem→响应回 tg）
    // num_requests=5 确保 TrafficGen 实际发起事务（不止拓扑验证）
    eq.run(200);
    REQUIRE(eq.getCurrentCycle() == 200);

    // F11 P2.2 strengthen (Metis review): 通过 StatsManager 验证 TrafficGen 实际发起事务。
    // 比 SUCCEED-only 更强: 即便 P0-#3 regression 让 instantiation 偶然通过,
    // 若 traffic_gen 未发起事务, 本测试必失败。
    //
    // 注: CacheTLM/MemoryTLM 也有 stats_requests_/stats_requests_read_ 计数器,
    // 但 tick() 当前未 increment (这是 F10 待补工作)。本测试先验证 TrafficGen,
    // Cache/Memory 验证待 F10 完成 stats increment 后在后续 P2 commit 追加。
    //
    // 不调用 reset_all() — 它会清零所有 Scalar 计数器, 直接读取 eq.run(200) 之后的状态。
    auto* tg_group = tlm_stats::StatsManager::instance().find_group("system.traffic_gen");

    tlm_stats::Counter tg_issued = 0;
    if (tg_group) {
        for (auto& [name, stat] : tg_group->stats()) {
            if (name == "requests_issued") {
                if (auto* s = dynamic_cast<tlm_stats::Scalar*>(stat.get()))
                    tg_issued = s->value();
            }
        }
    }

    // TrafficGen 至少发起 1 个请求 (num_requests=5 期望 5)
    CHECK(tg_issued >= 1);

    SUCCEED("Phase 6 E2E data flow cache→xbar→mem 通过 StreamAdapter/MasterPort/SlavePort/PortPair "
            "完整通路 + TrafficGen stats_issued>=1 (Cache/Memory 验证待 F10)");
}

TEST_CASE("Phase 6: JSON config with port-indexed connections", "[phase6][json]") {
    json config = R"({
        "modules": [
            {"name": "cache", "type": "CacheTLM"},
            {"name": "xbar", "type": "CrossbarTLM"},
            {"name": "mem", "type": "MemoryTLM"}
        ],
        "connections": [
            {"src": "cache", "dst": "xbar.0", "latency": 1},
            {"src": "xbar.0", "dst": "mem", "latency": 2}
        ]
    })"_json;

    auto conns = config["connections"];
    REQUIRE(conns.size() == 2);
    REQUIRE(conns[0]["src"] == "cache");
    REQUIRE(conns[0]["dst"] == "xbar.0");
    REQUIRE(conns[0]["latency"] == 1);
    REQUIRE(conns[1]["src"] == "xbar.0");
    REQUIRE(conns[1]["dst"] == "mem");
}

TEST_CASE("ChStream ModuleFactory: CacheTLM single-port instantiation", "[phase6][factory]") {
    REGISTER_OBJECT
    REGISTER_CHSTREAM

    EventQueue eq;
    ModuleFactory factory(&eq);

    json config = R"({
        "modules": [
            {"name": "cache", "type": "CacheTLM"},
            {"name": "mem", "type": "MemoryTLM"}
        ],
        "connections": [
            {"src": "cache", "dst": "mem", "latency": 1}
        ]
    })"_json;

    factory.instantiateAll(config);
    REQUIRE(factory.getInstance("cache") != nullptr);
    REQUIRE(factory.getInstance("mem") != nullptr);
    REQUIRE(factory.getInstance("cache")->get_module_type() == "CacheTLM");
    REQUIRE(factory.getInstance("mem")->get_module_type() == "MemoryTLM");
}

TEST_CASE("ChStream ModuleFactory: CrossbarTLM multi-port instantiation", "[phase6][factory]") {
    REGISTER_OBJECT
    REGISTER_CHSTREAM

    EventQueue eq;
    ModuleFactory factory(&eq);

    json config = R"({
        "modules": [
            {"name": "cache", "type": "CacheTLM"},
            {"name": "xbar", "type": "CrossbarTLM"},
            {"name": "mem", "type": "MemoryTLM"}
        ],
        "connections": [
            {"src": "cache", "dst": "xbar.0", "latency": 1},
            {"src": "xbar.0", "dst": "mem", "latency": 2}
        ]
    })"_json;

    factory.instantiateAll(config);

    auto* xbar = dynamic_cast<CrossbarTLM*>(factory.getInstance("xbar"));
    REQUIRE(xbar != nullptr);
    REQUIRE(xbar->num_ports() == 4);
    REQUIRE(xbar->route_address(0x0000) == 0);
    REQUIRE(xbar->route_address(0x1000) == 1);
    REQUIRE(xbar->route_address(0x2000) == 2);
    REQUIRE(xbar->route_address(0x3000) == 3);
}
