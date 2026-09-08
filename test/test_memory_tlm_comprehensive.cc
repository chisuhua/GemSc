#include <cstdint>
#include <cstring>
#include "bundles/cache_bundles_tlm.hh"
#include "core/event_queue.hh"
#include "tlm/memory_tlm.hh"
#include <catch2/catch_all.hpp>

static void inject_req(MemoryTLM* mem, uint64_t tid, uint64_t addr, bool wr, uint64_t data = 0) {
    bundles::CacheReqBundle req;
    req.transaction_id.write(tid);
    req.address.write(addr);
    req.is_write.write(wr ? 1 : 0);
    req.data.write(data);
    req.size.write(8);
    mem->req_in().consume();
    std::memcpy(&mem->req_in().data(), &req, sizeof(req));
    mem->req_in().set_valid(true);
}

TEST_CASE("MemoryTLM read returns fixed mock data", "[tlm][memory][functional]") {
    EventQueue eq;
    MemoryTLM mem("mem", &eq);

    inject_req(&mem, 1, 0x1000, false);
    mem.tick();

    REQUIRE(mem.resp_out().valid());
    auto resp = mem.resp_out().data();
    REQUIRE(resp.transaction_id.read() == 1);
    REQUIRE(resp.data.read() == 0xDEADBEEF);
    REQUIRE(resp.is_hit.read() == false);
    REQUIRE(resp.error_code.read() == 0);
}

TEST_CASE("MemoryTLM write returns same mock data", "[tlm][memory][functional]") {
    EventQueue eq;
    MemoryTLM mem("mem", &eq);

    inject_req(&mem, 1, 0x2000, true, 0xFFFF);
    mem.tick();

    REQUIRE(mem.resp_out().valid());
    REQUIRE(mem.resp_out().data().data.read() == 0xDEADBEEF);
}

TEST_CASE("MemoryTLM preserves transaction ID", "[tlm][memory][functional]") {
    EventQueue eq;
    MemoryTLM mem("mem", &eq);

    const uint64_t tid = 0xCAFEBABE12345678ULL;
    inject_req(&mem, tid, 0x3000, false);
    mem.tick();

    REQUIRE(mem.resp_out().data().transaction_id.read() == tid);
}

TEST_CASE("MemoryTLM handles multiple sequential requests", "[tlm][memory][functional]") {
    EventQueue eq;
    MemoryTLM mem("mem", &eq);

    for (int i = 0; i < 10; i++) {
        inject_req(&mem, i + 1, 0x1000 + i, i % 2 == 0);
        mem.tick();
        REQUIRE(mem.resp_out().valid());
        REQUIRE(mem.resp_out().data().transaction_id.read() == static_cast<uint64_t>(i + 1));
        REQUIRE(mem.resp_out().data().data.read() == 0xDEADBEEF);
        mem.resp_out().clear_valid();
    }
}

TEST_CASE("MemoryTLM reset clears state", "[tlm][memory][reset]") {
    EventQueue eq;
    MemoryTLM mem("mem", &eq);

    inject_req(&mem, 1, 0x1000, false);
    mem.tick();
    REQUIRE(mem.resp_out().valid());

    ResetConfig cfg;
    mem.do_reset(cfg);

    REQUIRE_FALSE(mem.resp_out().valid());

    mem.resp_out().clear_valid();
    inject_req(&mem, 2, 0x1000, false);
    mem.tick();
    REQUIRE(mem.resp_out().valid());
    REQUIRE(mem.resp_out().data().data.read() == 0xDEADBEEF);
}

TEST_CASE("MemoryTLM no input produces no output", "[tlm][memory][handshake]") {
    EventQueue eq;
    MemoryTLM mem("mem", &eq);
    mem.resp_out().clear_valid();

    mem.tick();
    REQUIRE_FALSE(mem.resp_out().valid());

    mem.tick();
    REQUIRE_FALSE(mem.resp_out().valid());

    mem.tick();
    REQUIRE_FALSE(mem.resp_out().valid());
}

TEST_CASE("MemoryTLM invalidates output after consume", "[tlm][memory][handshake]") {
    EventQueue eq;
    MemoryTLM mem("mem", &eq);

    inject_req(&mem, 1, 0x1000, false);
    mem.tick();
    REQUIRE(mem.resp_out().valid());

    mem.resp_out().clear_valid();
    mem.tick();
    REQUIRE_FALSE(mem.resp_out().valid());
}

TEST_CASE("MemoryTLM adapter tick delegation", "[tlm][memory][integration]") {
    EventQueue eq;
    MemoryTLM mem("mem", &eq);
    REQUIRE(mem.get_adapter() == nullptr);

    auto* mock_adapter =
        new cpptlm::StreamAdapter<MemoryTLM, bundles::CacheReqBundle, bundles::CacheRespBundle>(
            &mem);
    mem.set_stream_adapter(mock_adapter);
    REQUIRE(mem.get_adapter() == mock_adapter);

    mem.tick();
    delete mock_adapter;
}

TEST_CASE("MemoryTLM get_module_type returns MemoryTLM", "[tlm][memory][meta]") {
    EventQueue eq;
    MemoryTLM mem("test_mem", &eq);
    REQUIRE(mem.get_module_type() == "MemoryTLM");
}

TEST_CASE("MemoryTLM error code always zero", "[tlm][memory][behavior]") {
    EventQueue eq;
    MemoryTLM mem("mem", &eq);

    for (int i = 0; i < 5; i++) {
        inject_req(&mem, i, i * 0x1000, i % 2 == 0);
        mem.tick();
        REQUIRE(mem.resp_out().data().error_code.read() == 0);
        mem.resp_out().clear_valid();
    }
}
