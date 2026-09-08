// test/test_hierarchy_from_config.cc
// 端到端测试：从 configs/ 加载 hierarchy tree 配置并验证解析
// 补充 test/test_tgms_v4_hierarchy_integration.cc 的 inline JSON 路径
// 标签：[e2e][hierarchy][from-config][tgms-v4]
#include <filesystem>
#include <fstream>
#include "chstream_register.hh"
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "core/module_factory.hh"
#include "core/topology_node.hh"
#include "core/topology_parser.hh"
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using cpptlm::parse_hierarchy_tree;
using cpptlm::parse_hierarchy_tree_with_validation;
using cpptlm::TopologyNode;

static json loadConfig(const std::string& path) {
    std::string full_path = std::string(CPPTLM_SOURCE_DIR) + "/" + path;
    std::ifstream f(full_path);
    REQUIRE(f.is_open());
    return json::parse(f);
}

TEST_CASE("E2E: Load hierarchy_tree_3level.json and parse hierarchy",
          "[e2e][hierarchy][from-config][tgms-v4]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    auto config = loadConfig("configs/hierarchy_tree_3level.json");
    REQUIRE(factory.instantiateAll(config));

    // Verify hierarchy structure
    REQUIRE(config.contains("hierarchy"));
    REQUIRE(config["hierarchy"]["name"] == "system");

    auto root = parse_hierarchy_tree(config["hierarchy"]);
    REQUIRE(root != nullptr);
    REQUIRE(root->get_name() == "system");

    // 3 top-level clusters
    auto clusters = root->get_children();
    REQUIRE(clusters.size() == 3);

    // Each cluster has 2 sub-clusters
    for (const auto& cluster : clusters) {
        auto subs = cluster->get_children();
        REQUIRE(subs.size() == 2);
        for (const auto& sub : subs) {
            REQUIRE(sub->get_parent() == cluster->get_name());
        }
    }
}

TEST_CASE("E2E: Load hierarchy_tree_3level.json with coherence validation",
          "[e2e][hierarchy][from-config][tgms-v4][coherence]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    auto config = loadConfig("configs/hierarchy_tree_3level.json");

    // Parse with coherence_domains validation
    auto root = parse_hierarchy_tree_with_validation(
        config["hierarchy"], config.value("coherence_domains", json::array()));
    REQUIRE(root != nullptr);
    REQUIRE(root->get_name() == "system");

    // Verify coherence_domains are present
    REQUIRE(config.contains("coherence_domains"));
    REQUIRE(config["coherence_domains"].size() == 3);

    factory.startAllTicks();
    uint64_t before = eq.getCurrentCycle();
    eq.run(50);
    REQUIRE(eq.getCurrentCycle() == before + 50);
}

TEST_CASE("E2E: Integration — hierarchy config + ModuleFactory instantiateAll",
          "[e2e][hierarchy][from-config][tgms-v4][integration]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    auto config = loadConfig("configs/hierarchy_tree_3level.json");

    // Verify hierarchy tree parses AND modules instantiate together
    auto root = parse_hierarchy_tree(config["hierarchy"]);
    REQUIRE(root != nullptr);
    REQUIRE(factory.instantiateAll(config));
    factory.startAllTicks();

    // 4 modules in hierarchy_tree_3level.json (3 mem + 1 xbar)
    REQUIRE(factory.getInstance("mem_a") != nullptr);
    REQUIRE(factory.getInstance("mem_b") != nullptr);
    REQUIRE(factory.getInstance("mem_c") != nullptr);
    REQUIRE(factory.getInstance("xbar") != nullptr);
}
