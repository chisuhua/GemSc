// test/test_simulation_communication.cc
// 功能描述：仿真通信验证测试
// 作者：CppTLM Team / 日期：2026-04-14
#include "chstream_register.hh"
#include "bundles/cache_bundles_tlm.hh"
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "core/module_factory.hh"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

TEST_CASE("EventQueue: cycle counter advances deterministically", "[e2e][simulation][eventqueue]") {
    EventQueue eq;

    REQUIRE(eq.getCurrentCycle() == 0);

    eq.run(10);
    REQUIRE(eq.getCurrentCycle() == 10);

    eq.run(5);
    REQUIRE(eq.getCurrentCycle() == 15);
}

TEST_CASE("MemoryTLM: single module communication", "[e2e][simulation][memory]") {
    EventQueue eq;
    REGISTER_CHSTREAM;

    MemoryTLM mem("mem", &eq);

    bundles::CacheReqBundle req;
    req.transaction_id.write(42);
    req.address.write(0x1000);
    req.is_write.write(0);
    req.data.write(0);
    mem.req_in().data() = req;
    mem.req_in().set_valid(true);

    mem.tick();

    REQUIRE(mem.resp_out().valid() == true);
    auto resp = mem.resp_out().data();
    REQUIRE(resp.transaction_id.read() == 42);
    REQUIRE(resp.data.read() == 0xDEADBEEF);
}

TEST_CASE("CacheTLM→MemoryTLM: full chain communication via JSON", "[e2e][simulation][chstream]") {
    EventQueue eq;
    REGISTER_CHSTREAM;
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

    factory.instantiateAll(config);
    factory.startAllTicks();

    auto* cache = dynamic_cast<CacheTLM*>(factory.getInstance("cache"));
    REQUIRE(cache != nullptr);

    bundles::CacheReqBundle req;
    req.transaction_id.write(99);
    req.address.write(0x2000);
    req.is_write.write(0);
    req.data.write(0);
    cache->req_in().data() = req;
    cache->req_in().set_valid(true);

    uint64_t before = eq.getCurrentCycle();
    eq.run(5);
    REQUIRE(eq.getCurrentCycle() == before + 5);

    REQUIRE(cache->resp_out().valid() == true);
    auto resp = cache->resp_out().data();
    REQUIRE(resp.transaction_id.read() == 99);
}

TEST_CASE("CrossbarTLM: routing with communication", "[e2e][simulation][crossbar]") {
    EventQueue eq;
    REGISTER_CHSTREAM;

    CrossbarTLM xbar("xbar", &eq);

    bundles::CacheReqBundle req;
    req.transaction_id.write(7);
    req.address.write(0x1234);
    req.is_write.write(0);
    req.data.write(0);
    xbar.req_in[0].data() = req;
    xbar.req_in[0].set_valid(true);

    xbar.tick();

    unsigned route = xbar.route_address(0x1234);
    REQUIRE(route == 1);

    // P0-5b: 响应回源端口(0)
    REQUIRE(xbar.resp_out[0].valid() == true);
    auto resp = xbar.resp_out[0].data();
    REQUIRE(resp.transaction_id.read() == 7);
    REQUIRE(resp.is_hit.read() == 1);
}
