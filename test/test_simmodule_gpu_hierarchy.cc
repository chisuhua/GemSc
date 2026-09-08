// test/test_simmodule_gpu_hierarchy.cc
// GPU 4 类集成测试
// 验证 ComputeCluster / TpcCluster / GpcCluster / GpuCluster
// 参考: docs/superpowers/specs/2026-06-19-simmodule-complex-hierarchies-design.md §7.2
// 作者: Sisyphus / 日期: 2026-06-19
#include <string>
#include "chstream_register.hh"
#include "core/module_factory.hh"
#include "core/sim_module.hh"
#include "modules.hh"         // P2-T2.4: 提供 REGISTER_MODULE 宏 (modules_cluster.hh 依赖)
#include "modules_cluster.hh" // 5 个 REGISTER_MODULE
#include "tlm/cluster/compute_cluster.hh"
#include "tlm/cluster/gpc_cluster.hh"
#include "tlm/cluster/gpu_cluster.hh"
#include "tlm/cluster/tpc_cluster.hh"
#include "utils/json_includer.hh"
#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

TEST_CASE("ComputeCluster loads cu_template and instantiates N copies", "[simmodule][gpu]") {
    EventQueue eq;
    auto* cluster = new ComputeCluster("compute_grp", &eq);
    json params = {{"cu_template", "configs/templates/compute_unit_v1.json"}, {"cu_count", 4}};
    cluster->set_config(params);
    REQUIRE(cluster->get_module_type() == "ComputeCluster");
    cluster->simulate_instantiate({});

    REQUIRE(cluster->getInternalInstance("cu0") != nullptr);
    REQUIRE(cluster->getInternalInstance("cu3") != nullptr);
    auto* cu0 = dynamic_cast<SimModule*>(cluster->getInternalInstance("cu0"));
    REQUIRE(cu0 != nullptr);
    REQUIRE(cu0->getInternalFactory().getAllInstances().size() == 2);
    delete cluster;
}

TEST_CASE("TpcCluster contains 1 ComputeCluster with 2 CUs", "[simmodule][gpu]") {
    EventQueue eq;
    auto* tpc = new TpcCluster("tpc0", &eq);
    json params = {{"tpc_id", 0},
                   {"cu_per_tpc", 2},
                   {"cu_template", "configs/templates/compute_unit_v1.json"}};
    tpc->set_config(params);
    REQUIRE(tpc->get_module_type() == "TpcCluster");
    tpc->simulate_instantiate({});
    REQUIRE(tpc->getInternalInstance("compute_grp") != nullptr);
    delete tpc;
}

TEST_CASE("GpcCluster contains M TpcClusters", "[simmodule][gpu]") {
    EventQueue eq;
    auto* gpc = new GpcCluster("gpc0", &eq);
    json params = {{"gpc_id", 0},
                   {"tpc_per_gpc", 2},
                   {"cu_per_tpc", 2},
                   {"cu_template", "configs/templates/compute_unit_v1.json"}};
    gpc->set_config(params);
    REQUIRE(gpc->get_module_type() == "GpcCluster");
    gpc->simulate_instantiate({});
    REQUIRE(gpc->getInternalInstance("tpc0") != nullptr);
    REQUIRE(gpc->getInternalInstance("tpc1") != nullptr);
    delete gpc;
}

TEST_CASE("ComputeCluster clamps cu_count to [1, 64]", "[simmodule][gpu][boundary]") {
    EventQueue eq;
    auto* cluster = new ComputeCluster("c", &eq);
    // cu_count=100 -> 应钳到 64 (cu0..cu63 存在, cu64 不存在)
    cluster->set_config(
        {{"cu_template", "configs/templates/compute_unit_v1.json"}, {"cu_count", 100}});
    cluster->simulate_instantiate({});
    REQUIRE(cluster->getInternalInstance("cu63") != nullptr);
    REQUIRE(cluster->getInternalInstance("cu64") == nullptr);
    delete cluster;
}

TEST_CASE("ComputeCluster without cu_template is silent no-op", "[simmodule][gpu][boundary]") {
    // 不提供 cu_template -> simulate_instantiate 后 internal_factory 为空
    EventQueue eq;
    auto* cluster = new ComputeCluster("c", &eq);
    cluster->set_config({{"cu_count", 5}}); // 缺 cu_template
    cluster->simulate_instantiate({});
    REQUIRE(cluster->getInternalFactory().getAllInstances().empty());
    delete cluster;
}

TEST_CASE("ComputeCluster get_module_type returns ComputeCluster", "[simmodule][gpu]") {
    EventQueue eq;
    auto* cluster = new ComputeCluster("c", &eq);
    REQUIRE(cluster->get_module_type() == "ComputeCluster");
    delete cluster;
}

TEST_CASE("GpuCluster direct API path (no JSON)", "[simmodule][gpu][api]") {
    EventQueue eq;
    auto* gpu = new GpuCluster("g", &eq);
    gpu->set_config({{"gpc_count", 2},
                     {"tpc_per_gpc", 1},
                     {"cu_per_tpc", 1},
                     {"cu_template", "configs/templates/compute_unit_v1.json"}});
    gpu->simulate_instantiate({});
    REQUIRE(gpu->getInternalInstance("gpc0") != nullptr);
    REQUIRE(gpu->getInternalInstance("gpc1") != nullptr);
    REQUIRE(gpu->get_module_type() == "GpuCluster");
    delete gpu;
}

TEST_CASE("GpuCluster full APU-2GPC-2TPC-2CU runs E2E", "[simmodule][gpu][e2e]") {
    // 使用 JSON config 端到端测试
    const std::string config_path = "configs/gpu_2gpc_2tpc_2cu.json";
    auto config = JsonIncluder::loadAndInclude(config_path);
    EventQueue eq;
    ModuleFactory factory(&eq);
    REQUIRE_NOTHROW(factory.instantiateAll(config));
    auto* gpu = dynamic_cast<SimModule*>(factory.getInstance("gpu"));
    REQUIRE(gpu != nullptr);
    REQUIRE(gpu->get_module_type() == "GpuCluster");
    // 验证 2 GPC × 2 TPC × 2 CU × 2 子 = 16 个 leaf module
    int total = 0;
    std::function<void(SimModule*)> count = [&](SimModule* m) {
        for (auto& [n, obj] : m->getInternalFactory().getAllInstances()) {
            if (auto* sub = dynamic_cast<SimModule*>(obj))
                count(sub);
            else
                ++total;
        }
    };
    count(gpu);
    REQUIRE(total == 2 * 2 * 2 * 2); // 16
}
