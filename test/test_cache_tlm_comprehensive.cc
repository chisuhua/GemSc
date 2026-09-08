#include <cstdint>
#include <cstring>
#include "bundles/bundle_serialization.hh"
#include "bundles/cache_bundles_tlm.hh"
#include "core/event_queue.hh"
#include "tlm/cache_tlm.hh"
#include <catch2/catch_all.hpp>

static auto make_req(uint64_t tid, uint64_t addr, bool wr, uint64_t data = 0, uint8_t size = 8) {
    bundles::CacheReqBundle req;
    req.transaction_id.write(tid);
    req.address.write(addr);
    req.is_write.write(wr ? 1 : 0);
    req.data.write(data);
    req.size.write(size);
    return req;
}

static auto make_resp(uint64_t tid = 0, uint64_t data = 0, bool hit = false, uint8_t err = 0) {
    bundles::CacheRespBundle resp;
    resp.transaction_id.write(tid);
    resp.data.write(data);
    resp.is_hit.write(hit ? 1 : 0);
    resp.error_code.write(err);
    return resp;
}

/* Helper: inject req into adapter, tick cache, extract resp */
static auto req_and_tick(CacheTLM* cache, const bundles::CacheReqBundle& req) {
    cache->req_in().consume(); // clear stale valid
    std::memcpy(&cache->req_in().data(), &req, sizeof(req));
    cache->req_in().set_valid(true); // mark input as valid
    cache->tick();
    auto resp = cache->req_in().data(); // grab data that was consumed
    bundles::CacheRespBundle result;
    if (cache->resp_out().valid()) {
        result = cache->resp_out().data();
    }
    return result;
}

static void set_req_valid(CacheTLM* cache, const bundles::CacheReqBundle& req) {
    cache->req_in().consume();
    std::memcpy(&cache->req_in().data(), &req, sizeof(req));
    cache->req_in().set_valid(true);
}

TEST_CASE("CacheTLM basic read miss", "[tlm][cache][functional]") {
    EventQueue eq;
    CacheTLM cache("cache", &eq);

    set_req_valid(&cache, make_req(1, 0x1000, false));
    cache.tick();

    REQUIRE(cache.resp_out().valid());
    auto resp = cache.resp_out().data();
    REQUIRE(resp.transaction_id.read() == 1);
    REQUIRE(resp.data.read() == 0);
    REQUIRE(resp.is_hit.read() == false);
    REQUIRE(resp.error_code.read() == 0);
}

TEST_CASE("CacheTLM read hit after write", "[tlm][cache][functional]") {
    EventQueue eq;
    CacheTLM cache("cache", &eq);

    // Step 1: Write
    set_req_valid(&cache, make_req(1, 0x1000, true, 0xABCD));
    cache.tick();

    // Step 2: Read same address
    set_req_valid(&cache, make_req(2, 0x1000, false));
    cache.tick();

    REQUIRE(cache.resp_out().valid());
    auto resp = cache.resp_out().data();
    REQUIRE(resp.transaction_id.read() == 2);
    REQUIRE(resp.data.read() == 0xABCD);
    REQUIRE(resp.is_hit.read() == true);
}

TEST_CASE("CacheTLM consecutive writes to same address", "[tlm][cache][functional]") {
    EventQueue eq;
    CacheTLM cache("cache", &eq);

    set_req_valid(&cache, make_req(1, 0x1000, true, 0x1111));
    cache.tick();

    set_req_valid(&cache, make_req(2, 0x1000, true, 0x2222));
    cache.tick();

    set_req_valid(&cache, make_req(3, 0x1000, false));
    cache.tick();

    auto resp = cache.resp_out().data();
    REQUIRE(resp.data.read() == 0x2222);
    REQUIRE(resp.is_hit.read() == true);
}

TEST_CASE("CacheTLM multiple addresses independent", "[tlm][cache][functional]") {
    EventQueue eq;
    CacheTLM cache("cache", &eq);

    set_req_valid(&cache, make_req(1, 0x1000, true, 0xAAAA));
    cache.tick();
    set_req_valid(&cache, make_req(2, 0x2000, true, 0xBBBB));
    cache.tick();
    set_req_valid(&cache, make_req(3, 0x3000, true, 0xCCCC));
    cache.tick();

    // Verify each address
    set_req_valid(&cache, make_req(4, 0x1000, false));
    cache.tick();
    REQUIRE(cache.resp_out().data().data.read() == 0xAAAA);

    set_req_valid(&cache, make_req(5, 0x2000, false));
    cache.tick();
    REQUIRE(cache.resp_out().data().data.read() == 0xBBBB);

    set_req_valid(&cache, make_req(6, 0x3000, false));
    cache.tick();
    REQUIRE(cache.resp_out().data().data.read() == 0xCCCC);
}

