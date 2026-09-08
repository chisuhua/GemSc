// test/test_load_new_configs.cc
// 端到端测试：加载 configs/ 下 7 个新增配置文件并验证可实例化
// 覆盖新模块类型/新特性的真实文件测试（之前只用 inline JSON 测试）
// 标签：[e2e][config][new-configs]
#include <filesystem>
#include <fstream>
#include "chstream_register.hh"
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "core/module_factory.hh"
#include "core/topology_parser.hh"
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using cpptlm::parse_hierarchy_tree;
using cpptlm::TopologyNode;

static json loadConfig(const std::string& path) {
    std::string full_path = std::string(CPPTLM_SOURCE_DIR) + "/" + path;
    std::ifstream f(full_path);
    REQUIRE(f.is_open());
    return json::parse(f);
}

// ────────────────────────────────────────────────────────────────────
// Module Type Coverage Tests
// ────────────────────────────────────────────────────────────────────

TEST_CASE("E2E: Load arbiter_tlm4_test.json — fills ArbiterTLM4 coverage",
          "[e2e][config][new-configs][arbiter]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    auto config = loadConfig("configs/arbiter_tlm4_test.json");
    REQUIRE(factory.instantiateAll(config));
    factory.startAllTicks();

    // Verify ArbiterTLM4 (4-port)
    REQUIRE(factory.getInstance("arb4") != nullptr);
    auto* arb4 = dynamic_cast<ArbiterTLM<4>*>(factory.getInstance("arb4"));
    REQUIRE(arb4 != nullptr);
    REQUIRE(arb4->get_module_type() == "ArbiterTLM4");
    REQUIRE(arb4->num_ports() == 4);

    REQUIRE(factory.getInstance("cpu0")->get_module_type() == "CPUTLM");
    REQUIRE(factory.getInstance("mem0")->get_module_type() == "MemoryTLM");

    uint64_t before = eq.getCurrentCycle();
    eq.run(50);
    REQUIRE(eq.getCurrentCycle() == before + 50);
}

TEST_CASE("E2E: Load link_tlm_chain.json — fills LinkTLM coverage",
          "[e2e][config][new-configs][link]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    auto config = loadConfig("configs/link_tlm_chain.json");
    REQUIRE(factory.instantiateAll(config));
    factory.startAllTicks();

    REQUIRE(factory.getInstance("link0") != nullptr);
    REQUIRE(factory.getInstance("link1") != nullptr);
    REQUIRE(factory.getInstance("link0")->get_module_type() == "LinkTLM");
    REQUIRE(factory.getInstance("link1")->get_module_type() == "LinkTLM");

    uint64_t before = eq.getCurrentCycle();
    eq.run(50);
    REQUIRE(eq.getCurrentCycle() == before + 50);
}

// ────────────────────────────────────────────────────────────────────
// Feature Coverage Tests
// ────────────────────────────────────────────────────────────────────

TEST_CASE("E2E: Load hierarchy_tree_3level.json — tests hierarchy tree parsing",
          "[e2e][config][new-configs][hierarchy]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    auto config = loadConfig("configs/hierarchy_tree_3level.json");
    REQUIRE(factory.instantiateAll(config));

    // Verify hierarchy field is present and parseable
    REQUIRE(config.contains("hierarchy"));
    auto hierarchy_node = parse_hierarchy_tree(config["hierarchy"]);
    REQUIRE(hierarchy_node != nullptr);
    REQUIRE(hierarchy_node->get_name() == "system");

    auto children = hierarchy_node->get_children();
    REQUIRE(children.size() == 3); // 3 top-level clusters

    // Verify first cluster has 2 sub-clusters
    REQUIRE(children[0]->get_children().size() == 2);

    factory.startAllTicks();

    uint64_t before = eq.getCurrentCycle();
    eq.run(50);
    REQUIRE(eq.getCurrentCycle() == before + 50);
}

TEST_CASE("E2E: include_chain_demo.json archived — $include array syntax unsupported",
          "[e2e][config][new-configs][archived]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    auto config = loadConfig("docs-archived/dead-configs-2026-q2/include_chain_demo.json");
    REQUIRE(config.contains("$include"));

    REQUIRE(factory.instantiateAll(config));
    factory.startAllTicks();

    REQUIRE(factory.getInstance("cpu0") != nullptr);
    REQUIRE(factory.getInstance("cpu1") != nullptr);
}

