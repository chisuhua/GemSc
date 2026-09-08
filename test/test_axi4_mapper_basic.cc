// test/test_axi4_mapper_basic.cc
// AXI4Mapper 基础测试：AXI 信号 ↔ Axi4Bundle 无损往返 + 反向还原 (T-P6-1)
// 功能：验证 axi4_signal_to_bundle / axi4_bundle_to_signal 字段完整性
// 作者 CppTLM Team / 日期 2026-12-22
// 参考: openspec/changes/2026-12-22-cpptlm-dgpu-axi4-mapper/tasks.md T-P6-1

#include "bundles/axi4_bundles_tlm.hh"
#include "catch_amalgamated.hpp"
#include "framework/axi4_bundle_to_signal.hh"
#include "framework/axi4_signal_to_bundle.hh"

#include <cstdint>

using namespace bundles;
using namespace cpptlm;

TEST_CASE("AXI4Mapper: bundle_to_signal all fields written", "[axi][mapper][basic]") {
    Axi4Bundle bundle;

    // Initialize all fields
    bundle.awaddr.write(0x123456789ABCDEFFULL);
    bundle.awlen.write(7);   // 8 beats
    bundle.awsize.write(6);  // 64 bytes
    bundle.awburst.write(1); // INCR
    bundle.awid.write(0xABCD);
    bundle.wdata.write(0x0102030405060708ULL);
    bundle.wstrb.write(0xFFFFFFFFFFFFFFFFULL);
    bundle.wlast.write(1);
    bundle.bid.write(0xABCD);
    bundle.bresp.write(0);
    bundle.araddr.write(0xFEDCBA9876543210ULL);
    bundle.arlen.write(3);   // 4 beats
    bundle.arsize.write(5);  // 32 bytes
    bundle.arburst.write(1); // INCR
    bundle.arid.write(0xDCBA);
    bundle.rid.write(0xDCBA);
    bundle.rdata.write(0x1122334455667788ULL);
    bundle.rresp.write(0);
    bundle.rlast.write(1);

    // AXI signal struct (plain C struct for signal interface)
    AXI4Signals signals{};

    // Convert bundle to signal
    bundle_to_signal(bundle, signals);

    // Verify all fields written to signals
    REQUIRE(signals.awaddr == 0x123456789ABCDEFFULL);
    REQUIRE(signals.awlen == 7);
    REQUIRE(signals.awsize == 6);
    REQUIRE(signals.awburst == 1);
    REQUIRE(signals.awid == 0xABCD);
    REQUIRE(signals.wdata == 0x0102030405060708ULL);
    REQUIRE(signals.wstrb == 0xFFFFFFFFFFFFFFFFULL);
    REQUIRE(signals.wlast == 1);
    REQUIRE(signals.bid == 0xABCD);
    REQUIRE(signals.bresp == 0);
    REQUIRE(signals.araddr == 0xFEDCBA9876543210ULL);
    REQUIRE(signals.arlen == 3);
    REQUIRE(signals.arsize == 5);
    REQUIRE(signals.arburst == 1);
    REQUIRE(signals.arid == 0xDCBA);
    REQUIRE(signals.rid == 0xDCBA);
    REQUIRE(signals.rdata == 0x1122334455667788ULL);
    REQUIRE(signals.rresp == 0);
    REQUIRE(signals.rlast == 1);
}