TEST_CASE("CacheTLM read miss on different address after write", "[tlm][cache][functional]") {
    EventQueue eq;
    CacheTLM cache("cache", &eq);

    set_req_valid(&cache, make_req(1, 0x1000, true, 0xDDDD));
    cache.tick();

    set_req_valid(&cache, make_req(2, 0x9999, false));
    cache.tick();

    auto resp = cache.resp_out().data();
    REQUIRE(resp.transaction_id.read() == 2);
    REQUIRE(resp.data.read() == 0);
    REQUIRE(resp.is_hit.read() == false);
}

TEST_CASE("CacheTLM write allocates on miss (write-allocate)", "[tlm][cache][behavior]") {
    EventQueue eq;
    CacheTLM cache("cache", &eq);

    // Write to uncached address
    set_req_valid(&cache, make_req(1, 0x4000, true, 0xEEEE));
    cache.tick();

    // Read same address — should be a hit now
    set_req_valid(&cache, make_req(2, 0x4000, false));
    cache.tick();

    auto resp = cache.resp_out().data();
    REQUIRE(resp.is_hit.read() == true);
    REQUIRE(resp.data.read() == 0xEEEE);
}

TEST_CASE("CacheTLM preserves transaction ID across operations", "[tlm][cache][behavior]") {
    EventQueue eq;
    CacheTLM cache("cache", &eq);

    const uint64_t tid = 0xDEADBEEFCAFEBABE;
    set_req_valid(&cache, make_req(tid, 0x1234, false));
    cache.tick();

    REQUIRE(cache.resp_out().data().transaction_id.read() == tid);
}

TEST_CASE("CacheTLM handles zero address", "[tlm][cache][boundary]") {
    EventQueue eq;
    CacheTLM cache("cache", &eq);

    set_req_valid(&cache, make_req(1, 0, true, 0xFFFF));
    cache.tick();

    set_req_valid(&cache, make_req(2, 0, false));
    cache.tick();

    auto resp = cache.resp_out().data();
    REQUIRE(resp.data.read() == 0xFFFF);
    REQUIRE(resp.is_hit.read() == true);
}

TEST_CASE("CacheTLM handles max address", "[tlm][cache][boundary]") {
    EventQueue eq;
    CacheTLM cache("cache", &eq);

    const uint64_t max_addr = UINT64_MAX;
    set_req_valid(&cache, make_req(1, max_addr, true, 0x7777));
    cache.tick();

    set_req_valid(&cache, make_req(2, max_addr, false));
    cache.tick();

    auto resp = cache.resp_out().data();
    REQUIRE(resp.data.read() == 0x7777);
    REQUIRE(resp.is_hit.read() == true);
}

TEST_CASE("CacheTLM reset clears all state", "[tlm][cache][reset]") {
    EventQueue eq;
    CacheTLM cache("cache", &eq);

    // Write some data
    set_req_valid(&cache, make_req(1, 0x1000, true, 0xAAAA));
    cache.tick();
    set_req_valid(&cache, make_req(2, 0x2000, true, 0xBBBB));
    cache.tick();

    ResetConfig cfg;
    cache.do_reset(cfg);

    // Read should miss
    set_req_valid(&cache, make_req(3, 0x1000, false));
    cache.tick();
    REQUIRE(cache.resp_out().data().is_hit.read() == false);

    set_req_valid(&cache, make_req(4, 0x2000, false));
    cache.tick();
    REQUIRE(cache.resp_out().data().is_hit.read() == false);
}

TEST_CASE("CacheTLM invalidates output after consume", "[tlm][cache][handshake]") {
    EventQueue eq;
    CacheTLM cache("cache", &eq);

    set_req_valid(&cache, make_req(1, 0x1000, false));
    cache.tick();
    REQUIRE(cache.resp_out().valid());

    cache.resp_out().clear_valid(); // consumer processed the response
    REQUIRE_FALSE(cache.resp_out().valid());

    // Second tick without new input should not produce stale data
    cache.tick();
    REQUIRE_FALSE(cache.resp_out().valid());
}

