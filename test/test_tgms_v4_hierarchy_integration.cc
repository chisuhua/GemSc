// test/test_tgms_v4_hierarchy_integration.cc
// TGMS v4.0 Phase 4.1: Hierarchy Tree Parser 集成测试
// 功能描述：验证 hierarchy 解析与 ModuleFactory 的端到端集成

#include "chstream_register.hh"
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "core/module_factory.hh"
#include "core/topology_node.hh"
#include "core/topology_parser.hh"
#include "modules.hh"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

static void registerModules() {
    static bool registered = false;
    if (!registered) {
        REGISTER_OBJECT;
        REGISTER_CHSTREAM;
        registered = true;
    }
}

TEST_CASE("TGMS v4.1: Hierarchy tree parsing - simple tree", "[tgms-v4][hierarchy][integration]") {
    EventQueue eq;
    registerModules();
    ModuleFactory factory(&eq);

    json config = R"({
        "hierarchy": {
            "name": "system",
            "children": [
                {"name": "cluster0"},
                {"name": "cluster1"}
            ]
        },
        "modules": [
            {"name": "cache", "type": "CacheTLM"},
            {"name": "mem", "type": "MemoryTLM"}
        ],
        "connections": [
            {"src": "cache", "dst": "mem", "latency": 1}
        ]
    })"_json;

    bool result = factory.instantiateAll(config);
    REQUIRE(result == true);
}

TEST_CASE("TGMS v4.1: Hierarchy tree parsing - nested tree", "[tgms-v4][hierarchy][integration]") {
    EventQueue eq;
    registerModules();
    ModuleFactory factory(&eq);

    json config = R"({
        "hierarchy": {
            "name": "system",
            "children": [
                {
                    "name": "cluster0",
                    "children": [
                        {"name": "cpu0"},
                        {"name": "cpu1"}
                    ]
                },
                {
                    "name": "cluster1",
                    "children": [
                        {"name": "cpu2"},
                        {"name": "cpu3"}
                    ]
                }
            ]
        },
        "modules": [],
        "connections": []
    })"_json;

    bool result = factory.instantiateAll(config);
    REQUIRE(result == true);
}

TEST_CASE("TGMS v4.1: Hierarchy with coherence_domains",
          "[tgms-v4][hierarchy][coherence][integration]") {
    EventQueue eq;
    registerModules();
    ModuleFactory factory(&eq);

    json config = R"({
        "hierarchy": {
            "name": "system",
            "children": [
                {
                    "name": "cluster0",
                    "children": [
                        {"name": "cpu0", "coherence_domain": "L2_cache"},
                        {"name": "cpu1", "coherence_domain": "L2_cache"}
                    ]
                },
                {
                    "name": "cluster1",
                    "children": [
                        {"name": "cpu2", "coherence_domain": "L3_cache"},
                        {"name": "cpu3", "coherence_domain": "L3_cache"}
                    ]
                }
            ]
        },
        "coherence_domains": ["L2_cache", "L3_cache"],
        "modules": [
            {"name": "cache", "type": "CacheTLM"},
            {"name": "xbar", "type": "CrossbarTLM"},
            {"name": "mem", "type": "MemoryTLM"}
        ],
        "connections": [
            {"src": "cache", "dst": "xbar.0", "latency": 1},
            {"src": "xbar.0", "dst": "mem", "latency": 2}
        ]
    })"_json;

    bool result = factory.instantiateAll(config);
    REQUIRE(result == true);
}

TEST_CASE("TGMS v4.1: No hierarchy section - should still work",
          "[tgms-v4][hierarchy][integration]") {
    EventQueue eq;
    registerModules();
    ModuleFactory factory(&eq);

    json config = R"({
        "modules": [
            {"name": "cache", "type": "CacheTLM"},
            {"name": "mem", "type": "MemoryTLM"}
        ],
        "connections": [
            {"src": "cache", "dst": "mem", "latency": 1}
        ]
    })"_json;

    bool result = factory.instantiateAll(config);
    REQUIRE(result == true);
}

TEST_CASE("TGMS v4.1: Circular reference detection", "[tgms-v4][hierarchy][error][integration]") {
    EventQueue eq;
    registerModules();
    ModuleFactory factory(&eq);

    json config = R"({
        "hierarchy": {
            "name": "node1",
            "children": [
                {"name": "node2", "children": [
                    {"name": "node1"}
                ]}
            ]
        },
        "modules": [],
        "connections": []
    })"_json;

    bool result = factory.instantiateAll(config);
    REQUIRE(result == false);
}

