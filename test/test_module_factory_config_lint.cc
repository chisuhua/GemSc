/**
 * @file test_module_factory_config_lint.cc
 * @brief LINT005 rule tests — `config` field semantic enforcement
 *
 * 验证 openspec/changes/field-name-unification 中定义的 LINT005 规则：
 * - `params` 是模块参数的规范字段 (JSON object)
 * - `config` 保留作为外部配置文件路径 (JSON string)
 * - 把参数 dict 错放在 `config` 字段会触发 LINT005 错误
 *
 * ## Test Cases
 * 1. LINT005a: `"config": {dict}` 模块触发 LINT005 → validateConfig 失败
 * 2. LINT005b: `"config": "path/to/file.json"` 通过 lint (合法文件路径)
 * 3. LINT005c: `"params": {dict}` 通过 lint (主路径)
 * 4. LINT005d: 都不存在 → 通过 lint (允许默认配置)
 * 5. LINT005e: 迁移后的 `stress_full_system.json` 加载成功 + 4 个 cpu 模块可访问
 *
 * @author CppTLM Team
 * @date 2026-06-17
 * @see openspec/changes/field-name-unification
 * @see src/core/module_factory_validate.cc:validateConfig()
 */

#include "chstream_register.hh"
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "core/module_factory.hh"
#include "tlm/traffic_gen_tlm.hh"

#include <filesystem>
#include <fstream>
#include <string>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// CPPTLM_SOURCE_DIR 由 test/CMakeLists.txt 注入 (项目根目录)
// 用于定位 configs/ 目录中的真实测试 fixture

// =============================================================================
// Case 1: config-as-dict 触发 LINT005
// =============================================================================

TEST_CASE("LINT005a: 'config' as object triggers LINT005 error", "[config-lint][lint005]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    // 用户把参数 dict 错放在 'config' 字段 (而非 'params')
    json config = R"({
        "modules": [
            {
                "name": "tg_bad",
                "type": "TrafficGenTLM",
                "config": {
                    "pattern": "SEQUENTIAL",
                    "num_requests": 100,
                    "start_addr": "0x1000",
                    "end_addr": "0x2000"
                }
            }
        ],
        "connections": []
    })"_json;

    bool result = factory.instantiateAll(config);
    CHECK(result == false);
    // 模块未实例化
    CHECK(factory.getInstance("tg_bad") == nullptr);
}

// =============================================================================
// Case 2: config-as-string 通过 LINT005
// =============================================================================

TEST_CASE("LINT005b: 'config' as file path string passes lint", "[config-lint][lint005]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    // 合法用法: 'config' 是外部配置文件路径 (string)
    // 使用 SimObject (TrafficGenTLM) 而非 SimModule, 因为 SimModule 类型
    // 需要 REGISTER_MODULE 在测试范围注册（v2.2+ REGISTER_MODULE 仍由 CpuCluster 触发）。
    // LINT005 检查是 type-based (config 必须是 string), 不依赖模块是 SimObject 还是 SimModule.
    // 对 SimObject 而言, 即便 'config' 是 string, 文件加载代码路径 (在 module_instances 中查找)
    // 也找不到实例, 故不会尝试加载文件 → 不会因文件不存在而失败。
    json config = R"({
        "modules": [
            {
                "name": "tg_with_config_path",
                "type": "TrafficGenTLM",
                "config": "/tmp/cpputlm_nonexistent_fixture.json"
            }
        ],
        "connections": []
    })"_json;

    bool result = factory.instantiateAll(config);
    // LINT005 不应触发 (config 是 string)
    CHECK(result == true);
    CHECK(factory.getInstance("tg_with_config_path") != nullptr);
}

// =============================================================================
// Case 3: params-only 通过 LINT005
// =============================================================================

TEST_CASE("LINT005c: 'params' as object passes lint (main path)", "[config-lint][lint005]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    // 规范用法: 'params' 字段持有参数 dict
    json config = R"({
        "modules": [
            {
                "name": "tg_good",
                "type": "TrafficGenTLM",
                "params": {
                    "pattern": "SEQUENTIAL",
                    "num_requests": 50,
                    "start_addr": "0x1000",
                    "end_addr": "0x2000"
                }
            }
        ],
        "connections": []
    })"_json;

    bool result = factory.instantiateAll(config);
    CHECK(result == true);
    CHECK(factory.getInstance("tg_good") != nullptr);

    // 验证实例类型正确
    auto* tg = dynamic_cast<TrafficGenTLM*>(factory.getInstance("tg_good"));
    REQUIRE(tg != nullptr);
}

// =============================================================================
// Case 4: 都不存在 → 通过 lint
// =============================================================================

TEST_CASE("LINT005d: neither 'params' nor 'config' passes lint (defaults OK)",
          "[config-lint][lint005]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    // 不指定任何参数 → 使用模块默认
    json config = R"({
        "modules": [
            {"name": "tg_default", "type": "TrafficGenTLM"}
        ],
        "connections": []
    })"_json;

    bool result = factory.instantiateAll(config);
    CHECK(result == true);
    CHECK(factory.getInstance("tg_default") != nullptr);
}

// =============================================================================
// Case 5: 迁移后的 stress_full_system.json 加载 + 模块可访问
// =============================================================================

TEST_CASE("LINT005e: Migrated stress_full_system.json loads all 4 cpu modules",
          "[config-lint][lint005][migration]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    // 从项目根目录加载迁移后的 fixture
    const std::string config_path =
        std::string(CPPTLM_SOURCE_DIR) + "/configs/stress_full_system.json";

    REQUIRE(std::filesystem::exists(config_path));

    std::ifstream f(config_path);
    REQUIRE(f.is_open());
    json config = json::parse(f);

    // 迁移后所有 TrafficGenTLM 都用 'params' (LINT005 兼容)
    bool result = factory.instantiateAll(config);
    CHECK(result == true);

    // 4 个 cpu 模块 (cpu0-cpu3) 都应可访问
    for (const char* name : {"cpu0", "cpu1", "cpu2", "cpu3"}) {
        auto* mod = factory.getInstance(name);
        CHECK(mod != nullptr);
        auto* tg = dynamic_cast<TrafficGenTLM*>(mod);
        REQUIRE(tg != nullptr);
    }

    // 其他模块 (l1_*, xbar, mem*) 也都应可访问
    CHECK(factory.getInstance("xbar") != nullptr);
    CHECK(factory.getInstance("mem0") != nullptr);
    CHECK(factory.getInstance("l1_0") != nullptr);
}
