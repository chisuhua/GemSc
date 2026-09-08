// test/test_e2e_simulation.cc
// 端到端仿真测试：覆盖所有已注册模块类型的JSON配置加载、实例化和仿真运行验证
// 标签体系：[e2e][module-type][topology][sim]

#include <filesystem>
#include <fstream>
#include "chstream_register.hh"
#include "bundles/cache_bundles_tlm.hh"
#include "bundles/noc_bundles_tlm.hh"
#include "core/event_queue.hh"
#include "core/module_factory.hh"
#include "framework/chstream_adapter_factory.hh"
#include "modules.hh"
#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace bundles;

// ============================================================================
// 共享基础设施
// ============================================================================

static void registerAllModules() {
    static bool registered = false;
    if (!registered) {
        REGISTER_OBJECT;
        REGISTER_CHSTREAM;
        REGISTER_MODULE(CpuCluster);
        registered = true;
    }
}

static json loadConfig(const std::string& relative_path) {
    std::string full_path = std::string(CPPTLM_SOURCE_DIR) + "/" + relative_path;
    std::ifstream f(full_path);
    REQUIRE(f.is_open());
    return json::parse(f);
}

// ============================================================================
// Task 1: 基础设施验证
// ============================================================================

TEST_CASE("E2E: 测试基础设施可用", "[e2e][infra]") {
    registerAllModules();
    EventQueue eq;
    REQUIRE(eq.getCurrentCycle() == 0);
    SUCCEED("Infrastructure initialized");
}

// ============================================================================
// Task 2: 单端口 TLM 模块测试
// ============================================================================

TEST_CASE("E2E: CacheTLM + MemoryTLM 实例化与运行", "[e2e][cache][memory][single-port]") {
    registerAllModules();
    json config = R"({
        "modules": [
            {"name": "cache", "type": "CacheTLM"},
            {"name": "mem", "type": "MemoryTLM"}
        ],
        "connections": [
            {"src": "cache", "dst": "mem", "latency": 1}
        ]
    })"_json;
    EventQueue eq;
    ModuleFactory factory(&eq);
    REQUIRE(factory.instantiateAll(config));
    factory.startAllTicks();
    REQUIRE(factory.getInstance("cache") != nullptr);
    REQUIRE(factory.getInstance("mem") != nullptr);
    REQUIRE(factory.getInstance("cache")->get_module_type() == "CacheTLM");
    eq.run(100);
    REQUIRE(eq.getCurrentCycle() == 100);
    SUCCEED("CacheTLM+MemoryTLM instantiated and ran");
}

TEST_CASE("E2E: LinkTLM + RouterTLM NoC连接", "[e2e][link][noc][single-port]") {
    registerAllModules();
    json config = R"({
        "modules": [
            {"name": "r0", "type": "RouterTLM", "params": {"node_x": 0, "node_y": 0, "mesh_x": 2, "mesh_y": 1}},
            {"name": "r1", "type": "RouterTLM", "params": {"node_x": 1, "node_y": 0, "mesh_x": 2, "mesh_y": 1}}
        ],
        "connections": [
            {"src": "r0.1", "dst": "r1.0", "latency": 1}
        ]
    })"_json;
    EventQueue eq;
    ModuleFactory factory(&eq);
    REQUIRE(factory.instantiateAll(config));
    factory.startAllTicks();
    REQUIRE(factory.getInstance("r0") != nullptr);
    REQUIRE(factory.getInstance("r1") != nullptr);
    eq.run(50);
    REQUIRE(eq.getCurrentCycle() == 50);
    SUCCEED("RouterTLM pair instantiated and ran");
}

// ============================================================================
// Task 3: 多端口 TLM 模块测试
// ============================================================================

TEST_CASE("E2E: CrossbarTLM 4端口路由", "[e2e][crossbar][multi-port]") {
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
    auto* xbar = dynamic_cast<CrossbarTLM*>(factory.getInstance("xbar"));
    REQUIRE(xbar != nullptr);
    REQUIRE(xbar->num_ports() == 4);
    REQUIRE(factory.getInstance("cpu0") != nullptr);
    REQUIRE(factory.getInstance("mem") != nullptr);
    eq.run(100);
    REQUIRE(eq.getCurrentCycle() == 100);
    SUCCEED("CrossbarTLM 4-ported topology ran");
}