TEST_CASE("E2E: Load vc_priorities_mesh.json — tests vc_priorities feature",
          "[e2e][config][new-configs][vc]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    auto config = loadConfig("configs/vc_priorities_mesh.json");
    REQUIRE(factory.instantiateAll(config));
    factory.startAllTicks();

    // Verify 4 routers instantiated
    REQUIRE(factory.getInstance("router_0_0") != nullptr);
    REQUIRE(factory.getInstance("router_0_1") != nullptr);
    REQUIRE(factory.getInstance("router_1_0") != nullptr);
    REQUIRE(factory.getInstance("router_1_1") != nullptr);

    // Verify vc_priorities field present in connections
    bool has_vc_priorities = false;
    for (const auto& c : config["connections"]) {
        if (c.contains("vc_priorities")) {
            has_vc_priorities = true;
            break;
        }
    }
    REQUIRE(has_vc_priorities);

    uint64_t before = eq.getCurrentCycle();
    eq.run(50);
    REQUIRE(eq.getCurrentCycle() == before + 50);
}

TEST_CASE("E2E: Load wildcard_patterns.json — tests wildcard pattern matching",
          "[e2e][config][new-configs][wildcard]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    auto config = loadConfig("configs/wildcard_patterns.json");
    REQUIRE(factory.instantiateAll(config));
    factory.startAllTicks();

    REQUIRE(factory.getInstance("shared_l1") != nullptr);
    REQUIRE(factory.getInstance("cpus_0") != nullptr);
    REQUIRE(factory.getInstance("cpus_1") != nullptr);
    REQUIRE(factory.getInstance("cpus_2") != nullptr);
    REQUIRE(factory.getInstance("cpus_3") != nullptr);

    // Verify group: prefix in connections
    bool has_group_ref = false;
    for (const auto& c : config["connections"]) {
        if (c["dst"].get<std::string>().find("group:") != std::string::npos) {
            has_group_ref = true;
            break;
        }
    }
    REQUIRE(has_group_ref);
}

TEST_CASE("E2E: Load fan_in_cluster.json — tests fan-in topology (4→1→1)",
          "[e2e][config][new-configs][fan-in]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    auto config = loadConfig("configs/fan_in_cluster.json");
    REQUIRE(factory.instantiateAll(config));
    factory.startAllTicks();

    REQUIRE(factory.getInstance("xbar") != nullptr);
    REQUIRE(factory.getInstance("xbar")->get_module_type() == "CrossbarTLM");
    auto* xbar = dynamic_cast<CrossbarTLM*>(factory.getInstance("xbar"));
    REQUIRE(xbar != nullptr);
    REQUIRE(xbar->num_ports() == 4);

    REQUIRE(factory.getInstance("mem") != nullptr);
    REQUIRE(factory.getInstance("mem")->get_module_type() == "MemoryTLM");

    uint64_t before = eq.getCurrentCycle();
    eq.run(50);
    REQUIRE(eq.getCurrentCycle() == before + 50);
}

// ────────────────────────────────────────────────────────────────────
// Bulk Verification (parametrized for all 7 new configs)
// ────────────────────────────────────────────────────────────────────

TEST_CASE("E2E: Bulk load all 7 new configs — instantiateAll returns true",
          "[e2e][config][new-configs][bulk]") {
    const std::vector<std::string> configs_to_test = {
        "configs/arbiter_tlm4_test.json",
        "configs/link_tlm_chain.json",
        "configs/hierarchy_tree_3level.json",
        "docs-archived/dead-configs-2026-q2/include_chain_demo.json",
        "configs/vc_priorities_mesh.json",
        "configs/wildcard_patterns.json",
        "configs/fan_in_cluster.json",
    };

    for (const auto& cfg_path : configs_to_test) {
        EventQueue eq;
        REGISTER_CHSTREAM;
        ModuleFactory factory(&eq);

        auto config = loadConfig(cfg_path);
        INFO("Config: " << cfg_path);
        REQUIRE(factory.instantiateAll(config));
    }
}
