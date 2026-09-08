// test/test_json_config_e2e.cc
// 功能描述：基于外部 JSON 配置文件的端到端仿真测试
// 作者：CppTLM Team / 日期：2026-04-14
// 验证：
// 1. 从 configs/ 目录加载 JSON 配置文件
// 2. 通过 ModuleFactory 实例化模块
// 3. 运行仿真并验证 cycle counter 推进
#include <filesystem>
#include <fstream>
#include "chstream_register.hh"
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "core/module_factory.hh"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// 从配置文件加载 JSON
static json loadConfig(const std::string& path) {
    std::string full_path;
    if (std::filesystem::path(path).is_absolute()) {
        full_path = path;
    } else {
        full_path = std::string(CPPTLM_SOURCE_DIR) + "/" + path;
    }
    std::ifstream f(full_path);
    REQUIRE(f.is_open());
    return json::parse(f);
}

TEST_CASE("E2E: Load crossbar_test.json and run simulation", "[e2e][config][chstream]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    auto config = loadConfig("configs/crossbar_test.json");
    factory.instantiateAll(config);
    factory.startAllTicks();

    // 验证模块实例化成功
    REQUIRE(factory.getInstance("cache") != nullptr);
    REQUIRE(factory.getInstance("xbar") != nullptr);
    REQUIRE(factory.getInstance("mem") != nullptr);

    // 验证模块类型
    REQUIRE(factory.getInstance("cache")->get_module_type() == "CacheTLM");
    REQUIRE(factory.getInstance("xbar")->get_module_type() == "CrossbarTLM");
    REQUIRE(factory.getInstance("mem")->get_module_type() == "MemoryTLM");

    // 验证 CrossbarTLM 端口数量
    auto* xbar = dynamic_cast<CrossbarTLM*>(factory.getInstance("xbar"));
    REQUIRE(xbar != nullptr);
    REQUIRE(xbar->num_ports() == 4);

    // 验证 cycle 推进（核心：证明仿真真正运行）
    uint64_t before = eq.getCurrentCycle();
    eq.run(50);
    REQUIRE(eq.getCurrentCycle() == before + 50);
}

TEST_CASE("E2E: Load cache_chstream_test.json and run simulation", "[e2e][config][chstream]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    auto config = loadConfig("configs/cache_chstream_test.json");
    factory.instantiateAll(config);
    factory.startAllTicks();

    // 验证 2 模块实例化成功
    REQUIRE(factory.getInstance("cache") != nullptr);
    REQUIRE(factory.getInstance("mem") != nullptr);

    // 验证 cycle 推进
    uint64_t before = eq.getCurrentCycle();
    eq.run(100);
    REQUIRE(eq.getCurrentCycle() == before + 100);
}

TEST_CASE("E2E: Load cpu_tlm_test.json and run simulation", "[e2e][config][chstream]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    auto config = loadConfig("configs/cpu_tlm_test.json");
    factory.instantiateAll(config);
    factory.startAllTicks();

    // 验证 TLM 模块实例化
    REQUIRE(factory.getInstance("cpu0") != nullptr);
    REQUIRE(factory.getInstance("l1") != nullptr);
    REQUIRE(factory.getInstance("mem") != nullptr);

    // 验证模块类型
    REQUIRE(factory.getInstance("cpu0")->get_module_type() == "CPUTLM");
    REQUIRE(factory.getInstance("l1")->get_module_type() == "CacheTLM");
    REQUIRE(factory.getInstance("mem")->get_module_type() == "MemoryTLM");

    uint64_t before = eq.getCurrentCycle();
    eq.run(50);
    REQUIRE(eq.getCurrentCycle() == before + 50);
}

TEST_CASE("E2E: Load traffic_gen_tlm_test.json and run simulation", "[e2e][config][chstream]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    auto config = loadConfig("configs/traffic_gen_tlm_test.json");
    factory.instantiateAll(config);
    factory.startAllTicks();

    // 验证 TrafficGenTLM 多实例
    REQUIRE(factory.getInstance("tg0") != nullptr);
    REQUIRE(factory.getInstance("tg1") != nullptr);
    REQUIRE(factory.getInstance("l1") != nullptr);
    REQUIRE(factory.getInstance("mem") != nullptr);

    // 验证模块类型
    REQUIRE(factory.getInstance("tg0")->get_module_type() == "TrafficGenTLM");
    REQUIRE(factory.getInstance("tg1")->get_module_type() == "TrafficGenTLM");

    uint64_t before = eq.getCurrentCycle();
    eq.run(50);
    REQUIRE(eq.getCurrentCycle() == before + 50);
}

TEST_CASE("E2E: Load arbiter_tlm_test.json and run simulation", "[e2e][config][chstream]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    auto config = loadConfig("configs/arbiter_tlm_test.json");
    factory.instantiateAll(config);
    factory.startAllTicks();

    REQUIRE(factory.getInstance("cpu0") != nullptr);
    REQUIRE(factory.getInstance("cpu1") != nullptr);
    REQUIRE(factory.getInstance("arb") != nullptr);
    REQUIRE(factory.getInstance("mem") != nullptr);

    auto* arb = dynamic_cast<ArbiterTLM<2>*>(factory.getInstance("arb"));
    REQUIRE(arb != nullptr);
    REQUIRE(arb->get_module_type() == "ArbiterTLM2");
    REQUIRE(arb->num_ports() == 2);

    uint64_t before = eq.getCurrentCycle();
    eq.run(50);
    REQUIRE(eq.getCurrentCycle() == before + 50);
}
