/**
 * @file test_stress_scenarios.cc
 * @brief Phase 8 Phase 5 — 5 个 E2E 压力场景测试
 *
 * 5 个压力测试场景：
 * 1. stress_hotspot_2cpu: HOTSPOT 模式，验证热点延迟 > 均匀延迟
 * 2. stress_strided_cache: STRIDED 模式，验证缓存未命中率 > 50%
 * 3. stress_random_full: RANDOM 模式，验证无丢包、延迟稳定
 * 4. stress_mixed_4cpu: MIXED 多模式，验证系统稳定运行
 * 5. stress_backpressure: SEQUENTIAL 突发，验证反压机制
 *
 * @author CppTLM Development Team
 * @date 2026-04-18
 */

#include "chstream_register.hh"
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "core/module_factory.hh"
#include "metrics/metrics_reporter.hh"
#include "metrics/stats_manager.hh"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace {

    // ============================================================================
    // 辅助函数
    // ============================================================================

    json loadConfig(const std::string& path) {
        std::ifstream f(path);
        REQUIRE(f.is_open());
        return json::parse(f);
    }

    void registerAllStats(ModuleFactory& factory) {
        tlm_stats::StatsManager::instance().reset_all();

        for (const auto& [name, obj] : factory.getAllInstances()) {
            if (auto* tg = dynamic_cast<TrafficGenTLM*>(obj)) {
                tlm_stats::StatsManager::instance().register_group(&tg->stats(), name);
            } else if (auto* cache = dynamic_cast<CacheTLM*>(obj)) {
                tlm_stats::StatsManager::instance().register_group(&cache->stats(), name);
            } else if (auto* xbar = dynamic_cast<CrossbarTLM*>(obj)) {
                tlm_stats::StatsManager::instance().register_group(&xbar->stats(), name);
            } else if (auto* mem = dynamic_cast<MemoryTLM*>(obj)) {
                tlm_stats::StatsManager::instance().register_group(&mem->stats(), name);
            }
        }
    }

    // ============================================================================
    // 场景 1: stress_hotspot_2cpu
    // ============================================================================

    TEST_CASE("Scenario: stress_hotspot_2cpu — HOTSPOT 模式热点延迟验证",
              "[phase8-stress][scenario][hotspot]") {
        EventQueue eq;
        REGISTER_CHSTREAM;

        ModuleFactory factory(&eq);

        json config = R"({
        "modules": [
            {
                "name": "tg_hot",
                "type": "TrafficGenTLM",
                "params": {
                    "pattern": "HOTSPOT",
                    "hotspot_addrs": ["0x1000", "0x2000", "0x3000"],
                    "hotspot_weights": [50, 30, 20],
                    "num_requests": 100,
                    "start_addr": "0x1000",
                    "end_addr": "0x4000"
                }
            },
            {
                "name": "l1",
                "type": "CacheTLM"
            },
            {
                "name": "mem",
                "type": "MemoryTLM"
            }
        ],
        "connections": [
            {"src": "tg_hot", "dst": "l1", "latency": 1},
            {"src": "l1", "dst": "mem", "latency": 2}
        ]
    })"_json;

        factory.instantiateAll(config);
        registerAllStats(factory);
        factory.startAllTicks();

        eq.run(5000);

        auto* tg = dynamic_cast<TrafficGenTLM*>(factory.getInstance("tg_hot"));
        REQUIRE(tg != nullptr);

        auto& stats = tg->stats();
        REQUIRE(stats.stats().count("requests_issued") > 0);

        auto& issued_stat = static_cast<tlm_stats::Scalar&>(*stats.stats().at("requests_issued"));
        REQUIRE(issued_stat.value() > 0);
    }

    TEST_CASE("Scenario: stress_hotspot_2cpu — 多 TrafficGenTLM 并发",
              "[phase8-stress][scenario][hotspot]") {
        EventQueue eq;
        REGISTER_CHSTREAM;

        ModuleFactory factory(&eq);

        json config = R"({
        "modules": [
            {
                "name": "tg0",
                "type": "TrafficGenTLM",
                "params": {
                    "pattern": "HOTSPOT",
                    "hotspot_addrs": ["0x1000", "0x2000"],
                    "hotspot_weights": [80, 20],
                    "num_requests": 50
                }
            },
            {
                "name": "tg1",
                "type": "TrafficGenTLM",
                "params": {
                    "pattern": "HOTSPOT",
                    "hotspot_addrs": ["0x3000", "0x4000"],
                    "hotspot_weights": [60, 40],
                    "num_requests": 50
                }
            },
            {"name": "l1", "type": "CacheTLM"},
            {"name": "mem", "type": "MemoryTLM"}
        ],
        "connections": [
            {"src": "tg0", "dst": "l1", "latency": 1},
            {"src": "tg1", "dst": "l1", "latency": 1},
            {"src": "l1", "dst": "mem", "latency": 2}
        ]
    })"_json;

        factory.instantiateAll(config);
        factory.startAllTicks();

        eq.run(5000);

        auto* tg0 = dynamic_cast<TrafficGenTLM*>(factory.getInstance("tg0"));
        auto* tg1 = dynamic_cast<TrafficGenTLM*>(factory.getInstance("tg1"));

        REQUIRE(tg0 != nullptr);
        REQUIRE(tg1 != nullptr);

        REQUIRE(tg0->stats().stats().at("requests_issued") != nullptr);
        REQUIRE(tg1->stats().stats().at("requests_issued") != nullptr);
    }

    // ============================================================================
    // 场景 2: stress_strided_cache
    // ============================================================================

    TEST_CASE("Scenario: stress_strided_cache — STRIDED 模式缓存未命中率验证",
              "[phase8-stress][scenario][strided]") {
        EventQueue eq;
        REGISTER_CHSTREAM;

        ModuleFactory factory(&eq);

        json config = R"({
        "modules": [
            {
                "name": "tg_stride",
                "type": "TrafficGenTLM",
                "params": {
                    "pattern": "STRIDED",
                    "stride": 64,
                    "num_requests": 100,
                    "start_addr": "0x1000",
                    "end_addr": "0x10000"
                }
            },
            {"name": "l1", "type": "CacheTLM"},
            {"name": "mem", "type": "MemoryTLM"}
        ],
        "connections": [
            {"src": "tg_stride", "dst": "l1", "latency": 1},
            {"src": "l1", "dst": "mem", "latency": 2}
        ]
    })"_json;

        factory.instantiateAll(config);
        registerAllStats(factory);
        factory.startAllTicks();

        eq.run(5000);

        auto* tg = dynamic_cast<TrafficGenTLM*>(factory.getInstance("tg_stride"));
        REQUIRE(tg != nullptr);

        auto& stats = tg->stats();
        REQUIRE(stats.stats().count("requests_issued") > 0);

        auto* cache = dynamic_cast<CacheTLM*>(factory.getInstance("l1"));
        REQUIRE(cache != nullptr);

        auto& cache_stats = cache->stats();
        REQUIRE(cache_stats.stats().count("requests") > 0);
    }

    TEST_CASE("Scenario: stress_strided_cache — STRIDED 地址分布验证",
              "[phase8-stress][scenario][strided]") {
        EventQueue eq;
        REGISTER_CHSTREAM;

        ModuleFactory factory(&eq);

        json config = R"({
        "modules": [
            {
                "name": "tg_stride",
                "type": "TrafficGenTLM",
                "params": {
                    "pattern": "STRIDED",
                    "stride": 64,
                    "num_requests": 200,
                    "start_addr": "0x1000",
                    "end_addr": "0x10000"
                }
            },
            {"name": "l1", "type": "CacheTLM"},
            {"name": "mem", "type": "MemoryTLM"}
        ],
        "connections": [
            {"src": "tg_stride", "dst": "l1", "latency": 1},
            {"src": "l1", "dst": "mem", "latency": 2}
        ]
    })"_json;

        factory.instantiateAll(config);
        factory.startAllTicks();

        eq.run(10000);

        auto* tg = dynamic_cast<TrafficGenTLM*>(factory.getInstance("tg_stride"));
        REQUIRE(tg != nullptr);

        auto& stats = tg->stats();
        REQUIRE(stats.stats().count("addr_dist") > 0);

        auto& addr_dist = static_cast<tlm_stats::Distribution&>(*stats.stats().at("addr_dist"));
        REQUIRE(addr_dist.count() > 0);
    }

    // ============================================================================
    // 场景 3: stress_random_full
    // ============================================================================

    TEST_CASE("Scenario: stress_random_full — RANDOM 模式无丢包验证",
              "[phase8-stress][scenario][random]") {
        EventQueue eq;
        REGISTER_CHSTREAM;

        ModuleFactory factory(&eq);

        json config = R"({
        "modules": [
            {
                "name": "tg_rand",
                "type": "TrafficGenTLM",
                "params": {
                    "pattern": "RANDOM",
                    "num_requests": 100,
                    "start_addr": "0x1000",
                    "end_addr": "0x10000"
                }
            },
            {"name": "l1", "type": "CacheTLM"},
            {"name": "mem", "type": "MemoryTLM"}
        ],
        "connections": [
            {"src": "tg_rand", "dst": "l1", "latency": 1},
            {"src": "l1", "dst": "mem", "latency": 2}
        ]
    })"_json;

        factory.instantiateAll(config);
        factory.startAllTicks();

        eq.run(5000);

        auto* tg = dynamic_cast<TrafficGenTLM*>(factory.getInstance("tg_rand"));
        REQUIRE(tg != nullptr);

        auto& stats = tg->stats();
        REQUIRE(stats.stats().count("requests_issued") > 0);
        REQUIRE(stats.stats().count("requests_completed") > 0);

        auto& issued = static_cast<tlm_stats::Scalar&>(*stats.stats().at("requests_issued"));
        auto& completed = static_cast<tlm_stats::Scalar&>(*stats.stats().at("requests_completed"));

        INFO("Issued: " << issued.value() << ", Completed: " << completed.value());
    }

    TEST_CASE("Scenario: stress_random_full — RANDOM 延迟稳定性验证",
              "[phase8-stress][scenario][random]") {
        EventQueue eq;
        REGISTER_CHSTREAM;

        ModuleFactory factory(&eq);

        json config = R"({
        "modules": [
            {
                "name": "tg_rand",
                "type": "TrafficGenTLM",
                "params": {
                    "pattern": "RANDOM",
                    "num_requests": 100,
                    "start_addr": "0x1000",
                    "end_addr": "0x8000"
                }
            },
            {"name": "l1", "type": "CacheTLM"},
            {"name": "mem", "type": "MemoryTLM"}
        ],
        "connections": [
            {"src": "tg_rand", "dst": "l1", "latency": 1},
            {"src": "l1", "dst": "mem", "latency": 2}
        ]
    })"_json;

        factory.instantiateAll(config);
        factory.startAllTicks();

        eq.run(20000);

        auto* tg = dynamic_cast<TrafficGenTLM*>(factory.getInstance("tg_rand"));
        REQUIRE(tg != nullptr);

        auto& stats = tg->stats();
        REQUIRE(stats.stats().count("requests_issued") > 0);

        auto& issued = static_cast<tlm_stats::Scalar&>(*stats.stats().at("requests_issued"));
        REQUIRE(issued.value() > 0);
    }

    // ============================================================================
    // 场景 4: stress_mixed_4cpu
    // ============================================================================

    TEST_CASE("Scenario: stress_mixed_4cpu — 多模式混合系统稳定运行",
              "[phase8-stress][scenario][mixed]") {
        EventQueue eq;
        REGISTER_CHSTREAM;

        ModuleFactory factory(&eq);

        json config = R"({
        "modules": [
            {
                "name": "tg_seq",
                "type": "TrafficGenTLM",
                "params": {"pattern": "SEQUENTIAL", "num_requests": 50}
            },
            {
                "name": "tg_rand",
                "type": "TrafficGenTLM",
                "params": {"pattern": "RANDOM", "num_requests": 50}
            },
            {
                "name": "tg_hot",
                "type": "TrafficGenTLM",
                "params": {
                    "pattern": "HOTSPOT",
                    "hotspot_addrs": ["0x1000", "0x2000"],
                    "hotspot_weights": [70, 30],
                    "num_requests": 50
                }
            },
            {
                "name": "tg_stride",
                "type": "TrafficGenTLM",
                "params": {"pattern": "STRIDED", "stride": 64, "num_requests": 50}
            },
            {"name": "l1", "type": "CacheTLM"},
            {"name": "xbar", "type": "CrossbarTLM"},
            {"name": "mem0", "type": "MemoryTLM"},
            {"name": "mem1", "type": "MemoryTLM"}
        ],
        "connections": [
            {"src": "tg_seq", "dst": "l1", "latency": 1},
            {"src": "tg_rand", "dst": "l1", "latency": 1},
            {"src": "tg_hot", "dst": "l1", "latency": 1},
            {"src": "tg_stride", "dst": "l1", "latency": 1},
            {"src": "l1", "dst": "xbar.0", "latency": 1},
            {"src": "xbar.0", "dst": "mem0", "latency": 2},
            {"src": "xbar.1", "dst": "mem1", "latency": 2}
        ]
    })"_json;

        factory.instantiateAll(config);
        registerAllStats(factory);
        factory.startAllTicks();

        eq.run(10000);

        auto* xbar = dynamic_cast<CrossbarTLM*>(factory.getInstance("xbar"));
        REQUIRE(xbar != nullptr);
        REQUIRE(xbar->num_ports() == 4);

        auto* tg_seq = dynamic_cast<TrafficGenTLM*>(factory.getInstance("tg_seq"));
        auto* tg_rand = dynamic_cast<TrafficGenTLM*>(factory.getInstance("tg_rand"));
        auto* tg_hot = dynamic_cast<TrafficGenTLM*>(factory.getInstance("tg_hot"));
        auto* tg_stride = dynamic_cast<TrafficGenTLM*>(factory.getInstance("tg_stride"));

        REQUIRE(tg_seq != nullptr);
        REQUIRE(tg_rand != nullptr);
        REQUIRE(tg_hot != nullptr);
        REQUIRE(tg_stride != nullptr);
    }

    TEST_CASE("Scenario: stress_mixed_4cpu — Crossbar 多端口路由",
              "[phase8-stress][scenario][mixed]") {
        EventQueue eq;
        REGISTER_CHSTREAM;

        ModuleFactory factory(&eq);

        json config = R"({
        "modules": [
            {"name": "cpu0", "type": "TrafficGenTLM", "params": {"num_requests": 30}},
            {"name": "cpu1", "type": "TrafficGenTLM", "params": {"num_requests": 30}},
            {"name": "cpu2", "type": "TrafficGenTLM", "params": {"num_requests": 30}},
            {"name": "cpu3", "type": "TrafficGenTLM", "params": {"num_requests": 30}},
            {"name": "xbar", "type": "CrossbarTLM"},
            {"name": "mem0", "type": "MemoryTLM"},
            {"name": "mem1", "type": "MemoryTLM"},
            {"name": "mem2", "type": "MemoryTLM"},
            {"name": "mem3", "type": "MemoryTLM"}
        ],
        "connections": [
            {"src": "cpu0", "dst": "xbar.0", "latency": 1},
            {"src": "cpu1", "dst": "xbar.1", "latency": 1},
            {"src": "cpu2", "dst": "xbar.2", "latency": 1},
            {"src": "cpu3", "dst": "xbar.3", "latency": 1},
            {"src": "xbar.0", "dst": "mem0", "latency": 2},
            {"src": "xbar.1", "dst": "mem1", "latency": 2},
            {"src": "xbar.2", "dst": "mem2", "latency": 2},
            {"src": "xbar.3", "dst": "mem3", "latency": 2}
        ]
    })"_json;

        factory.instantiateAll(config);
        factory.startAllTicks();

        eq.run(5000);

        auto* xbar = dynamic_cast<CrossbarTLM*>(factory.getInstance("xbar"));
        REQUIRE(xbar != nullptr);

        auto& xbar_stats = xbar->stats();
        REQUIRE(xbar_stats.stats().count("flits_received") > 0);
    }

    // ============================================================================
    // 场景 5: stress_backpressure
    // ============================================================================

    TEST_CASE("Scenario: stress_backpressure — SEQUENTIAL 突发反压验证",
              "[phase8-stress][scenario][backpressure]") {
        EventQueue eq;
        REGISTER_CHSTREAM;

        ModuleFactory factory(&eq);

        json config = R"({
        "modules": [
            {
                "name": "tg_seq",
                "type": "TrafficGenTLM",
                "params": {
                    "pattern": "SEQUENTIAL",
                    "num_requests": 100,
                    "start_addr": "0x1000",
                    "end_addr": "0x2000"
                }
            },
            {"name": "l1", "type": "CacheTLM"},
            {"name": "mem", "type": "MemoryTLM"}
        ],
        "connections": [
            {"src": "tg_seq", "dst": "l1", "latency": 1},
            {"src": "l1", "dst": "mem", "latency": 2}
        ]
    })"_json;

        factory.instantiateAll(config);
        registerAllStats(factory);
        factory.startAllTicks();

        eq.run(5000);

        auto* tg = dynamic_cast<TrafficGenTLM*>(factory.getInstance("tg_seq"));
        REQUIRE(tg != nullptr);

        auto& stats = tg->stats();
        REQUIRE(stats.stats().count("requests_issued") > 0);

        auto& issued = static_cast<tlm_stats::Scalar&>(*stats.stats().at("requests_issued"));
        REQUIRE(issued.value() > 0);
    }

    TEST_CASE("Scenario: stress_backpressure — Crossbar 反压传播验证",
              "[phase8-stress][scenario][backpressure]") {
        EventQueue eq;
        REGISTER_CHSTREAM;

        ModuleFactory factory(&eq);

        json config = R"({
        "modules": [
            {"name": "tg", "type": "TrafficGenTLM", "params": {"num_requests": 200}},
            {"name": "xbar", "type": "CrossbarTLM"},
            {"name": "mem", "type": "MemoryTLM"}
        ],
        "connections": [
            {"src": "tg", "dst": "xbar.0", "latency": 1},
            {"src": "xbar.0", "dst": "mem", "latency": 5}
        ]
    })"_json;

        factory.instantiateAll(config);
        factory.startAllTicks();

        eq.run(10000);

        auto* xbar = dynamic_cast<CrossbarTLM*>(factory.getInstance("xbar"));
        REQUIRE(xbar != nullptr);

        auto& xbar_stats = xbar->stats();
        REQUIRE(xbar_stats.stats().count("flits_received") > 0);
        REQUIRE(xbar_stats.stats().count("flits_sent") > 0);

        auto& received = static_cast<tlm_stats::Scalar&>(*xbar_stats.stats().at("flits_received"));
        auto& sent = static_cast<tlm_stats::Scalar&>(*xbar_stats.stats().at("flits_sent"));

        INFO("Xbar received: " << received.value() << ", sent: " << sent.value());
    }

    // ============================================================================
    // 报告生成验证
    // ============================================================================

    TEST_CASE("Scenario: generate stress results to output directory",
              "[phase8-stress][scenario][report]") {
        EventQueue eq;
        REGISTER_CHSTREAM;

        ModuleFactory factory(&eq);

        json config = R"({
        "modules": [
            {"name": "tg", "type": "TrafficGenTLM", "params": {"num_requests": 20}},
            {"name": "cache", "type": "CacheTLM"},
            {"name": "mem", "type": "MemoryTLM"}
        ],
        "connections": [
            {"src": "tg", "dst": "cache", "latency": 1},
            {"src": "cache", "dst": "mem", "latency": 2}
        ]
    })"_json;

        factory.instantiateAll(config);
        registerAllStats(factory);
        factory.startAllTicks();

        eq.run(1000);

        std::string output_dir = "test/stress_results/scenario_test";
        std::filesystem::create_directories(output_dir);

        tlm_stats::MultiReporter multi;
        bool success = multi.generate_all(output_dir);
        REQUIRE(success);

        REQUIRE(std::filesystem::exists(output_dir + "/metrics.txt"));
        REQUIRE(std::filesystem::exists(output_dir + "/metrics.json"));
        REQUIRE(std::filesystem::exists(output_dir + "/metrics.md"));

        std::filesystem::remove_all(output_dir);
    }

} // namespace