TEST_CASE("E2E: ArbiterTLM2 双请求者仲裁", "[e2e][arbiter][multi-port]") {
    registerAllModules();
    json config = R"({
        "modules": [
            {"name": "cpu0", "type": "CPUTLM"},
            {"name": "cpu1", "type": "CPUTLM"},
            {"name": "arb", "type": "ArbiterTLM2"},
            {"name": "mem", "type": "MemoryTLM"}
        ],
        "connections": [
            {"src": "cpu0", "dst": "arb.0", "latency": 1},
            {"src": "cpu1", "dst": "arb.1", "latency": 1},
            {"src": "arb.0", "dst": "mem", "latency": 1}
        ]
    })"_json;
    EventQueue eq;
    ModuleFactory factory(&eq);
    REQUIRE(factory.instantiateAll(config));
    factory.startAllTicks();
    auto* arb = dynamic_cast<ArbiterTLM<2>*>(factory.getInstance("arb"));
    REQUIRE(arb != nullptr);
    REQUIRE(arb->num_ports() == 2);
    eq.run(100);
    REQUIRE(eq.getCurrentCycle() == 100);
    SUCCEED("ArbiterTLM2 round-robin exercised");
}

TEST_CASE("E2E: ArbiterTLM4 四请求者仲裁", "[e2e][arbiter][multi-port]") {
    registerAllModules();
    json config = R"({
        "modules": [
            {"name": "tg0", "type": "TrafficGenTLM"},
            {"name": "tg1", "type": "TrafficGenTLM"},
            {"name": "tg2", "type": "TrafficGenTLM"},
            {"name": "tg3", "type": "TrafficGenTLM"},
            {"name": "arb", "type": "ArbiterTLM4"},
            {"name": "mem", "type": "MemoryTLM"}
        ],
        "connections": [
            {"src": "tg0", "dst": "arb.0", "latency": 1},
            {"src": "tg1", "dst": "arb.1", "latency": 1},
            {"src": "tg2", "dst": "arb.2", "latency": 1},
            {"src": "tg3", "dst": "arb.3", "latency": 1},
            {"src": "arb.0", "dst": "mem", "latency": 1}
        ]
    })"_json;
    EventQueue eq;
    ModuleFactory factory(&eq);
    REQUIRE(factory.instantiateAll(config));
    factory.startAllTicks();
    auto* arb = dynamic_cast<ArbiterTLM<4>*>(factory.getInstance("arb"));
    REQUIRE(arb != nullptr);
    REQUIRE(arb->num_ports() == 4);
    eq.run(200);
    REQUIRE(eq.getCurrentCycle() == 200);
    SUCCEED("ArbiterTLM4 topology ran");
}

// ============================================================================
// Task 4: 发起者模块测试
// ============================================================================

TEST_CASE("E2E: CPUTLM 自主发起请求", "[e2e][cpu][initiator]") {
    registerAllModules();
    json config = R"({
        "modules": [
            {"name": "cpu", "type": "CPUTLM"},
            {"name": "mem", "type": "MemoryTLM"}
        ],
        "connections": [
            {"src": "cpu", "dst": "mem", "latency": 1}
        ]
    })"_json;
    EventQueue eq;
    ModuleFactory factory(&eq);
    REQUIRE(factory.instantiateAll(config));
    factory.startAllTicks();
    REQUIRE(factory.getInstance("cpu") != nullptr);
    eq.run(100);
    REQUIRE(eq.getCurrentCycle() == 100);
    SUCCEED("CPUTLM autonomous operation exercised");
}

TEST_CASE("E2E: TrafficGenTLM 顺序模式", "[e2e][traffic-gen][initiator]") {
    registerAllModules();
    json config = R"({
        "modules": [
            {"name": "tg", "type": "TrafficGenTLM"},
            {"name": "mem", "type": "MemoryTLM"}
        ],
        "connections": [
            {"src": "tg", "dst": "mem", "latency": 1}
        ]
    })"_json;
    EventQueue eq;
    ModuleFactory factory(&eq);
    REQUIRE(factory.instantiateAll(config));
    factory.startAllTicks();
    REQUIRE(factory.getInstance("tg") != nullptr);
    eq.run(200);
    REQUIRE(eq.getCurrentCycle() == 200);
    SUCCEED("TrafficGenTLM sequential mode exercised");
}

