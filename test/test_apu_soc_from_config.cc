// test/test_apu_soc_from_config.cc
// 端到端测试：从 configs/ 加载 APU SoC 拓扑（Phase 7.A/7.B）并运行仿真
// 对应 docs/soc_arch/specs/apu-soc-design.md §2.2 (最终态) 和 §2.3 (阶段递进)
// 标签：[e2e][apu][from-config]
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

static json loadConfig(const std::string& path) {
    std::string full_path = std::string(CPPTLM_SOURCE_DIR) + "/" + path;
    std::ifstream f(full_path);
    REQUIRE(f.is_open());
    return json::parse(f);
}

// ────────────────────────────────────────────────────────────────────
// Phase 7.A: GPUTLM (ComputeUnit v0) + MemoryTLM direct
// ────────────────────────────────────────────────────────────────────

TEST_CASE("E2E: Load apu_soc_phase7a.json — Phase 7.A minimal APU",
          "[e2e][apu][from-config][phase7a]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    auto config = loadConfig("configs/apu_soc_phase7a.json");
    REQUIRE(factory.instantiateAll(config));
    factory.startAllTicks();

    REQUIRE(factory.getInstance("gpu_unit_0") != nullptr);
    REQUIRE(factory.getInstance("hbm_memory") != nullptr);
    REQUIRE(factory.getInstance("gpu_unit_0")->get_module_type() == "GPUTLM");
    REQUIRE(factory.getInstance("hbm_memory")->get_module_type() == "MemoryTLM");

    // Verify hierarchy tree parses (3-level per apu-soc-design.md §2.3)
    REQUIRE(config.contains("hierarchy"));
    auto root = parse_hierarchy_tree(config["hierarchy"]);
    REQUIRE(root != nullptr);
    REQUIRE(root->get_name() == "apu_phase7a");
    REQUIRE(root->get_children().size() == 2);

    uint64_t before = eq.getCurrentCycle();
    eq.run(50);
    REQUIRE(eq.getCurrentCycle() == before + 50);
}

// ────────────────────────────────────────────────────────────────────
// Phase 7.B: 2 CPU + Cache hierarchy + GPUTLM + Crossbar + Memory
// ────────────────────────────────────────────────────────────────────

TEST_CASE("E2E: Load apu_soc_phase7b.json — Phase 7.B with CPU+Cache+CU",
          "[e2e][apu][from-config][phase7b]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    auto config = loadConfig("configs/apu_soc_phase7b.json");
    REQUIRE(factory.instantiateAll(config));
    factory.startAllTicks();

    // 2 CPU + 2 L1 + 1 L2 + 1 GPUTLM + 1 Crossbar + 1 Memory = 8 modules
    REQUIRE(factory.getInstance("cpu_0") != nullptr);
    REQUIRE(factory.getInstance("cpu_1") != nullptr);
    REQUIRE(factory.getInstance("l1_cpu_0") != nullptr);
    REQUIRE(factory.getInstance("l1_cpu_1") != nullptr);
    REQUIRE(factory.getInstance("l2_cpu_shared") != nullptr);
    REQUIRE(factory.getInstance("compute_unit_0") != nullptr);
    REQUIRE(factory.getInstance("xbar") != nullptr);
    REQUIRE(factory.getInstance("hbm_memory") != nullptr);

    // Verify types
    REQUIRE(factory.getInstance("cpu_0")->get_module_type() == "TrafficGenTLM");
    REQUIRE(factory.getInstance("l1_cpu_0")->get_module_type() == "CacheTLM");
    REQUIRE(factory.getInstance("compute_unit_0")->get_module_type() == "GPUTLM");
    REQUIRE(factory.getInstance("xbar")->get_module_type() == "CrossbarTLM");
    REQUIRE(factory.getInstance("hbm_memory")->get_module_type() == "MemoryTLM");

    // Verify 4-port CrossbarTLM
    auto* xbar = dynamic_cast<CrossbarTLM*>(factory.getInstance("xbar"));
    REQUIRE(xbar != nullptr);
    REQUIRE(xbar->num_ports() == 4);

    // Verify hierarchy tree (4-level: root →
    // cpu_cluster/compute_cluster/interconnect/memory_cluster)
    REQUIRE(config.contains("hierarchy"));
    auto root = parse_hierarchy_tree(config["hierarchy"]);
    REQUIRE(root != nullptr);
    REQUIRE(root->get_name() == "apu_phase7b");
    REQUIRE(root->get_children().size() == 4);

    // Verify coherence_domains
    REQUIRE(config.contains("coherence_domains"));
    REQUIRE(config["coherence_domains"].size() == 1);
    REQUIRE(config["coherence_domains"][0]["protocol"] == "WRITE_THROUGH_SIMPLIFIED");

    uint64_t before = eq.getCurrentCycle();
    eq.run(50);
    REQUIRE(eq.getCurrentCycle() == before + 50);
}

