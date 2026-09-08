// test/test_axi4_mapper_out_of_order.cc
// Axi4Mapper OOO completion（rid 关联）测试 (T-P6-3)
// 功能：验证多 outstanding 读、乱序 rdata 返回、rid 关联回原事务、不匹配不消耗
// 作者 CppTLM Team / 日期 2026-12-22
// 参考: openspec/changes/2026-12-22-cpptlm-dgpu-axi4-mapper/tasks.md T-P6-3

#include "bundles/axi4_bundles_tlm.hh"
#include "catch_amalgamated.hpp"
#include "framework/axi4_mapper.hh"

#include <cstdint>

using namespace bundles;
using namespace cpptlm;

TEST_CASE("Axi4Mapper: multiple outstanding reads, OOO rdata via rid association",
          "[axi][mapper][ooo]") {
    Axi4Mapper mapper(8);

    // Issue 3 outstanding reads: A=0x1000 (addr=0x1000), B=0x2000 (addr=0x2000), C=0x3000
    // (addr=0x3000)
    Axi4Bundle reqA, reqB, reqC;
    reqA.arid.write(0xAAAA);
    reqA.araddr.write(0x1000);
    reqA.arlen.write(0);
    reqA.arsize.write(3);
    reqA.arburst.write(1);
    REQUIRE(mapper.issue_read(reqA) == true);

    reqB.arid.write(0xBBBB);
    reqB.araddr.write(0x2000);
    reqB.arlen.write(0);
    reqB.arsize.write(3);
    reqB.arburst.write(1);
    REQUIRE(mapper.issue_read(reqB) == true);

    reqC.arid.write(0xCCCC);
    reqC.araddr.write(0x3000);
    reqC.arlen.write(0);
    reqC.arsize.write(3);
    reqC.arburst.write(1);
    REQUIRE(mapper.issue_read(reqC) == true);

    REQUIRE(mapper.outstanding_rd() == 3);
    REQUIRE(mapper.can_issue_read() == true); // capacity 8

    // Downstream returns rdata OUT OF ORDER: C first (rid=0xCCCC), then A (rid=0xAAAA), then B
    // (rid=0xBBBB) Complete C first
    REQUIRE(mapper.complete_read(0xCCCC, 0xC0FFEE00, true) == true);
    REQUIRE(mapper.has_read_data(0xCCCC) == true);
    REQUIRE(mapper.read_data(0xCCCC) == 0xC0FFEE00);
    REQUIRE(mapper.pending_read(0xCCCC) == nullptr); // rlast=true -> removed from pending
    REQUIRE(mapper.outstanding_rd() == 2);           // C slot released

    // Complete A second
    REQUIRE(mapper.complete_read(0xAAAA, 0xA0FFEE00, true) == true);
    REQUIRE(mapper.has_read_data(0xAAAA) == true);
    REQUIRE(mapper.read_data(0xAAAA) == 0xA0FFEE00);
    REQUIRE(mapper.outstanding_rd() == 1);

    // Complete B third
    REQUIRE(mapper.complete_read(0xBBBB, 0xB0FFEE00, true) == true);
    REQUIRE(mapper.has_read_data(0xBBBB) == true);
    REQUIRE(mapper.read_data(0xBBBB) == 0xB0FFEE00);
    REQUIRE(mapper.outstanding_rd() == 0);
}

TEST_CASE("Axi4Mapper: OOO completion does not affect other outstanding (non-match not consumed)",
          "[axi][mapper][ooo]") {
    Axi4Mapper mapper(8);

    Axi4Bundle reqA, reqB;
    reqA.arid.write(0xAAAA);
    reqA.araddr.write(0x1000);
    reqB.arid.write(0xBBBB);
    reqB.araddr.write(0x2000);
    REQUIRE(mapper.issue_read(reqA) == true);
    REQUIRE(mapper.issue_read(reqB) == true);

    // Try complete with non-existent rid
    REQUIRE(mapper.complete_read(0x9999, 0xDEADBEEF, true) == false);
    REQUIRE(mapper.outstanding_rd() == 2); // Unchanged

    // Complete A
    REQUIRE(mapper.complete_read(0xAAAA, 0xA0FFEE00, true) == true);
    REQUIRE(mapper.outstanding_rd() == 1);

    // Try complete non-existent again
    REQUIRE(mapper.complete_read(0x9999, 0xDEADBEEF, true) == false);
    REQUIRE(mapper.outstanding_rd() == 1); // B still outstanding

    // Complete B
    REQUIRE(mapper.complete_read(0xBBBB, 0xB0FFEE00, true) == true);
    REQUIRE(mapper.outstanding_rd() == 0);
}

