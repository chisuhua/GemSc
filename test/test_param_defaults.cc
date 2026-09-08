// test/test_param_defaults.cc
#include "chstream_register.hh"
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "core/module_factory.hh"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

TEST_CASE("T3.1-08a: Default values applied", "[param][phase3]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    json config = json::parse(R"({
        "modules": [
            {"name": "r0", "type": "RouterTLM", "params": {
                "node_x": 0, "node_y": 0, "mesh_x": 1, "mesh_y": 1
            }}
        ],
        "connections": []
    })");

    bool result = factory.instantiateAll(config);
    REQUIRE(result == true);
}

TEST_CASE("T3.1-08c: Missing required parameter fails", "[param][phase3]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
    ModuleFactory factory(&eq);

    json config = json::parse(R"({
        "modules": [
            {"name": "r0", "type": "RouterTLM", "params": {
                "node_x": 0
            }}
        ],
        "connections": []
    })");

    bool result = factory.instantiateAll(config);
    CHECK(result == false);
}