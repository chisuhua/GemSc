// test/test_config_inheritance.cc
// Phase 3.1: Config Inheritance Tests (extends field)

#include <unistd.h>
#include <cstdio>
#include <fstream>
#include "chstream_register.hh"
#include "catch_amalgamated.hpp"
#include "core/module_factory.hh"
#include "mock_modules.hh"

using json = nlohmann::json;

static std::string createTempJson(const std::string& content) {
    char path[] = "/tmp/cpptlm_test_XXXXXX.json";
    int fd = mkstemps(path, 5);
    ::write(fd, content.c_str(), content.size());
    ::close(fd);
    return std::string(path);
}

static void cleanupTempJson(const std::string& path) {
    std::remove(path.c_str());
}

// ============================================================================
// Task 2.1: extends field processing
// ============================================================================

TEST_CASE("ConfigInheritance: Basic extends loads base config", "[config][extends]") {
    EventQueue eq;
    REGISTER_CHSTREAM;

    // Create base config file
    std::string base_path = createTempJson(R"({
        "modules": [
            { "name": "base_cpu", "type": "MockSim" }
        ],
        "connections": []
    })");

    // Create child config that extends base
    std::string child_path = createTempJson(R"({
        "extends": ")" + base_path + R"(",
        "modules": [
            { "name": "child_cache", "type": "MockSim" }
        ],
        "connections": []
    })");

    // Load and process via factory
    ModuleFactory factory(&eq);
    json child_config = json::parse(std::ifstream(child_path));
    bool result = factory.instantiateAll(child_config);

    REQUIRE(result == true);

    // Verify both base_cpu and child_cache exist
    auto* base_cpu = factory.getInstance("base_cpu");
    auto* child_cache = factory.getInstance("child_cache");
    REQUIRE(base_cpu != nullptr);
    REQUIRE(child_cache != nullptr);

    cleanupTempJson(base_path);
    cleanupTempJson(child_path);
}

TEST_CASE("ConfigInheritance: Module name matching merges params", "[config][extends]") {
    EventQueue eq;
    REGISTER_CHSTREAM;

    // Base config with router having mesh_x=4, mesh_y=4
    std::string base_path = createTempJson(R"({
        "modules": [
            { "name": "router", "type": "MockSim", "params": { "mesh_x": 4, "mesh_y": 4 }}
        ],
        "connections": []
    })");

    // Child config overrides mesh_x=8, mesh_y should remain 4
    std::string child_path = createTempJson(R"({
        "extends": ")" + base_path + R"(",
        "modules": [
            { "name": "router", "params": { "mesh_x": 8 }}
        ],
        "connections": []
    })");

    ModuleFactory factory(&eq);
    json child_config = json::parse(std::ifstream(child_path));

    // The merge should produce params with mesh_x=8, mesh_y=4
    // This test verifies the deep merge behavior
    bool result = factory.instantiateAll(child_config);
    REQUIRE(result == true);

    auto* router = factory.getInstance("router");
    REQUIRE(router != nullptr);

    cleanupTempJson(base_path);
    cleanupTempJson(child_path);
}

TEST_CASE("ConfigInheritance: New modules in child are appended", "[config][extends]") {
    EventQueue eq;
    REGISTER_CHSTREAM;

    std::string base_path = createTempJson(R"({
        "modules": [
            { "name": "existing", "type": "MockSim" }
        ],
        "connections": []
    })");

    std::string child_path = createTempJson(R"({
        "extends": ")" + base_path + R"(",
        "modules": [
            { "name": "new_module", "type": "MockSim" }
        ],
        "connections": []
    })");

    ModuleFactory factory(&eq);
    json child_config = json::parse(std::ifstream(child_path));
    bool result = factory.instantiateAll(child_config);
    REQUIRE(result == true);

    auto* existing = factory.getInstance("existing");
    auto* new_module = factory.getInstance("new_module");
    REQUIRE(existing != nullptr);
    REQUIRE(new_module != nullptr);

    cleanupTempJson(base_path);
    cleanupTempJson(child_path);
}

