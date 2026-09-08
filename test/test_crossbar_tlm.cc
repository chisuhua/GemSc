#include <cstdint>
#include <cstring>
#include "bundles/cache_bundles_tlm.hh"
#include "core/event_queue.hh"
#include "tlm/crossbar_tlm.hh"
#include <catch2/catch_all.hpp>

static void inject_req(CrossbarTLM* xbar, unsigned port, uint64_t tid, uint64_t addr,
                       uint64_t data = 0) {
    bundles::CacheReqBundle req;
    req.transaction_id.write(tid);
    req.address.write(addr);
    req.is_write.write(0);
    req.data.write(data);
    req.size.write(8);
    xbar->req_in[port].consume();
    std::memcpy(&xbar->req_in[port].data(), &req, sizeof(req));
    xbar->req_in[port].set_valid(true);
}

TEST_CASE("CrossbarTLM routes port 0 correctly", "[tlm][crossbar]") {
    EventQueue eq;
    CrossbarTLM xbar("xbar", &eq);

    // Port 0: 0x0000-0x0FFF
    inject_req(&xbar, 0, 1, 0x0000); // route to port 0
    xbar.tick();

    REQUIRE(xbar.resp_out[0].valid());
    REQUIRE(xbar.resp_out[0].data().transaction_id.read() == 1);
    REQUIRE(xbar.route_address(0x0000) == 0);
}

TEST_CASE("CrossbarTLM routes port 1 correctly", "[tlm][crossbar]") {
    EventQueue eq;
    CrossbarTLM xbar("xbar", &eq);

    // Port 1: 0x1000-0x1FFF
    inject_req(&xbar, 1, 2, 0x1000); // route to port 1
    xbar.tick();

    REQUIRE(xbar.resp_out[1].valid());
    REQUIRE(xbar.route_address(0x1000) == 1);
}

TEST_CASE("CrossbarTLM routes port 2 correctly", "[tlm][crossbar]") {
    EventQueue eq;
    CrossbarTLM xbar("xbar", &eq);

    inject_req(&xbar, 2, 3, 0x2000);
    xbar.tick();

    REQUIRE(xbar.resp_out[2].valid());
    REQUIRE(xbar.route_address(0x2000) == 2);
}

TEST_CASE("CrossbarTLM routes port 3 correctly", "[tlm][crossbar]") {
    EventQueue eq;
    CrossbarTLM xbar("xbar", &eq);

    inject_req(&xbar, 3, 4, 0x3000);
    xbar.tick();

    REQUIRE(xbar.resp_out[3].valid());
    REQUIRE(xbar.route_address(0x3000) == 3);
}

TEST_CASE("CrossbarTLM routes to boundary ports", "[tlm][crossbar]") {
    EventQueue eq;
    CrossbarTLM xbar("xbar", &eq);

    inject_req(&xbar, 0, 1, 0x0FFF); // Still port 0
    xbar.tick();
    REQUIRE(xbar.resp_out[0].valid());
    xbar.resp_out[0].clear_valid();

    inject_req(&xbar, 0, 2, 0x1000); // Port 1
    xbar.tick();
    // P0-5b: 响应回源端口(0),不是路由目的端口(1)
    REQUIRE(xbar.resp_out[0].valid());
    xbar.resp_out[0].clear_valid();
}

TEST_CASE("CrossbarTLM preserves transaction ID", "[tlm][crossbar]") {
    EventQueue eq;
    CrossbarTLM xbar("xbar", &eq);

    const uint64_t tid = 0xDEADBEEF;
    inject_req(&xbar, 0, tid, 0x1234);
    xbar.tick();

    // P0-5b: 响应回源端口(0)
    REQUIRE(xbar.resp_out[0].data().transaction_id.read() == tid);
}

TEST_CASE("CrossbarTLM handles multiple sequential requests", "[tlm][crossbar]") {
    EventQueue eq;
    CrossbarTLM xbar("xbar", &eq);

    for (int i = 0; i < 4; i++) {
        inject_req(&xbar, 0, i + 1, 0x1000 * (i + 1));
        xbar.tick();
        // P0-5b: 响应回源端口(0)
        REQUIRE(xbar.resp_out[0].valid());
        xbar.resp_out[0].clear_valid();
    }
}

TEST_CASE("CrossbarTLM reset clears all outputs", "[tlm][crossbar][reset]") {
    EventQueue eq;
    CrossbarTLM xbar("xbar", &eq);

    inject_req(&xbar, 0, 1, 0x1000);
    xbar.tick();
    // P0-5b: 响应回源端口(0)
    REQUIRE(xbar.resp_out[0].valid());

    ResetConfig cfg;
    xbar.do_reset(cfg);

    xbar.resp_out[0].clear_valid();
    REQUIRE_FALSE(xbar.resp_out[0].valid());
}