TEST_CASE("E2E: TrafficGenTLM 随机模式", "[e2e][traffic-gen][initiator]") {
    registerAllModules();
    json config = R"({
        "modules": [
            {"name": "tg", "type": "TrafficGenTLM"},
            {"name": "mem", "type": "MemoryTLM"}
        ],
        "connections": [
            {"src": "tg", "dst": "mem", "latency": 1}
        ]
    })"_json;
    EventQueue eq;
    ModuleFactory factory(&eq);
    REQUIRE(factory.instantiateAll(config));
    factory.startAllTicks();
    REQUIRE(factory.getInstance("tg") != nullptr);
    eq.run(200);
    REQUIRE(eq.getCurrentCycle() == 200);
    SUCCEED("TrafficGenTLM random mode exercised");
}

// ============================================================================
// Task 5: NoC 域测试
// ============================================================================

TEST_CASE("E2E: NICTLM + RouterTLM 最小NoC拓扑", "[e2e][nic][router][noc]") {
    registerAllModules();
    json config = R"({
        "modules": [
            {"name": "ni0", "type": "NICTLM", "params": {"node_id": 0, "mesh_x": 2, "mesh_y": 1}},
            {"name": "router", "type": "RouterTLM", "params": {"node_x": 0, "node_y": 0, "mesh_x": 2, "mesh_y": 1}},
            {"name": "ni1", "type": "NICTLM", "params": {"node_id": 1, "mesh_x": 2, "mesh_y": 1}}
        ],
        "connections": [
            {"src": "ni0.1", "dst": "router.4", "latency": 1},
            {"src": "router.4", "dst": "ni1.1", "latency": 1}
        ]
    })"_json;
    EventQueue eq;
    ModuleFactory factory(&eq);
    REQUIRE(factory.instantiateAll(config));
    factory.startAllTicks();
    auto* router = dynamic_cast<tlm::RouterTLM*>(factory.getInstance("router"));
    REQUIRE(router != nullptr);
    REQUIRE(router->num_ports() == 5);
    REQUIRE(router->node_x() == 0);
    REQUIRE(router->node_y() == 0);
    REQUIRE(router->mesh_x() == 2);
    REQUIRE(router->mesh_y() == 1);
    eq.run(200);
    REQUIRE(eq.getCurrentCycle() == 200);
    SUCCEED("NIC→Router→NIC topology instantiated and ran");
}

TEST_CASE("E2E: RouterTLM XY路由与节点位置", "[e2e][router][routing][xy]") {
    registerAllModules();
    json config = R"({
        "modules": [
            {"name": "r00", "type": "RouterTLM", "params": {"node_x": 0, "node_y": 0, "mesh_x": 2, "mesh_y": 2}},
            {"name": "r01", "type": "RouterTLM", "params": {"node_x": 0, "node_y": 1, "mesh_x": 2, "mesh_y": 2}},
            {"name": "r10", "type": "RouterTLM", "params": {"node_x": 1, "node_y": 0, "mesh_x": 2, "mesh_y": 2}},
            {"name": "r11", "type": "RouterTLM", "params": {"node_x": 1, "node_y": 1, "mesh_x": 2, "mesh_y": 2}}
        ],
        "connections": [
            {"src": "r00.2", "dst": "r01.0", "latency": 1},
            {"src": "r00.1", "dst": "r10.3", "latency": 1},
            {"src": "r01.1", "dst": "r11.3", "latency": 1},
            {"src": "r10.2", "dst": "r11.0", "latency": 1}
        ]
    })"_json;
    EventQueue eq;
    ModuleFactory factory(&eq);
    REQUIRE(factory.instantiateAll(config));
    factory.startAllTicks();
    auto* r00 = dynamic_cast<tlm::RouterTLM*>(factory.getInstance("r00"));
    REQUIRE(r00 != nullptr);
    REQUIRE(r00->node_id() == 0);
    REQUIRE(r00->mesh_x() == 2);
    REQUIRE(r00->mesh_y() == 2);
    auto* r11 = dynamic_cast<tlm::RouterTLM*>(factory.getInstance("r11"));
    REQUIRE(r11 != nullptr);
    REQUIRE(r11->node_id() == 3);
    eq.run(50);
    REQUIRE(eq.getCurrentCycle() == 50);
    SUCCEED("RouterTLM XY routing positions verified");
}

