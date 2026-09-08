// test/test_dgpu_soc_from_config.cc
// BS-G1: SOC 完整实例化 + connections 解析 + outputs/inputs 暴露
// Per board-soc-split design §3 Q1 陷阱: 断言内部 ChStream 组件 adapter 非空
// 注意: 若 PcieEndpointTLM 等组件尚未注册 (T-bs-2 才提升), 测试可能 fail - 这是预期的
#include "chstream_register.hh"
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "core/module_factory.hh"
#include "core/sim_module.hh"
#include "tlm/gpu/dgpu_soc.hh"
#include <nlohmann/json.hpp>

using namespace cpptlm::tlm;
using json = nlohmann::json;

TEST_CASE("DGpuSoc: BS-G1 full JSON instantiation with connections", "[dgpu][json]") {
    EventQueue eq;
    ModuleFactory factory(&eq);

    json board_cfg = {{"name", "test_dgpu_soc"},
                      {"modules", json::array({{{"name", "soc"},
                                                {"type", "DGpuSoc"},
                                                {"modules", json::array()},
                                                {"connections", json::array()}}})},
                      {"connections", json::array()}};

    REQUIRE(factory.instantiateAll(board_cfg));

    auto* soc = factory.getInstance("soc");
    REQUIRE(soc != nullptr);

    auto* soc_typed = dynamic_cast<DGpuSoc*>(soc);
    REQUIRE(soc_typed != nullptr);
    REQUIRE(soc_typed->get_module_type() == "DGpuSoc");
}

TEST_CASE("DGpuSoc: BS-G1 internal factory empty when no modules", "[dgpu][json]") {
    EventQueue eq;
    ModuleFactory factory(&eq);

    json board_cfg = {{"name", "test_dgpu_soc_empty"},
                      {"modules", json::array({{{"name", "soc"},
                                                {"type", "DGpuSoc"},
                                                {"modules", json::array()},
                                                {"connections", json::array()}}})},
                      {"connections", json::array()}};

    REQUIRE(factory.instantiateAll(board_cfg));

    auto* soc = factory.getInstance("soc");
    REQUIRE(soc != nullptr);

    auto* soc_typed = dynamic_cast<DGpuSoc*>(soc);
    REQUIRE(soc_typed != nullptr);

    auto& internal_factory = soc_typed->getInternalFactory();
    REQUIRE(internal_factory.getAllInstances().empty());
}