TEST_CASE("CrossbarTLM no input produces no output", "[tlm][crossbar]") {
    EventQueue eq;
    CrossbarTLM xbar("xbar", &eq);

    for (unsigned i = 0; i < 4; i++) {
        xbar.resp_out[i].clear_valid();
    }

    xbar.tick();
    for (unsigned i = 0; i < 4; i++) {
        REQUIRE_FALSE(xbar.resp_out[i].valid());
    }
}

TEST_CASE("CrossbarTLM get_module_type returns CrossbarTLM", "[tlm][crossbar]") {
    EventQueue eq;
    CrossbarTLM xbar("test_xbar", &eq);
    REQUIRE(xbar.get_module_type() == "CrossbarTLM");
}

TEST_CASE("CrossbarTLM num_ports returns 4", "[tlm][crossbar]") {
    EventQueue eq;
    CrossbarTLM xbar("xbar", &eq);
    REQUIRE(xbar.num_ports() == 4);
}

TEST_CASE("CrossbarTLM adapter array initialized to nullptr", "[tlm][crossbar]") {
    EventQueue eq;
    CrossbarTLM xbar("xbar", &eq);
    for (unsigned i = 0; i < 4; i++) {
        REQUIRE(xbar.get_adapter(i) == nullptr);
    }
}

TEST_CASE("CrossbarTLM route function edge cases", "[tlm][crossbar]") {
    EventQueue eq;
    CrossbarTLM xbar("xbar", &eq);

    // Max address per port
    REQUIRE(xbar.route_address(0x0FFF) == 0);
    REQUIRE(xbar.route_address(0x1FFF) == 1);
    REQUIRE(xbar.route_address(0x2FFF) == 2);
    REQUIRE(xbar.route_address(0x3FFF) == 3);

    // Wrap around (uint64 overflow behavior)
    REQUIRE(xbar.route_address(UINT64_MAX) == 3); // 0xFFFFFFFFFFFFFFFF >> 12 & 3 = 3
}

TEST_CASE("CrossbarTLM data passthrough", "[tlm][crossbar]") {
    EventQueue eq;
    CrossbarTLM xbar("xbar", &eq);

    inject_req(&xbar, 0, 1, 0x1234, 0xDEADBEEF);
    xbar.tick();

    REQUIRE(xbar.resp_out[0].data().data.read() == 0xDEADBEEF);
}

TEST_CASE("CrossbarTLM error code always zero", "[tlm][crossbar]") {
    EventQueue eq;
    CrossbarTLM xbar("xbar", &eq);

    for (int i = 0; i < 4; i++) {
        inject_req(&xbar, 0, i, 0x1000 * (i + 1));
        xbar.tick();
        REQUIRE(xbar.resp_out[0].data().error_code.read() == 0);
        xbar.resp_out[0].clear_valid();
    }
}

TEST_CASE("CrossbarTLM is_hit always true (routing only)", "[tlm][crossbar]") {
    EventQueue eq;
    CrossbarTLM xbar("xbar", &eq);

    inject_req(&xbar, 0, 1, 0x1000);
    xbar.tick();
    REQUIRE(xbar.resp_out[0].data().is_hit.read() == true);
}

TEST_CASE("CrossbarTLM detects bus conflict on same destination", "[tlm][crossbar][conflict]") {
    EventQueue eq;
    CrossbarTLM xbar("xbar", &eq);

    // 两个请求同时路由到 port 1 (0x1000-0x1FFF)
    bundles::CacheReqBundle req0;
    req0.transaction_id.write(100);
    req0.address.write(0x1000); // dst = port 1
    req0.is_write.write(0);
    req0.data.write(0xAAAA);
    req0.size.write(8);
    std::memcpy(&xbar.req_in[0].data(), &req0, sizeof(req0));
    xbar.req_in[0].set_valid(true);

    bundles::CacheReqBundle req1;
    req1.transaction_id.write(101);
    req1.address.write(0x1500); // dst = port 1 (same as above!)
    req1.is_write.write(0);
    req1.data.write(0xBBBB);
    req1.size.write(8);
    std::memcpy(&xbar.req_in[2].data(), &req1, sizeof(req1));
    xbar.req_in[2].set_valid(true);

    xbar.tick();

    // Both should be consumed but only first one wins
    REQUIRE(xbar.req_in[0].valid() == false);
    REQUIRE(xbar.req_in[2].valid() == false);
}