TEST_CASE("E2E: NICTLM 地址映射与Flit", "[e2e][nic][packetize]") {
    registerAllModules();
    json config = R"({
        "modules": [
            {"name": "ni", "type": "NICTLM", "params": {
                "node_id": 5,
                "mesh_x": 4,
                "mesh_y": 4,
                "address_regions": [
                    {"base": 268435456, "size": 67108864, "target_node": 7, "target_type": "MEMORY_CTRL"},
                    {"base": 0, "size": 268435456, "target_node": 3, "target_type": "MEMORY_CTRL"}
                ]
            }}
        ],
        "connections": []
    })"_json;
    EventQueue eq;
    ModuleFactory factory(&eq);
    REQUIRE(factory.instantiateAll(config));
    factory.startAllTicks();
    auto* ni = dynamic_cast<tlm::NICTLM*>(factory.getInstance("ni"));
    REQUIRE(ni != nullptr);
    REQUIRE(ni->node_id() == 5);
    REQUIRE(ni->mesh_x() == 4);
    REQUIRE(ni->mesh_y() == 4);
    REQUIRE(ni->lookup_node(0x10000000) == 7);
    REQUIRE(ni->lookup_node(0x00001000) == 3);
    eq.run(10);
    REQUIRE(eq.getCurrentCycle() == 10);
    SUCCEED("NICTLM address regions configured");
}

// ============================================================================
// Task 6: 拓扑级别端到端测试
// ============================================================================

TEST_CASE("E2E: mesh_2x2完整加载与仿真", "[e2e][topology][mesh][external-config]") {
    registerAllModules();
    auto config = loadConfig("configs/mesh_2x2_tlm.json");
    EventQueue eq;
    ModuleFactory factory(&eq);
    REQUIRE(factory.instantiateAll(config));
    factory.startAllTicks();
    REQUIRE(factory.getInstance("router_0_0") != nullptr);
    REQUIRE(factory.getInstance("router_1_1") != nullptr);
    REQUIRE(factory.getInstance("ni_0_0") != nullptr);
    auto* r00 = dynamic_cast<tlm::RouterTLM*>(factory.getInstance("router_0_0"));
    REQUIRE(r00->node_x() == 0);
    REQUIRE(r00->node_y() == 0);
    REQUIRE(r00->mesh_x() == 2);
    REQUIRE(r00->mesh_y() == 2);
    auto* ni00 = dynamic_cast<tlm::NICTLM*>(factory.getInstance("ni_0_0"));
    REQUIRE(ni00->node_id() == 0);
    eq.run(100);
    REQUIRE(eq.getCurrentCycle() == 100);
    SUCCEED("mesh_2x2_tlm.json fully loaded and simulated");
}

TEST_CASE("E2E: mesh_4x4大规模拓扑加载与仿真", "[e2e][topology][mesh][large][external-config]") {
    registerAllModules();
    auto config = loadConfig("configs/mesh_4x4_tlm.json");
    EventQueue eq;
    ModuleFactory factory(&eq);
    REQUIRE(factory.instantiateAll(config));
    factory.startAllTicks();
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            char name[32];
            snprintf(name, sizeof(name), "router_%d_%d", y, x);
            auto* r = dynamic_cast<tlm::RouterTLM*>(factory.getInstance(name));
            REQUIRE(r != nullptr);
            REQUIRE(r->node_x() == (unsigned)y);
            REQUIRE(r->node_y() == (unsigned)x);
            REQUIRE(r->mesh_x() == 4);
            REQUIRE(r->mesh_y() == 4);
        }
    }
    eq.run(100);
    REQUIRE(eq.getCurrentCycle() == 100);
    SUCCEED("mesh_4x4_tlm.json large topology loaded and simulated");
}

TEST_CASE("E2E: ring_8环形拓扑加载与仿真", "[e2e][topology][ring][external-config]") {
    registerAllModules();
    auto config = loadConfig("configs/ring_8_tlm.json");
    EventQueue eq;
    ModuleFactory factory(&eq);
    REQUIRE(factory.instantiateAll(config));
    factory.startAllTicks();
    for (int i = 0; i < 8; ++i) {
        std::string name = "node_" + std::to_string(i);
        REQUIRE(factory.getInstance(name) != nullptr);
    }
    eq.run(100);
    REQUIRE(eq.getCurrentCycle() == 100);
    SUCCEED("ring_8_tlm.json ring topology loaded and simulated");
}