TEST_CASE("TGMS v4.1: Missing name field in hierarchy",
          "[tgms-v4][hierarchy][error][integration]") {
    EventQueue eq;
    registerModules();
    ModuleFactory factory(&eq);

    json config = R"({
        "hierarchy": {
            "children": []
        },
        "modules": [],
        "connections": []
    })"_json;

    bool result = factory.instantiateAll(config);
    REQUIRE(result == false);
}

TEST_CASE("TGMS v4.1: parse_hierarchy_tree directly", "[tgms-v4][hierarchy][unit]") {
    json hierarchy = json::parse(R"({
        "name": "system",
        "children": [
            {"name": "cpu0"},
            {"name": "cpu1"}
        ]
    })");

    auto root = cpptlm::parse_hierarchy_tree(hierarchy);
    REQUIRE(root != nullptr);
    REQUIRE(root->get_name() == "system");
    REQUIRE(root->get_children().size() == 2);
}

TEST_CASE("TGMS v4.1: TopologyNode parent-child relationship", "[tgms-v4][hierarchy][unit]") {
    auto parent = std::make_shared<cpptlm::TopologyNode>("parent");
    auto child = std::make_shared<cpptlm::TopologyNode>("child");
    parent->add_child(child);

    REQUIRE(child->get_parent() == "parent");
    REQUIRE(parent->get_children().size() == 1);
    REQUIRE(parent->get_children()[0]->get_name() == "child");
}

TEST_CASE("TGMS v4.1: parse_coherence_domains_array", "[tgms-v4][coherence][unit]") {
    json coherence = json::parse(R"(["domain0", "domain1", "domain2"])");
    auto domains = cpptlm::parse_coherence_domains_array(coherence);

    REQUIRE(domains.size() == 3);
    REQUIRE(domains[0] == "domain0");
    REQUIRE(domains[1] == "domain1");
    REQUIRE(domains[2] == "domain2");
}

TEST_CASE("TGMS v4.1: Hierarchy + Cache→Crossbar→Memory integration",
          "[tgms-v4][hierarchy][phase6][integration]") {
    EventQueue eq;
    registerModules();
    ModuleFactory factory(&eq);

    json config = R"({
        "hierarchy": {
            "name": "soc",
            "children": [
                {
                    "name": "cluster0",
                    "children": [
                        {"name": "cache0"},
                        {"name": "cache1"}
                    ]
                },
                {
                    "name": "fabric",
                    "children": [
                        {"name": "xbar"}
                    ]
                },
                {
                    "name": "memory_subsystem",
                    "children": [
                        {"name": "mem0"},
                        {"name": "mem1"}
                    ]
                }
            ]
        },
        "coherence_domains": ["L2_cache", "memory"],
        "modules": [
            {"name": "cache0", "type": "CacheTLM"},
            {"name": "cache1", "type": "CacheTLM"},
            {"name": "xbar", "type": "CrossbarTLM"},
            {"name": "mem0", "type": "MemoryTLM"},
            {"name": "mem1", "type": "MemoryTLM"}
        ],
        "connections": [
            {"src": "cache0", "dst": "xbar.0", "latency": 1},
            {"src": "cache1", "dst": "xbar.1", "latency": 1},
            {"src": "xbar.0", "dst": "mem0", "latency": 2},
            {"src": "xbar.1", "dst": "mem1", "latency": 2}
        ]
    })"_json;

    bool result = factory.instantiateAll(config);
    REQUIRE(result == true);

    auto* cache0 = factory.getInstance("cache0");
    auto* cache1 = factory.getInstance("cache1");
    auto* xbar = factory.getInstance("xbar");
    auto* mem0 = factory.getInstance("mem0");
    auto* mem1 = factory.getInstance("mem1");

    REQUIRE(cache0 != nullptr);
    REQUIRE(cache1 != nullptr);
    REQUIRE(xbar != nullptr);
    REQUIRE(mem0 != nullptr);
    REQUIRE(mem1 != nullptr);

    factory.startAllTicks();
    eq.run(10);
    REQUIRE(eq.getCurrentCycle() == 10);
}