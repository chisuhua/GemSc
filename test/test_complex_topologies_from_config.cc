// test/test_complex_topologies_from_config.cc
// 端到端测试：从 configs/ 加载复杂拓扑（mesh/ring/hierarchical）并运行仿真
// 补充 test/test_complex_topologies_e2e.cc 的 inline JSON 路径
// 标签：[e2e][complex-topology][from-config]
#include <filesystem>
#include <fstream>
#include "chstream_register.hh"
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "core/module_factory.hh"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

static json loadConfig(const std::string& path) {
    std::string full_path = std::string(CPPTLM_SOURCE_DIR) + "/" + path;
    std::ifstream f(full_path);
    REQUIRE(f.is_open());
    return json::parse(f);
}

// ────────────────────────────────────────────────────────────────────
// Mesh Topologies (loaded from configs/)
// ────────────────────────────────────────────────────────────────────

TEST_CASE("E2E: Load mesh_2x2_tlm.json — 2x2 Mesh NoC from config",
          "[e2e][complex-topology][from-config][mesh]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    auto config = loadConfig("configs/mesh_2x2_tlm.json");
    REQUIRE(factory.instantiateAll(config));
    factory.startAllTicks();

    // 4 routers + 4 NIs + 4 CPUs = 12 modules
    REQUIRE(factory.getInstance("router_0_0") != nullptr);
    REQUIRE(factory.getInstance("router_0_1") != nullptr);
    REQUIRE(factory.getInstance("router_1_0") != nullptr);
    REQUIRE(factory.getInstance("router_1_1") != nullptr);

    uint64_t before = eq.getCurrentCycle();
    eq.run(100);
    REQUIRE(eq.getCurrentCycle() == before + 100);
}

TEST_CASE("E2E: Load mesh_4x4_tlm.json — 4x4 Mesh NoC (large topology)",
          "[e2e][complex-topology][from-config][mesh][large]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    auto config = loadConfig("configs/mesh_4x4_tlm.json");
    REQUIRE(factory.instantiateAll(config));
    factory.startAllTicks();

    // 16 routers + 16 NIs + 16 CPUs = 48 modules
    for (int x = 0; x < 4; ++x) {
        for (int y = 0; y < 4; ++y) {
            REQUIRE(factory.getInstance("router_" + std::to_string(x) + "_" + std::to_string(y)) !=
                    nullptr);
        }
    }

    uint64_t before = eq.getCurrentCycle();
    eq.run(100);
    REQUIRE(eq.getCurrentCycle() == before + 100);
}

// ────────────────────────────────────────────────────────────────────
// Ring Topology (loaded from configs/)
// ────────────────────────────────────────────────────────────────────

TEST_CASE("E2E: Load ring_8_tlm.json — 8-node ring from config",
          "[e2e][complex-topology][from-config][ring]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    auto config = loadConfig("configs/ring_8_tlm.json");
    REQUIRE(factory.instantiateAll(config));
    factory.startAllTicks();

    // Ring has 11 nodes (node_0..node_10 in ring_8_tlm.json)
    REQUIRE(factory.getInstance("node_0") != nullptr);
    REQUIRE(factory.getInstance("node_1") != nullptr);
    REQUIRE(factory.getInstance("node_7") != nullptr);

    uint64_t before = eq.getCurrentCycle();
    eq.run(100);
    REQUIRE(eq.getCurrentCycle() == before + 100);
}

// ────────────────────────────────────────────────────────────────────
// Hierarchical Topology (loaded from configs/)
// ────────────────────────────────────────────────────────────────────

TEST_CASE("E2E: Load hierarchical_2x2_tlm.json — 2x2 hierarchical from config",
          "[e2e][complex-topology][from-config][hierarchical]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    auto config = loadConfig("configs/hierarchical_2x2_tlm.json");
    REQUIRE(factory.instantiateAll(config));
    factory.startAllTicks();

    // hierarchical_2x2_tlm.json: 7 modules named root, l1_n0, l1_n1, etc.
    REQUIRE(factory.getInstance("root") != nullptr);
    REQUIRE(factory.getInstance("l1_n0") != nullptr);
    REQUIRE(factory.getInstance("l1_n1") != nullptr);

    uint64_t before = eq.getCurrentCycle();
    eq.run(100);
    REQUIRE(eq.getCurrentCycle() == before + 100);
}