TEST_CASE("E2E: hierarchical_2x2分层拓扑加载与仿真",
          "[e2e][topology][hierarchical][external-config]") {
    registerAllModules();
    auto config = loadConfig("configs/hierarchical_2x2_tlm.json");
    EventQueue eq;
    ModuleFactory factory(&eq);
    REQUIRE(factory.instantiateAll(config));
    factory.startAllTicks();
    REQUIRE(factory.getInstance("root") != nullptr);
    REQUIRE(factory.getInstance("l1_n0") != nullptr);
    REQUIRE(factory.getInstance("l1_n1") != nullptr);
    auto* root = dynamic_cast<tlm::RouterTLM*>(factory.getInstance("root"));
    REQUIRE(root != nullptr);
    REQUIRE(root->node_x() == 0);
    REQUIRE(root->node_y() == 0);
    eq.run(100);
    REQUIRE(eq.getCurrentCycle() == 100);
    SUCCEED("hierarchical_2x2_tlm.json topology loaded and simulated");
}

TEST_CASE("E2E: 批量加载所有TLM配置文件", "[e2e][config][batch][external-config]") {
    registerAllModules();
    std::vector<std::string> config_files = {
        "configs/crossbar_test.json",    "configs/cache_chstream_test.json",
        "configs/cpu_tlm_test.json",     "configs/traffic_gen_tlm_test.json",
        "configs/arbiter_tlm_test.json", "configs/test/nic_router_nic.json"};
    for (const auto& path : config_files) {
        INFO("Loading config: " << path);
        auto config = loadConfig(path);
        EventQueue eq;
        ModuleFactory factory(&eq);
        REQUIRE(factory.instantiateAll(config));
        factory.startAllTicks();
        eq.run(10);
        REQUIRE(eq.getCurrentCycle() == 10);
    }
    SUCCEED("All TLM config files loaded and simulated");
}

// ============================================================================
// Task 7: 负向测试
// ============================================================================

TEST_CASE("E2E: 未注册模块类型被拒绝", "[e2e][negative][validation]") {
    registerAllModules();
    json config = R"({
        "modules": [{"name": "bad", "type": "NonExistentType"}],
        "connections": []
    })"_json;
    EventQueue eq;
    ModuleFactory factory(&eq);
    factory.instantiateAll(config); // 警告但不会阻止其他模块
    REQUIRE(factory.getInstance("bad") == nullptr);
    SUCCEED("Unknown type warned and not instantiated");
}

TEST_CASE("E2E: RouterTLM缺少params被拒绝", "[e2e][negative][validation][params]") {
    registerAllModules();
    json config = R"({
        "modules": [{"name": "r0", "type": "RouterTLM"}],
        "connections": []
    })"_json;
    EventQueue eq;
    ModuleFactory factory(&eq);
    REQUIRE_FALSE(factory.instantiateAll(config));
    SUCCEED("Missing RouterTLM params correctly rejected");
}

TEST_CASE("E2E: NICTLM缺少params被拒绝", "[e2e][negative][validation][params]") {
    registerAllModules();
    json config = R"({
        "modules": [{"name": "ni0", "type": "NICTLM"}],
        "connections": []
    })"_json;
    EventQueue eq;
    ModuleFactory factory(&eq);
    REQUIRE_FALSE(factory.instantiateAll(config));
    SUCCEED("Missing NICTLM params correctly rejected");
}

TEST_CASE("E2E: 缺少modules字段被拒绝", "[e2e][negative][validation][schema]") {
    registerAllModules();
    json config = R"({"name": "test", "connections": []})"_json;
    EventQueue eq;
    ModuleFactory factory(&eq);
    REQUIRE_FALSE(factory.instantiateAll(config));
    SUCCEED("Missing 'modules' field correctly rejected");
}

TEST_CASE("E2E: 缺少connections字段被拒绝", "[e2e][negative][validation][schema]") {
    registerAllModules();
    json config = R"({"modules": [{"name": "cpu", "type": "CPUTLM"}]})"_json;
    EventQueue eq;
    ModuleFactory factory(&eq);
    REQUIRE_FALSE(factory.instantiateAll(config));
    SUCCEED("Missing 'connections' field correctly rejected");
}

// ============================================================================
// Legacy module tests moved to test_legacy_e2e_simulation.cc
// ============================================================================
