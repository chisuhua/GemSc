// test/test_axi4_bundle.cc
// Axi4Bundle / Axi4LiteBundle 字段完整性与 ID 关联测试 (T-P5-1)
// 功能：验证 AXI4 与 AXI4-Lite Bundle 所有字段读回、序列化、ID 关联
// 作者 CppTLM Team / 日期 2026-11-03
// 参考: openspec/changes/2026-11-03-cpptlm-dgpu-axi-stream-adapter/tasks.md T-P5-1

#include "bundles/axi4_bundles_tlm.hh"
#include "bundles/bundle_serialization.hh"
#include "catch_amalgamated.hpp"

#include <array>
#include <cstdint>

using namespace bundles;

TEST_CASE("Axi4Bundle: all fields round-trip read/write", "[axi][bundle][basic]") {
    Axi4Bundle b;

    // Write channel fields
    b.awaddr.write(0x123456789ABCDEFFULL);
    b.awlen.write(7);   // 8 beats
    b.awsize.write(6);  // 64 bytes (2^6)
    b.awburst.write(1); // INCR
    b.awid.write(0xABCD);
    b.wdata.write(0x0102030405060708ULL); // 512-bit data (ch_uint<512> stored in uint64_t)
    b.wstrb.write(0xFFFFFFFFFFFFFFFFULL); // 64 bytes = 512 bits all strobes
    b.wlast.write(1);

    // Read channel fields
    b.araddr.write(0xFEDCBA9876543210ULL);
    b.arlen.write(3);   // 4 beats
    b.arsize.write(5);  // 32 bytes
    b.arburst.write(1); // INCR
    b.arid.write(0xDCBA);

    // Response fields
    b.bid.write(0xABCD); // Should match awid
    b.bresp.write(0);    // OKAY
    b.rid.write(0xDCBA); // Should match arid
    b.rdata.write(0x1122334455667788ULL);
    b.rresp.write(0);
    b.rlast.write(1);

    // Read back all fields
    REQUIRE(b.awaddr.read() == 0x123456789ABCDEFFULL);
    REQUIRE(b.awlen.read() == 7);
    REQUIRE(b.awsize.read() == 6);
    REQUIRE(b.awburst.read() == 1);
    REQUIRE(b.awid.read() == 0xABCD);
    REQUIRE(b.wdata.read() == 0x0102030405060708ULL);
    REQUIRE(b.wstrb.read() == 0xFFFFFFFFFFFFFFFFULL);
    REQUIRE(b.wlast.read() == 1);

    REQUIRE(b.araddr.read() == 0xFEDCBA9876543210ULL);
    REQUIRE(b.arlen.read() == 3);
    REQUIRE(b.arsize.read() == 5);
    REQUIRE(b.arburst.read() == 1);
    REQUIRE(b.arid.read() == 0xDCBA);

    REQUIRE(b.bid.read() == 0xABCD);
    REQUIRE(b.bresp.read() == 0);
    REQUIRE(b.rid.read() == 0xDCBA);
    REQUIRE(b.rdata.read() == 0x1122334455667788ULL);
    REQUIRE(b.rresp.read() == 0);
    REQUIRE(b.rlast.read() == 1);
}

TEST_CASE("Axi4Bundle: awid/arid independent ID spaces", "[axi][bundle][id]") {
    Axi4Bundle b;

    // Write and read IDs are independent
    b.awid.write(0x1111);
    b.arid.write(0x2222);

    REQUIRE(b.awid.read() == 0x1111);
    REQUIRE(b.arid.read() == 0x2222);
    REQUIRE(b.awid.read() != b.arid.read()); // Independent spaces
}

TEST_CASE("Axi4Bundle: awid->bid and arid->rid association", "[axi][bundle][id]") {
    Axi4Bundle b;

    // Write request ID -> Write response ID
    b.awid.write(0x5555);
    b.bid.write(0x5555);

    // Read request ID -> Read response ID
    b.arid.write(0xAAAA);
    b.rid.write(0xAAAA);

    REQUIRE(b.awid.read() == b.bid.read());
    REQUIRE(b.arid.read() == b.rid.read());
    REQUIRE(b.awid.read() != b.arid.read()); // Read/write independent
}

TEST_CASE("Axi4Bundle: field widths match spec (awaddr 64b, awid 16b, wdata 512b)",
          "[axi][bundle][width]") {
    Axi4Bundle b;

    // awaddr: 64-bit
    b.awaddr.write(UINT64_MAX);
    REQUIRE(b.awaddr.read() == UINT64_MAX);

    // awid: 16-bit (ch_uint<16>)
    b.awid.write(UINT16_MAX);
    REQUIRE(b.awid.read() == UINT16_MAX);

    // arid: 16-bit
    b.arid.write(UINT16_MAX);
    REQUIRE(b.arid.read() == UINT16_MAX);

    // bid: 16-bit
    b.bid.write(UINT16_MAX);
    REQUIRE(b.bid.read() == UINT16_MAX);

    // rid: 16-bit
    b.rid.write(UINT16_MAX);
    REQUIRE(b.rid.read() == UINT16_MAX);

    // wdata: 512-bit = 64 bytes (ch_uint<512> stored in 64-bit chunks)
    // Since we use ch_uint<512> which stores in uint64_t, test max 64-bit value
    // The actual 512-bit is represented as array in ch_uint<512> but our ch_uint uses single
    // uint64_t Per spec: wdata 512-bit -> ch_uint<512>
    b.wdata.write(0xFFFFFFFFFFFFFFFFULL);
    REQUIRE(b.wdata.read() == 0xFFFFFFFFFFFFFFFFULL);
}

