// test/test_config_schema_validation.cc
// 功能描述：JSON Schema 验证器测试（CFG-08）
// 作者：CppTLM Team / 日期：2026-04-29
// 验证：
// 1. 必需顶层字段检查（modules, connections）
// 2. 模块必需字段检查（name, type）
// 3. RouterTLM 参数检查（node_x, node_y, mesh_x, mesh_y）
// 4. NICTLM 参数检查（node_id）
#include "chstream_register.hh"
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "core/module_factory.hh"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

TEST_CASE("Schema: Missing 'modules' field", "[config][schema]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    json config = json::parse(R"({
        "connections": []
    })");

    bool result = factory.instantiateAll(config);
    REQUIRE(result == false);
}

TEST_CASE("Schema: Missing 'connections' field", "[config][schema]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    json config = json::parse(R"({
        "modules": []
    })");

    bool result = factory.instantiateAll(config);
    REQUIRE(result == false);
}

TEST_CASE("Schema: 'modules' not array", "[config][schema]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    json config = json::parse(R"({
        "modules": "invalid",
        "connections": []
    })");

    bool result = factory.instantiateAll(config);
    REQUIRE(result == false);
}

TEST_CASE("Schema: Module missing 'name'", "[config][schema]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    json config = json::parse(R"({
        "modules": [
            { "type": "CacheTLM" }
        ],
        "connections": []
    })");

    bool result = factory.instantiateAll(config);
    REQUIRE(result == false);
}

TEST_CASE("Schema: Module missing 'type'", "[config][schema]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    json config = json::parse(R"({
        "modules": [
            { "name": "cache" }
        ],
        "connections": []
    })");

    bool result = factory.instantiateAll(config);
    REQUIRE(result == false);
}

TEST_CASE("Schema: RouterTLM missing required params", "[config][schema]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    json config = json::parse(R"({
        "modules": [
            { "name": "router_0_0", "type": "RouterTLM", "params": { "node_x": 0 } }
        ],
        "connections": []
    })");

    bool result = factory.instantiateAll(config);
    REQUIRE(result == false);
}

TEST_CASE("Schema: RouterTLM param not integer", "[config][schema]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    json config = json::parse(R"({
        "modules": [
            { "name": "router_0_0", "type": "RouterTLM", 
              "params": { "node_x": 0, "node_y": 0, "mesh_x": 2.5, "mesh_y": 2 } }
        ],
        "connections": []
    })");

    bool result = factory.instantiateAll(config);
    REQUIRE(result == false);
}

TEST_CASE("Schema: NICTLM missing 'node_id'", "[config][schema]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    json config = json::parse(R"({
        "modules": [
            { "name": "nic_0_0", "type": "NICTLM", "params": { "mesh_x": 2 } }
        ],
        "connections": []
    })");

    bool result = factory.instantiateAll(config);
    REQUIRE(result == false);
}

TEST_CASE("Schema: NICTLM 'node_id' not integer", "[config][schema]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    json config = json::parse(R"({
        "modules": [
            { "name": "nic_0_0", "type": "NICTLM", "params": { "node_id": "invalid" } }
        ],
        "connections": []
    })");

    bool result = factory.instantiateAll(config);
    REQUIRE(result == false);
}

TEST_CASE("Schema: Valid minimal config", "[config][schema]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    json config = json::parse(R"({
        "modules": [
            { "name": "cache", "type": "CacheTLM" }
        ],
        "connections": []
    })");

    bool result = factory.instantiateAll(config);
    REQUIRE(result == true);
}

TEST_CASE("Schema: Valid RouterTLM config", "[config][schema]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    json config = json::parse(R"({
        "modules": [
            { "name": "router_0_0", "type": "RouterTLM", 
              "params": { "node_x": 0, "node_y": 0, "mesh_x": 2, "mesh_y": 2 } }
        ],
        "connections": []
    })");

    bool result = factory.instantiateAll(config);
    REQUIRE(result == true);
}

TEST_CASE("Schema: Valid NICTLM config", "[config][schema]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    json config = json::parse(R"({
        "modules": [
            { "name": "nic_0_0", "type": "NICTLM", "params": { "node_id": 0 } }
        ],
        "connections": []
    })");

    bool result = factory.instantiateAll(config);
    REQUIRE(result == true);
}

TEST_CASE("Schema: RouterTLM missing 'params' section", "[config][schema]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    json config = json::parse(R"({
        "modules": [
            { "name": "router_0_0", "type": "RouterTLM" }
        ],
        "connections": []
    })");

    bool result = factory.instantiateAll(config);
    REQUIRE(result == false);
}

TEST_CASE("Schema: NICTLM missing 'params' section", "[config][schema]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    json config = json::parse(R"({
        "modules": [
            { "name": "nic_0_0", "type": "NICTLM" }
        ],
        "connections": []
    })");

    bool result = factory.instantiateAll(config);
    REQUIRE(result == false);
}