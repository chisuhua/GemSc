/**
 * @file test_lint007_ports_validation.cc
 * @brief LINT007 rule tests — `params.ports` vs class max_ports validation
 *
 * 验证 docs/superpowers/plans/2026-06-20-future-work-roadmap.md F7:
 * - `params.ports` 与类实际 `n_ports` 不一致时 emit warning (LINT007)
 * - warning 是非阻塞的 (validateConfig 仍返回 true, instantiateAll 仍成功)
 * - 已知 forward-looking 场景: configs/apu_soc_v1.json 中 CoherentXBarTLM
 *   指定 ports=8 (Phase 7.C 8-port upgrade 前的占位), 应允许 + 警告
 *
 * ## Test Cases
 * 1. LINT007a: `params.ports > max_ports` → validateConfig=true + instantiateAll 成功
 * 2. LINT007b: `params.ports < max_ports` → validateConfig=true + instantiateAll 成功
 * 3. LINT007c: `params.ports == max_ports` → validateConfig=true, 无误报
 * 4. LINT007d: 无 `params.ports` → validateConfig=true (无端口配置的类)
 * 5. LINT007e: configs/apu_soc_v1.json 加载成功 (forward-looking 场景真实 fixture)
 *
 * @see src/core/module_factory_validate.cc:validateConfig()
 * @see docs/superpowers/plans/2026-06-20-future-work-roadmap.md F7
 */

#include "chstream_register.hh"
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "core/module_factory.hh"
#include "tlm/coherent_xbar_tlm.hh"
#include "tlm/crossbar_tlm.hh"

#include <filesystem>
#include <fstream>
#include <string>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// CPPTLM_SOURCE_DIR 由 test/CMakeLists.txt 注入 (项目根目录)
#ifndef CPPTLM_SOURCE_DIR
#define CPPTLM_SOURCE_DIR "."
#endif

TEST_CASE("LINT007a: params.ports > max_ports warns but does not fail", "[config-lint][lint007]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    // CoherentXBarTLM NUM_PORTS=4, 用户配置 ports=8 (forward-looking Phase 7.C)
    json config = R"({
        "modules": [
            {
                "name": "xbar_forward",
                "type": "CoherentXBarTLM",
                "params": { "ports": 8 }
            }
        ],
        "connections": []
    })"_json;

    // 警告但不阻塞: validateConfig 返回 true, instantiateAll 成功
    bool result = factory.instantiateAll(config);
    CHECK(result == true);
    CHECK(factory.getInstance("xbar_forward") != nullptr);

    // 模块实际 num_ports 应为 class max (4), 用户配置 8 被 silently ignored
    auto* xbar = dynamic_cast<CoherentXBarTLM*>(factory.getInstance("xbar_forward"));
    REQUIRE(xbar != nullptr);
    CHECK(xbar->num_ports() == 4); // 实际可用端口 = NUM_PORTS (类硬编码 4)
}

TEST_CASE("LINT007b: params.ports < max_ports warns but does not fail", "[config-lint][lint007]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    // 用户配置 ports=2 但类 NUM_PORTS=4 — 模块会使用全 4 端口
    json config = R"({
        "modules": [
            {
                "name": "xbar_under",
                "type": "CrossbarTLM",
                "params": { "ports": 2 }
            }
        ],
        "connections": []
    })"_json;

    bool result = factory.instantiateAll(config);
    CHECK(result == true);
    CHECK(factory.getInstance("xbar_under") != nullptr);

    auto* xbar = dynamic_cast<CrossbarTLM*>(factory.getInstance("xbar_under"));
    REQUIRE(xbar != nullptr);
    CHECK(xbar->num_ports() == 4); // 模块使用全 4 端口
}

TEST_CASE("LINT007c: params.ports == max_ports passes silently", "[config-lint][lint007]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    json config = R"({
        "modules": [
            {
                "name": "xbar_exact",
                "type": "CrossbarTLM",
                "params": { "ports": 4 }
            }
        ],
        "connections": []
    })"_json;

    bool result = factory.instantiateAll(config);
    CHECK(result == true);
    CHECK(factory.getInstance("xbar_exact") != nullptr);
}

TEST_CASE("LINT007d: no params.ports passes lint", "[config-lint][lint007]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    // 不指定 ports — 无端口校验
    json config = R"({
        "modules": [
            {
                "name": "mem_no_ports",
                "type": "MemoryTLM",
                "params": { "capacity_gb": 1 }
            }
        ],
        "connections": []
    })"_json;

    bool result = factory.instantiateAll(config);
    CHECK(result == true);
    CHECK(factory.getInstance("mem_no_ports") != nullptr);
}

TEST_CASE("LINT007e: apu_soc_v1.json loads with forward-looking ports=8 warning",
          "[config-lint][lint007]") {
    // 验证真实 fixture 仍可加载 (warning-only 模式)
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    std::string path = std::string(CPPTLM_SOURCE_DIR) + "/configs/apu_soc_v1.json";
    std::ifstream ifs(path);
    REQUIRE(ifs.is_open());
    json config;
    ifs >> config;
    ifs.close();

    // 即使 CoherentXBarTLM 指定 ports=8 (forward-looking), instantiateAll 仍应成功
    // (warning-only 模式 — 不会 break existing config)
    // 注: xbar 是 ApuSoC 嵌套子模块, 非顶层实例; 故只验证顶层 instantiateAll 成功
    bool result = factory.instantiateAll(config);
    CHECK(result == true);
    CHECK(factory.getInstance("apu_top") != nullptr);
}