// test/test_nic_port_groups.cc
// Phase 3.2 T3.2-08: NICTLM port_groups integration tests

#include "chstream_register.hh"
#include "catch_amalgamated.hpp"
#include "core/module_factory.hh"
#include "core/port_types.hh"
#include "modules.hh"

using json = nlohmann::json;

static void registerAllModules() {
    static bool registered = false;
    if (!registered) {
        REGISTER_OBJECT;
        REGISTER_CHSTREAM;
        registered = true;
    }
}

TEST_CASE("T3.2-08: NICTLM with port_groups in module spec", "[phase3.2][port_groups][nic]") {
    registerAllModules();
    EventQueue eq;
    ModuleFactory factory(&eq);

    json config = R"({
        "modules": [
            {
                "name": "nic0",
                "type": "NICTLM",
                "node_id": 0,
                "mesh_x": 2,
                "mesh_y": 2,
                "port_spec": {
                    "port_groups": [
                        {
                            "name": "pe",
                            "bundle_type": "SINGLE",
                            "ports": [
                                { "index": 0, "role": "target", "bundle": "cache_req" },
                                { "index": 1, "role": "initiator", "bundle": "cache_resp" }
                            ]
                        },
                        {
                            "name": "Net",
                            "bundle_type": "SINGLE",
                            "ports": [
                                { "index": 2, "role": "initiator", "bundle": "noc_flit" },
                                { "index": 3, "role": "target", "bundle": "noc_flit" }
                            ]
                        }
                    ]
                }
            },
            {
                "name": "router0",
                "type": "RouterTLM",
                "node_x": 0,
                "node_y": 0,
                "mesh_x": 2,
                "mesh_y": 2
            }
        ],
        "connections": [
            { "src": "nic0.2", "dst": "router0.0", "latency": 1 },
            { "src": "router0.0", "dst": "nic0.3", "latency": 1 }
        ]
    })"_json;

    bool result = factory.instantiateAll(config);
    REQUIRE(result == true);

    auto* nic = factory.getInstance<tlm::NICTLM>("nic0");
    REQUIRE(nic != nullptr);
    REQUIRE(nic->num_ports() == 4);
}

TEST_CASE("T3.2-08: ModulePortSpec with port_groups JSON roundtrip",
          "[phase3.2][port_groups][port_types]") {
    cpptlm::ModulePortSpec spec;
    spec.module_name = "nic0";

    cpptlm::PortGroupSpec pe_group;
    pe_group.name = "pe";
    pe_group.bundle_type = cpptlm::PortGroupBundleType::SINGLE;
    pe_group.ports = {{0, cpptlm::PortRole::TARGET, cpptlm::BundleType::CACHE_REQ},
                      {1, cpptlm::PortRole::INITIATOR, cpptlm::BundleType::CACHE_RESP}};

    cpptlm::PortGroupSpec net_group;
    net_group.name = "Net";
    net_group.bundle_type = cpptlm::PortGroupBundleType::SINGLE;
    net_group.ports = {{2, cpptlm::PortRole::INITIATOR, cpptlm::BundleType::NOC_FLIT},
                       {3, cpptlm::PortRole::TARGET, cpptlm::BundleType::NOC_FLIT}};

    spec.port_groups = {pe_group, net_group};

    json j = spec;
    auto restored = j.get<cpptlm::ModulePortSpec>();

    REQUIRE(restored.module_name == "nic0");
    REQUIRE(restored.port_groups.size() == 2);
    REQUIRE(restored.port_groups[0].name == "pe");
    REQUIRE(restored.port_groups[1].name == "Net");
    REQUIRE(restored.port_groups[0].ports.size() == 2);
    REQUIRE(restored.port_groups[1].ports.size() == 2);
}

TEST_CASE("T3.2-08: NICTLM dual-port adapter creates 2 logical groups",
          "[phase3.2][port_groups][dualport]") {
    registerAllModules();
    EventQueue eq;
    ModuleFactory factory(&eq);

    json config = R"({
        "modules": [
            { "name": "nic0", "type": "NICTLM", "node_id": 0, "mesh_x": 2, "mesh_y": 2 },
            { "name": "router0", "type": "RouterTLM", "node_x": 0, "node_y": 0, "mesh_x": 2, "mesh_y": 2 }
        ],
        "connections": [
            { "src": "nic0.2", "dst": "router0.0", "latency": 1 }
        ]
    })"_json;

    bool result = factory.instantiateAll(config);
    REQUIRE(result == true);

    auto* nic = factory.getInstance<tlm::NICTLM>("nic0");
    REQUIRE(nic != nullptr);

    auto& req_out = nic->net_req_out();
    auto& resp_in = nic->net_resp_in();
    REQUIRE(req_out.valid() == false);
    REQUIRE(resp_in.valid() == false);
}

TEST_CASE("T3.2-08: PortGroupBundleType enum JSON serialization",
          "[phase3.2][port_groups][port_types]") {
    cpptlm::PortGroupBundleType bt = cpptlm::PortGroupBundleType::BUNDLE_MASTER;
    json j = bt;
    REQUIRE(j.get<std::string>() == "BUNDLE_MASTER");

    auto restored = j.get<cpptlm::PortGroupBundleType>();
    REQUIRE(restored == cpptlm::PortGroupBundleType::BUNDLE_MASTER);
}