// test/test_apu_soc_top.cc
// P5 ApuSoC 顶层 + incorporate_parent 测试
// 验证 ApuSoC 组合 CpuCluster + GpuCluster + MemoryCluster
// 参考: docs/superpowers/specs/2026-06-19-simmodule-complex-hierarchies-design.md §7.5
// 作者: Sisyphus / 日期: 2026-06-19
#include <cstdio>
#include "chstream_register.hh"
#include "core/module_factory.hh"
#include "core/sim_module.hh"
#include "modules.hh"
#include "modules_cluster.hh"
#include "tlm/cluster/apu_soc.hh"
#include "tlm/cluster/cpu_cluster.hh"
#include "tlm/cluster/gpu_cluster.hh"
#include "utils/json_includer.hh"
#include <catch2/catch_all.hpp>

#define CPPTLM_TESTING

namespace {
    auto _register_apu_soc = []() {
        REGISTER_CHSTREAM;
        return 0;
    }();
} // namespace

TEST_CASE("ApuSoC composes CpuCluster + GpuCluster + CrossbarTLM", "[simmodule][apu][e2e]") {
    auto config =
        JsonIncluder::loadAndInclude(std::string(CPPTLM_SOURCE_DIR) + "/configs/apu_soc_v1.json");
    EventQueue eq;
    ModuleFactory factory(&eq);
    REQUIRE_NOTHROW(factory.instantiateAll(config));
    REQUIRE(factory.getInstance("apu_top") != nullptr);
    auto* soc = dynamic_cast<SimModule*>(factory.getInstance("apu_top"));
    REQUIRE(soc != nullptr);
    REQUIRE(soc->getInternalInstance("cpu") != nullptr);
    REQUIRE(soc->getInternalInstance("gpu") != nullptr);
    REQUIRE(soc->getInternalInstance("xbar") != nullptr);
}

TEST_CASE("incorporate_parent hook recurses through hierarchy", "[simmodule][apu]") {
    EventQueue eq;
    auto* apu = new ApuSoC("apu_top", &eq);
    apu->set_config({{"cpu_topology", ""}, {"gpu_topology", ""}});
    auto* gpu = new GpuCluster("gpu", &eq);
    gpu->set_config({{"gpc_count", 1},
                     {"tpc_per_gpc", 1},
                     {"cu_per_tpc", 1},
                     {"cu_template", "configs/templates/compute_unit_v1.json"}});
    gpu->simulate_instantiate({});
    apu->addInternalInstance(gpu);
    apu->incorporate_parent(nullptr);
    REQUIRE(gpu->getInternalInstance("gpc0") != nullptr);
    delete apu;
}

TEST_CASE("[E2E] apu_soc_v1.json runs 1000 cycles without crash", "[simmodule][apu][e2e]") {
    auto config =
        JsonIncluder::loadAndInclude(std::string(CPPTLM_SOURCE_DIR) + "/configs/apu_soc_v1.json");
    EventQueue eq;
    ModuleFactory factory(&eq);
    factory.instantiateAll(config);
    factory.startAllTicks();
    eq.run(1000);
    SUCCEED("1000 cycles completed without crash");
}

// CoherentXBarTLM / CacheTLM 派生自 ChStreamModuleBase (非 SimModule),
// 无 simulate_instantiate() 方法。Case 1/3 用 ApuSoC::simulate_instantiate + JSON
// 触发 ModuleFactory Step 7 (StreamAdapter 注入 + D.1 mirror), 否则
// cache.getInternalOutputPort("req_out") 返回 nullptr。

TEST_CASE("ApuSoC incorporates peer caches into CoherentXBar", "[simmodule][apu][p1]") {
    EventQueue eq;
    auto* apu = new ApuSoC("apu", &eq);

    json cfg = {{"modules", json::array()}};
    cfg["modules"].push_back({{"name", "xbar"}, {"type", "CoherentXBarTLM"}});
    json cpu_cfg = {{"name", "cpu"},
                    {"type", "CpuCluster"},
                    {"modules", json::array()},
                    {"connections", json::array()}};
    cpu_cfg["modules"].push_back({{"name", "cache0"}, {"type", "CacheTLM"}});
    cpu_cfg["modules"].push_back({{"name", "cache1"}, {"type", "CacheTLM"}});
    cfg["modules"].push_back(cpu_cfg);
    apu->simulate_instantiate(cfg);

    auto* xbar = dynamic_cast<CoherentXBarTLM*>(apu->getInternalInstance("xbar"));
    REQUIRE(xbar != nullptr);
    REQUIRE(xbar->peer_count() == 0);

    apu->incorporate_parent(nullptr);
    REQUIRE(xbar->peer_count() == 2);
    delete apu;
}

TEST_CASE("ApuSoC deep-recurses through CpuCluster/GpuCluster", "[simmodule][apu][p1][e2e]") {
    auto config =
        JsonIncluder::loadAndInclude(std::string(CPPTLM_SOURCE_DIR) + "/configs/apu_soc_v1.json");
    EventQueue eq;
    ModuleFactory factory(&eq);
    REQUIRE_NOTHROW(factory.instantiateAll(config));
    auto* soc = dynamic_cast<SimModule*>(factory.getInstance("apu_top"));
    REQUIRE(soc != nullptr);
    auto* xbar = dynamic_cast<CoherentXBarTLM*>(soc->getInternalInstance("xbar"));
    REQUIRE(xbar != nullptr);
    soc->incorporate_parent(nullptr);
    REQUIRE(xbar->peer_count() >= 16);
}

TEST_CASE("incorporate_parent is idempotent", "[simmodule][apu][p1]") {
    EventQueue eq;
    auto* apu = new ApuSoC("apu", &eq);

    json cfg = {{"modules", json::array()}};
    cfg["modules"].push_back({{"name", "xbar"}, {"type", "CoherentXBarTLM"}});
    json cpu_cfg = {{"name", "cpu"},
                    {"type", "CpuCluster"},
                    {"modules", json::array()},
                    {"connections", json::array()}};
    cpu_cfg["modules"].push_back({{"name", "cache0"}, {"type", "CacheTLM"}});
    cfg["modules"].push_back(cpu_cfg);
    apu->simulate_instantiate(cfg);

    auto* xbar = dynamic_cast<CoherentXBarTLM*>(apu->getInternalInstance("xbar"));
    REQUIRE(xbar != nullptr);

    apu->incorporate_parent(nullptr);
    REQUIRE(xbar->peer_count() == 1);

    apu->incorporate_parent(nullptr);
    REQUIRE(xbar->peer_count() == 1);
    delete apu;
}

TEST_CASE("ApuSoC without xbar skips wiring gracefully", "[simmodule][apu][p1]") {
    EventQueue eq;
    auto* apu = new ApuSoC("apu", &eq);

    auto* cache0 = new CacheTLM("cache0", &eq);
    apu->addInternalInstance(cache0);

    REQUIRE_NOTHROW(apu->incorporate_parent(nullptr));
    delete apu;
}