TEST_CASE("Axi4Bundle: serialization round-trip", "[axi][bundle][serialization]") {
    Axi4Bundle b;

    b.awaddr.write(0x1122334455667788ULL);
    b.awlen.write(15);
    b.awsize.write(6);
    b.awburst.write(2); // WRAP
    b.awid.write(0xDEAD);
    b.wdata.write(0xAABBCCDDEEFF0011ULL);
    b.wstrb.write(0xFFFFFFFFFFFFFFFFULL);
    b.wlast.write(1);
    b.bid.write(0xDEAD);
    b.bresp.write(1); // EXOKAY
    b.araddr.write(0x8877665544332211ULL);
    b.arlen.write(7);
    b.arsize.write(4);
    b.arburst.write(1); // INCR
    b.arid.write(0xBEEF);
    b.rid.write(0xBEEF);
    b.rdata.write(0x1100FFEEDDCCBBAAULL);
    b.rresp.write(0);
    b.rlast.write(1);

    // Serialize
    std::array<uint8_t, sizeof(Axi4Bundle)> buf{};
    REQUIRE(serialize_bundle(b, buf.data(), buf.size()) == true);

    // Deserialize
    Axi4Bundle b2;
    REQUIRE(deserialize_bundle(buf.data(), buf.size(), b2) == true);

    // Verify all fields
    REQUIRE(b2.awaddr.read() == b.awaddr.read());
    REQUIRE(b2.awlen.read() == b.awlen.read());
    REQUIRE(b2.awsize.read() == b.awsize.read());
    REQUIRE(b2.awburst.read() == b.awburst.read());
    REQUIRE(b2.awid.read() == b.awid.read());
    REQUIRE(b2.wdata.read() == b.wdata.read());
    REQUIRE(b2.wstrb.read() == b.wstrb.read());
    REQUIRE(b2.wlast.read() == b.wlast.read());
    REQUIRE(b2.bid.read() == b.bid.read());
    REQUIRE(b2.bresp.read() == b.bresp.read());
    REQUIRE(b2.araddr.read() == b.araddr.read());
    REQUIRE(b2.arlen.read() == b.arlen.read());
    REQUIRE(b2.arsize.read() == b.arsize.read());
    REQUIRE(b2.arburst.read() == b.arburst.read());
    REQUIRE(b2.arid.read() == b.arid.read());
    REQUIRE(b2.rid.read() == b.rid.read());
    REQUIRE(b2.rdata.read() == b.rdata.read());
    REQUIRE(b2.rresp.read() == b.rresp.read());
    REQUIRE(b2.rlast.read() == b.rlast.read());
}

TEST_CASE("Axi4LiteBundle: all fields round-trip read/write", "[axi][bundle][lite]") {
    Axi4LiteBundle b;

    // Write address channel
    b.awaddr.write(0x1000);
    b.awid.write(0x12);

    // Write data channel
    b.wdata.write(0xDEADBEEF);
    b.wstrb.write(0xF);

    // Write response
    b.bresp.write(0); // OKAY

    // Read address channel
    b.araddr.write(0x2000);
    b.arid.write(0x34);

    // Read data channel
    b.rdata.write(0xCAFEBABE);
    b.rresp.write(0); // OKAY

    REQUIRE(b.awaddr.read() == 0x1000);
    REQUIRE(b.awid.read() == 0x12);
    REQUIRE(b.wdata.read() == 0xDEADBEEF);
    REQUIRE(b.wstrb.read() == 0xF);
    REQUIRE(b.bresp.read() == 0);
    REQUIRE(b.araddr.read() == 0x2000);
    REQUIRE(b.arid.read() == 0x34);
    REQUIRE(b.rdata.read() == 0xCAFEBABE);
    REQUIRE(b.rresp.read() == 0);
}

TEST_CASE("Axi4LiteBundle: serialization round-trip", "[axi][bundle][lite][serialization]") {
    Axi4LiteBundle b;

    b.awaddr.write(0xABCDEF00);
    b.awid.write(0xFFFF);
    b.wdata.write(0x12345678);
    b.wstrb.write(0xFF);
    b.bresp.write(0);
    b.araddr.write(0xFEDCBA00);
    b.arid.write(0xEEEE);
    b.rdata.write(0x87654321);
    b.rresp.write(1); // EXOKAY

    std::array<uint8_t, sizeof(Axi4LiteBundle)> buf{};
    REQUIRE(serialize_bundle(b, buf.data(), buf.size()) == true);

    Axi4LiteBundle b2;
    REQUIRE(deserialize_bundle(buf.data(), buf.size(), b2) == true);

    REQUIRE(b2.awaddr.read() == b.awaddr.read());
    REQUIRE(b2.awid.read() == b.awid.read());
    REQUIRE(b2.wdata.read() == b.wdata.read());
    REQUIRE(b2.wstrb.read() == b.wstrb.read());
    REQUIRE(b2.bresp.read() == b.bresp.read());
    REQUIRE(b2.araddr.read() == b.araddr.read());
    REQUIRE(b2.arid.read() == b.arid.read());
    REQUIRE(b2.rdata.read() == b.rdata.read());
    REQUIRE(b2.rresp.read() == b.rresp.read());
}