TEST_CASE("Axi4Mapper: burst read multi-beat OOO (rlast only releases on last beat)",
          "[axi][mapper][ooo]") {
    Axi4Mapper mapper(8);

    // Issue a burst read (arlen=3 => 4 beats)
    Axi4Bundle req;
    req.arid.write(0xB057);
    req.araddr.write(0x1000);
    req.arlen.write(3);   // 4 beats
    req.arsize.write(3);  // 8 bytes per beat
    req.arburst.write(1); // INCR
    REQUIRE(mapper.issue_read(req) == true);
    REQUIRE(mapper.outstanding_rd() == 1);

    // Beat 0 (not last)
    REQUIRE(mapper.complete_read(0xB057, 0x11111111, false) == true);
    REQUIRE(mapper.has_read_data(0xB057) == true);
    REQUIRE(mapper.outstanding_rd() == 1); // Still outstanding

    // Beat 1
    REQUIRE(mapper.complete_read(0xB057, 0x22222222, false) == true);
    REQUIRE(mapper.read_data(0xB057) == 0x22222222); // Last rdata wins
    REQUIRE(mapper.outstanding_rd() == 1);

    // Beat 2
    REQUIRE(mapper.complete_read(0xB057, 0x33333333, false) == true);
    REQUIRE(mapper.read_data(0xB057) == 0x33333333);
    REQUIRE(mapper.outstanding_rd() == 1);

    // Beat 3 (last)
    REQUIRE(mapper.complete_read(0xB057, 0x44444444, true) == true);
    REQUIRE(mapper.read_data(0xB057) == 0x44444444);
    REQUIRE(mapper.outstanding_rd() == 0); // Released on rlast
    REQUIRE(mapper.pending_read(0xB057) == nullptr);
}

TEST_CASE("Axi4Mapper: pending_read returns original araddr for OOO association",
          "[axi][mapper][ooo]") {
    Axi4Mapper mapper(8);

    Axi4Bundle req;
    req.arid.write(0x1234);
    req.araddr.write(0xABCDEF00);
    req.arlen.write(0);
    req.arsize.write(3);
    req.arburst.write(1);
    REQUIRE(mapper.issue_read(req) == true);

    // Before completion, pending_read should return original transaction
    const Axi4Bundle* pending = mapper.pending_read(0x1234);
    REQUIRE(pending != nullptr);
    REQUIRE(pending->araddr.read() == 0xABCDEF00);
    REQUIRE(pending->arid.read() == 0x1234);

    // After completion with rlast, pending_read should return nullptr
    REQUIRE(mapper.complete_read(0x1234, 0x11223344, true) == true);
    REQUIRE(mapper.pending_read(0x1234) == nullptr);
}

TEST_CASE("Axi4Mapper: interleaved OOO reads and writes", "[axi][mapper][ooo]") {
    Axi4Mapper mapper(8);

    // Issue writes
    Axi4Bundle wreq;
    wreq.awid.write(0x1001);
    wreq.awaddr.write(0x1001);
    REQUIRE(mapper.issue_write(wreq) == true);

    wreq.awid.write(0x1002);
    wreq.awaddr.write(0x1002);
    REQUIRE(mapper.issue_write(wreq) == true);

    // Issue reads
    Axi4Bundle rreq;
    rreq.arid.write(0x2001);
    rreq.araddr.write(0x2001);
    REQUIRE(mapper.issue_read(rreq) == true);

    rreq.arid.write(0x2002);
    rreq.araddr.write(0x2002);
    REQUIRE(mapper.issue_read(rreq) == true);

    REQUIRE(mapper.outstanding_wr() == 2);
    REQUIRE(mapper.outstanding_rd() == 2);

    // Complete reads OOO: R002 first
    REQUIRE(mapper.complete_read(0x2002, 0xABABAB02, true) == true);
    REQUIRE(mapper.outstanding_rd() == 1);
    REQUIRE(mapper.outstanding_wr() == 2); // Writes unaffected

    // Complete writes: W001
    REQUIRE(mapper.complete_write(0x1001) == true);
    REQUIRE(mapper.outstanding_wr() == 1);

    // Complete reads: R001
    REQUIRE(mapper.complete_read(0x2001, 0xABABAB01, true) == true);
    REQUIRE(mapper.outstanding_rd() == 0);

    // Complete writes: W002
    REQUIRE(mapper.complete_write(0x1002) == true);
    REQUIRE(mapper.outstanding_wr() == 0);
}