TEST_CASE("CacheTLM no input means no output", "[tlm][cache][handshake]") {
    EventQueue eq;
    CacheTLM cache("cache", &eq);

    cache.resp_out().clear_valid();
    cache.req_in().set_valid(false);

    cache.tick();
    REQUIRE_FALSE(cache.resp_out().valid());

    cache.tick();
    REQUIRE_FALSE(cache.resp_out().valid());

    cache.tick();
    REQUIRE_FALSE(cache.resp_out().valid());
}

TEST_CASE("CacheTLM adapter tick delegation", "[tlm][cache][integration]") {
    EventQueue eq;
    CacheTLM cache("cache", &eq);

    REQUIRE(cache.get_adapter() == nullptr);

    // Create mock adapter
    cpptlm::OutputStreamAdapter<bundles::CacheRespBundle> mock_out;
    cpptlm::InputStreamAdapter<bundles::CacheReqBundle> mock_in;
    auto* mock_adapter =
        new cpptlm::StreamAdapter<CacheTLM, bundles::CacheReqBundle, bundles::CacheRespBundle>(
            &cache);

    cache.set_stream_adapter(mock_adapter);
    REQUIRE(cache.get_adapter() == mock_adapter);

    // tick() should call adapter_->tick() — verify no crash
    cache.tick();

    delete mock_adapter;
}

TEST_CASE("CacheTLM size field does not affect caching", "[tlm][cache][behavior]") {
    EventQueue eq;
    CacheTLM cache("cache", &eq);

    // Write with different sizes to same address
    set_req_valid(&cache, make_req(1, 0x1000, true, 0x1111, 4));
    cache.tick();

    set_req_valid(&cache, make_req(2, 0x1000, true, 0x2222, 64));
    cache.tick();

    set_req_valid(&cache, make_req(3, 0x1000, false, 0, 1));
    cache.tick();

    // Last write wins regardless of size
    REQUIRE(cache.resp_out().data().data.read() == 0x2222);
}

TEST_CASE("CacheTLM address granularity (byte-level distinct)", "[tlm][cache][behavior]") {
    EventQueue eq;
    CacheTLM cache("cache", &eq);

    set_req_valid(&cache, make_req(1, 0x1000, true, 0xFFFF));
    cache.tick();

    // Adjacent addresses are NOT the same cache line
    set_req_valid(&cache, make_req(2, 0x1001, false));
    cache.tick();

    REQUIRE(cache.resp_out().data().is_hit.read() == false);
}

TEST_CASE("CacheTLM write data zero is cached", "[tlm][cache][boundary]") {
    EventQueue eq;
    CacheTLM cache("cache", &eq);

    // Write zero
    set_req_valid(&cache, make_req(1, 0x5000, true, 0));
    cache.tick();

    // Read back — should hit and return 0
    set_req_valid(&cache, make_req(2, 0x5000, false));
    cache.tick();

    auto resp = cache.resp_out().data();
    REQUIRE(resp.is_hit.read() == true);
    REQUIRE(resp.data.read() == 0);
}

TEST_CASE("CacheTLM error code is always zero", "[tlm][cache][behavior]") {
    EventQueue eq;
    CacheTLM cache("cache", &eq);

    for (int i = 0; i < 10; i++) {
        set_req_valid(&cache, make_req(i, i * 0x1000, i % 2 == 0, i * 0x100));
        cache.tick();
        REQUIRE(cache.resp_out().data().error_code.read() == 0);
    }
}

TEST_CASE("CacheTLM get_module_type returns CacheTLM", "[tlm][cache][meta]") {
    EventQueue eq;
    CacheTLM cache("my_cache", &eq);
    REQUIRE(cache.get_module_type() == "CacheTLM");
}

TEST_CASE("CacheTLM handles write then immediate read (2-cycle round)", "[tlm][cache][behavior]") {
    EventQueue eq;
    CacheTLM cache("cache", &eq);

    for (int i = 0; i < 20; i++) {
        const uint64_t addr = 0x1000 + i;

        // Write
        set_req_valid(&cache, make_req(i * 2 + 1, addr, true, addr * 7));
        cache.tick();

        // Read
        set_req_valid(&cache, make_req(i * 2 + 2, addr, false));
        cache.tick();

        auto resp = cache.resp_out().data();
        REQUIRE(resp.is_hit.read() == true);
        REQUIRE(resp.data.read() == addr * 7);
    }
}