// ────────────────────────────────────────────────────────────────────
// Phase 7.F: Full APU (aspirational — instantiateAll expected to fail
// because ComputeUnitTLM/CoherentXBarTLM/TCCTLM are NOT yet registered)
// ────────────────────────────────────────────────────────────────────

TEST_CASE("E2E: apu_soc_full.json documents target topology (Phase 7.F aspirational)",
          "[e2e][apu][from-config][phase7f][aspirational]") {
    auto config = loadConfig("configs/apu_soc_full.json");

    // Verify the config documents Phase 7.F target architecture
    REQUIRE(config["name"].get<std::string>().find("Phase 7.F") != std::string::npos);
    REQUIRE(config["description"].get<std::string>().find("ComputeUnitTLM") != std::string::npos);
    REQUIRE(config["description"].get<std::string>().find("CoherentXBarTLM") != std::string::npos);
    REQUIRE(config["description"].get<std::string>().find("TCCTLM") != std::string::npos);

    // 4 ComputeUnit + 4 GPU routers + 2 CPU + 2 L1 + 1 L2 + 1 TCC + 1 CoherentXBar + 1 HBM = 16
    REQUIRE(config["modules"].size() == 16);
    REQUIRE(config["connections"].size() == 15);

    // Verify 4 ComputeUnitTLM (Phase 7.B target)
    int cu_count = 0;
    for (const auto& m : config["modules"]) {
        if (m["type"] == "ComputeUnitTLM")
            ++cu_count;
    }
    REQUIRE(cu_count == 4);

    // Verify 2x2 GPU mesh (4 RouterTLM)
    int router_count = 0;
    for (const auto& m : config["modules"]) {
        if (m["type"] == "RouterTLM")
            ++router_count;
    }
    REQUIRE(router_count == 4);

    // Verify hierarchy tree (4-level)
    REQUIRE(config.contains("hierarchy"));
    auto root = parse_hierarchy_tree(config["hierarchy"]);
    REQUIRE(root != nullptr);
    REQUIRE(root->get_name() == "apu_phase7f");

    // Verify MOESI coherence domain
    REQUIRE(config.contains("coherence_domains"));
    bool has_moesi = false;
    for (const auto& d : config["coherence_domains"]) {
        if (d.value("protocol", "") == "MOESI_AMD_6_STATE") {
            has_moesi = true;
            break;
        }
    }
    REQUIRE(has_moesi);

    // NOTE: We do NOT call factory.instantiateAll(config) here because
    // ComputeUnitTLM/CoherentXBarTLM/TCCTLM are not yet registered.
    // This will be testable after Phase 7.B/C/D implementations.
}

// ────────────────────────────────────────────────────────────────────
// Bulk: verify both Phase 7.A and Phase 7.B configs are valid + loadable
// ────────────────────────────────────────────────────────────────────

TEST_CASE("E2E: Bulk load APU Phase 7.A + 7.B configs — v2.1.0 loadable subset",
          "[e2e][apu][from-config][bulk]") {
    const std::vector<std::string> configs_to_test = {
        "configs/apu_soc_phase7a.json",
        "configs/apu_soc_phase7b.json",
    };

    for (const auto& cfg_path : configs_to_test) {
        EventQueue eq;
        REGISTER_CHSTREAM;
        ModuleFactory factory(&eq);

        auto config = loadConfig(cfg_path);
        INFO("APU Config: " << cfg_path);
        REQUIRE(factory.instantiateAll(config));
    }
}