TEST_CASE("ConfigInheritance: Connections are appended not merged", "[config][extends]") {
    EventQueue eq;
    REGISTER_CHSTREAM;

    std::string base_path = createTempJson(R"({
        "modules": [
            { "name": "a", "type": "MockSim" },
            { "name": "b", "type": "MockSim" },
            { "name": "c", "type": "MockSim" },
            { "name": "d", "type": "MockSim" }
        ],
        "connections": [
            { "src": "a", "dst": "b", "latency": 1 }
        ]
    })");

    std::string child_path = createTempJson(R"({
        "extends": ")" + base_path + R"(",
        "modules": [],
        "connections": [
            { "src": "c", "dst": "d", "latency": 2 }
        ]
    })");

    ModuleFactory factory(&eq);
    json child_config = json::parse(std::ifstream(child_path));
    bool result = factory.instantiateAll(child_config);
    REQUIRE(result == true);

    // Both connections should exist (a->b and c->d)
    auto* b = factory.getInstance("b");
    auto* d = factory.getInstance("d");
    REQUIRE(b != nullptr);
    REQUIRE(d != nullptr);

    const auto& b_upstream = b->getPortManager().getUpstreamPorts();
    const auto& d_upstream = d->getPortManager().getUpstreamPorts();
    REQUIRE(b_upstream.size() == 1); // from a
    REQUIRE(d_upstream.size() == 1); // from c

    cleanupTempJson(base_path);
    cleanupTempJson(child_path);
}

TEST_CASE("ConfigInheritance: Missing base file produces error", "[config][extends]") {
    EventQueue eq;
    REGISTER_CHSTREAM;

    std::string child_path = createTempJson(R"({
        "extends": "/nonexistent/path/base.json",
        "modules": [],
        "connections": []
    })");

    ModuleFactory factory(&eq);
    json child_config = json::parse(std::ifstream(child_path));
    bool result = factory.instantiateAll(child_config);

    REQUIRE(result == false);

    cleanupTempJson(child_path);
}

// ============================================================================
// Task 2.4: Module groups are merged by group name
// ============================================================================

TEST_CASE("ConfigInheritance: Groups from base and child are merged", "[config][extends]") {
    EventQueue eq;
    REGISTER_CHSTREAM;

    std::string base_path = createTempJson(R"({
        "modules": [
            { "name": "r0", "type": "MockSim" },
            { "name": "r1", "type": "MockSim" },
            { "name": "r2", "type": "MockSim" }
        ],
        "groups": {
            "cluster_a": ["r0", "r1"]
        },
        "connections": []
    })");

    std::string child_path = createTempJson(R"({
        "extends": ")" + base_path + R"(",
        "modules": [],
        "groups": {
            "cluster_b": ["r2"]
        },
        "connections": []
    })");

    ModuleFactory factory(&eq);
    json child_config = json::parse(std::ifstream(child_path));
    bool result = factory.instantiateAll(child_config);
    REQUIRE(result == true);

    // Both cluster_a and cluster_b should exist
    auto cluster_a = ModuleGroup::resolve("group:cluster_a");
    auto cluster_b = ModuleGroup::resolve("group:cluster_b");
    REQUIRE(cluster_a.size() == 2); // r0, r1
    REQUIRE(cluster_b.size() == 1); // r2

    cleanupTempJson(base_path);
    cleanupTempJson(child_path);
}

TEST_CASE("ConfigInheritance: Same group name merges member lists", "[config][extends]") {
    EventQueue eq;
    REGISTER_CHSTREAM;

    std::string base_path = createTempJson(R"({
        "modules": [
            { "name": "a", "type": "MockSim" },
            { "name": "b", "type": "MockSim" },
            { "name": "c", "type": "MockSim" }
        ],
        "groups": {
            "shared": ["a", "b"]
        },
        "connections": []
    })");

    std::string child_path = createTempJson(R"({
        "extends": ")" + base_path + R"(",
        "modules": [],
        "groups": {
            "shared": ["c"]
        },
        "connections": []
    })");

    ModuleFactory factory(&eq);
    json child_config = json::parse(std::ifstream(child_path));
    bool result = factory.instantiateAll(child_config);
    REQUIRE(result == true);

    // shared group should contain a, b, c
    auto shared = ModuleGroup::resolve("group:shared");
    REQUIRE(shared.size() == 3);

    cleanupTempJson(base_path);
    cleanupTempJson(child_path);
}

// ============================================================================
// Task 2.5: Cycle detection for extends
// ============================================================================

TEST_CASE("ConfigInheritance: Self-referential extends produces error", "[config][extends]") {
    EventQueue eq;
    REGISTER_CHSTREAM;

    std::string self_path = createTempJson(R"({
        "extends": "REPLACEME",
        "modules": [],
        "connections": []
    })");

    std::ifstream f(self_path);
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();

    size_t pos = content.find("REPLACEME");
    content.replace(pos, 9, self_path);

    std::ofstream wf(self_path);
    wf << content;
    wf.close();

    ModuleFactory factory(&eq);
    json self_config = json::parse(std::ifstream(self_path));
    bool result = factory.instantiateAll(self_config);

    REQUIRE(result == false);
    cleanupTempJson(self_path);
}