TEST_CASE("AXI4Mapper: signal_to_bundle reverse restores all fields", "[axi][mapper][basic]") {
    // Create signals with known values
    AXI4Signals signals{};
    signals.awaddr = 0x1122334455667788ULL;
    signals.awlen = 15;
    signals.awsize = 6;
    signals.awburst = 2; // WRAP
    signals.awid = 0xDEAD;
    signals.wdata = 0xAABBCCDDEEFF0011ULL;
    signals.wstrb = 0xFFFFFFFFFFFFFFFFULL;
    signals.wlast = 1;
    signals.bid = 0xDEAD;
    signals.bresp = 1; // EXOKAY
    signals.araddr = 0x8877665544332211ULL;
    signals.arlen = 7;
    signals.arsize = 4;
    signals.arburst = 1; // INCR
    signals.arid = 0xBEEF;
    signals.rid = 0xBEEF;
    signals.rdata = 0x1100FFEEDDCCBBAAULL;
    signals.rresp = 0;
    signals.rlast = 1;

    Axi4Bundle bundle;
    signal_to_bundle(signals, bundle);

    // Verify all fields restored
    REQUIRE(bundle.awaddr.read() == 0x1122334455667788ULL);
    REQUIRE(bundle.awlen.read() == 15);
    REQUIRE(bundle.awsize.read() == 6);
    REQUIRE(bundle.awburst.read() == 2);
    REQUIRE(bundle.awid.read() == 0xDEAD);
    REQUIRE(bundle.wdata.read() == 0xAABBCCDDEEFF0011ULL);
    REQUIRE(bundle.wstrb.read() == 0xFFFFFFFFFFFFFFFFULL);
    REQUIRE(bundle.wlast.read() == 1);
    REQUIRE(bundle.bid.read() == 0xDEAD);
    REQUIRE(bundle.bresp.read() == 1);
    REQUIRE(bundle.araddr.read() == 0x8877665544332211ULL);
    REQUIRE(bundle.arlen.read() == 7);
    REQUIRE(bundle.arsize.read() == 4);
    REQUIRE(bundle.arburst.read() == 1);
    REQUIRE(bundle.arid.read() == 0xBEEF);
    REQUIRE(bundle.rid.read() == 0xBEEF);
    REQUIRE(bundle.rdata.read() == 0x1100FFEEDDCCBBAAULL);
    REQUIRE(bundle.rresp.read() == 0);
    REQUIRE(bundle.rlast.read() == 1);
}

TEST_CASE("AXI4Mapper: round-trip bundle -> signal -> bundle", "[axi][mapper][roundtrip]") {
    Axi4Bundle original;

    original.awaddr.write(0x123456789ABCDEFFULL);
    original.awlen.write(7);
    original.awsize.write(6);
    original.awburst.write(1);
    original.awid.write(0xABCD);
    original.wdata.write(0x0102030405060708ULL);
    original.wstrb.write(0xFFFFFFFFFFFFFFFFULL);
    original.wlast.write(1);
    original.bid.write(0xABCD);
    original.bresp.write(0);
    original.araddr.write(0xFEDCBA9876543210ULL);
    original.arlen.write(3);
    original.arsize.write(5);
    original.arburst.write(1);
    original.arid.write(0xDCBA);
    original.rid.write(0xDCBA);
    original.rdata.write(0x1122334455667788ULL);
    original.rresp.write(0);
    original.rlast.write(1);

    AXI4Signals signals{};
    bundle_to_signal(original, signals);

    Axi4Bundle restored;
    signal_to_bundle(signals, restored);

    // Verify all fields match
    REQUIRE(restored.awaddr.read() == original.awaddr.read());
    REQUIRE(restored.awlen.read() == original.awlen.read());
    REQUIRE(restored.awsize.read() == original.awsize.read());
    REQUIRE(restored.awburst.read() == original.awburst.read());
    REQUIRE(restored.awid.read() == original.awid.read());
    REQUIRE(restored.wdata.read() == original.wdata.read());
    REQUIRE(restored.wstrb.read() == original.wstrb.read());
    REQUIRE(restored.wlast.read() == original.wlast.read());
    REQUIRE(restored.bid.read() == original.bid.read());
    REQUIRE(restored.bresp.read() == original.bresp.read());
    REQUIRE(restored.araddr.read() == original.araddr.read());
    REQUIRE(restored.arlen.read() == original.arlen.read());
    REQUIRE(restored.arsize.read() == original.arsize.read());
    REQUIRE(restored.arburst.read() == original.arburst.read());
    REQUIRE(restored.arid.read() == original.arid.read());
    REQUIRE(restored.rid.read() == original.rid.read());
    REQUIRE(restored.rdata.read() == original.rdata.read());
    REQUIRE(restored.rresp.read() == original.rresp.read());
    REQUIRE(restored.rlast.read() == original.rlast.read());
}

TEST_CASE("AXI4Mapper: empty bundle conversion does not crash", "[axi][mapper][edge]") {
    Axi4Bundle bundle; // all zeros
    AXI4Signals signals{};

    // Should not crash
    bundle_to_signal(bundle, signals);

    Axi4Bundle restored;
    signal_to_bundle(signals, restored);

    // All zeros should remain zeros
    REQUIRE(restored.awaddr.read() == 0);
    REQUIRE(restored.awlen.read() == 0);
    REQUIRE(restored.awid.read() == 0);
    REQUIRE(restored.araddr.read() == 0);
    REQUIRE(restored.arid.read() == 0);
    REQUIRE(restored.rid.read() == 0);
    REQUIRE(restored.bid.read() == 0);
}