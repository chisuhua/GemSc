// test/test_axi4_mapper_outstanding.cc
// Axi4Mapper outstanding 跟踪测试 (T-P6-2)
// 功能：验证 outstanding 容量上限、读写独立 ID 空间、完成释放槽位
// 作者 CppTLM Team / 日期 2026-12-22
// 参考: openspec/changes/2026-12-22-cpptlm-dgpu-axi4-mapper/tasks.md T-P6-2

#include "bundles/axi4_bundles_tlm.hh"
#include "catch_amalgamated.hpp"
#include "framework/axi4_mapper.hh"

#include <cstdint>

using namespace bundles;
using namespace cpptlm;

TEST_CASE("Axi4Mapper: outstanding independent read/write ID spaces",
          "[axi][mapper][outstanding]") {
    Axi4Mapper mapper(4); // capacity 4

    // Issue writes with different awid
    Axi4Bundle wreq;
    wreq.awid.write(0x1000);
    wreq.awaddr.write(0x1000);
    REQUIRE(mapper.issue_write(wreq) == true);

    wreq.awid.write(0x2000);
    wreq.awaddr.write(0x2000);
    REQUIRE(mapper.issue_write(wreq) == true);

    // Issue reads with different arid (independent from awid)
    Axi4Bundle rreq;
    rreq.arid.write(0x1000); // Same value as awid above, but independent space
    rreq.araddr.write(0x3000);
    REQUIRE(mapper.issue_read(rreq) == true);

    rreq.arid.write(0x2000);
    rreq.araddr.write(0x4000);
    REQUIRE(mapper.issue_read(rreq) == true);

    // Verify counts are independent
    REQUIRE(mapper.outstanding_wr() == 2);
    REQUIRE(mapper.outstanding_rd() == 2);
}

TEST_CASE("Axi4Mapper: capacity limit N+1 rejects new issue", "[axi][mapper][outstanding]") {
    Axi4Mapper mapper(2); // capacity 2

    Axi4Bundle wreq;

    // Fill to capacity (2)
    wreq.awid.write(0x1000);
    wreq.awaddr.write(0x1000);
    REQUIRE(mapper.issue_write(wreq) == true);

    wreq.awid.write(0x2000);
    wreq.awaddr.write(0x2000);
    REQUIRE(mapper.issue_write(wreq) == true);

    // Verify full
    REQUIRE(mapper.outstanding_wr() == 2);
    REQUIRE(mapper.can_issue_write() == false);

    // N+1 (3rd) should be rejected
    wreq.awid.write(0x3000);
    wreq.awaddr.write(0x3000);
    REQUIRE(mapper.issue_write(wreq) == false);
    REQUIRE(mapper.outstanding_wr() == 2); // Unchanged

    // Same for reads
    Axi4Bundle rreq;
    rreq.arid.write(0x1000);
    rreq.araddr.write(0x1000);
    REQUIRE(mapper.issue_read(rreq) == true);

    rreq.arid.write(0x2000);
    rreq.araddr.write(0x2000);
    REQUIRE(mapper.issue_read(rreq) == true);

    REQUIRE(mapper.outstanding_rd() == 2);
    REQUIRE(mapper.can_issue_read() == false);

    // N+1 rejected
    rreq.arid.write(0x3000);
    rreq.araddr.write(0x3000);
    REQUIRE(mapper.issue_read(rreq) == false);
    REQUIRE(mapper.outstanding_rd() == 2);
}

TEST_CASE("Axi4Mapper: complete releases slot, can re-register", "[axi][mapper][outstanding]") {
    Axi4Mapper mapper(2);

    Axi4Bundle wreq;
    wreq.awid.write(0x1000);
    wreq.awaddr.write(0x1000);
    REQUIRE(mapper.issue_write(wreq) == true);

    wreq.awid.write(0x2000);
    wreq.awaddr.write(0x2000);
    REQUIRE(mapper.issue_write(wreq) == true);

    // Full - cannot issue more
    wreq.awid.write(0x3000);
    wreq.awaddr.write(0x3000);
    REQUIRE(mapper.issue_write(wreq) == false);

    // Complete one write (by bid matching awid)
    REQUIRE(mapper.complete_write(0x1000) == true);
    REQUIRE(mapper.outstanding_wr() == 1);
    REQUIRE(mapper.can_issue_write() == true);

    // Can now re-register
    wreq.awid.write(0x3000);
    wreq.awaddr.write(0x3000);
    REQUIRE(mapper.issue_write(wreq) == true);
    REQUIRE(mapper.outstanding_wr() == 2);

    // Same for reads
    Axi4Bundle rreq;
    rreq.arid.write(0xAAAA);
    rreq.araddr.write(0xAAAA);
    REQUIRE(mapper.issue_read(rreq) == true);

    rreq.arid.write(0xBBBB);
    rreq.araddr.write(0xBBBB);
    REQUIRE(mapper.issue_read(rreq) == true);

    // Complete one read (by rid matching arid)
    REQUIRE(mapper.complete_read(0xAAAA) == true);
    REQUIRE(mapper.outstanding_rd() == 1);
    REQUIRE(mapper.can_issue_read() == true);

    // Can now re-register
    rreq.arid.write(0xCCCC);
    rreq.araddr.write(0xCCCC);
    REQUIRE(mapper.issue_read(rreq) == true);
    REQUIRE(mapper.outstanding_rd() == 2);
}

TEST_CASE("Axi4Mapper: complete non-existent ID returns false, no consume",
          "[axi][mapper][outstanding]") {
    Axi4Mapper mapper(4);

    Axi4Bundle wreq;
    wreq.awid.write(0x1000);
    wreq.awaddr.write(0x1000);
    REQUIRE(mapper.issue_write(wreq) == true);
    REQUIRE(mapper.outstanding_wr() == 1);

    // Complete with non-matching bid
    REQUIRE(mapper.complete_write(0x9999) == false);
    REQUIRE(mapper.outstanding_wr() == 1); // Unchanged - no consume

    // Complete with matching bid
    REQUIRE(mapper.complete_write(0x1000) == true);
    REQUIRE(mapper.outstanding_wr() == 0);
}

TEST_CASE("Axi4Mapper: default capacity and capacity query", "[axi][mapper][outstanding]") {
    Axi4Mapper mapper; // default capacity

    // Default capacity should be reasonable (e.g., 16)
    REQUIRE(mapper.capacity() > 0);
    REQUIRE(mapper.can_issue_write() == true);
    REQUIRE(mapper.can_issue_read() == true);

    // Fill up to capacity
    Axi4Bundle req;
    for (std::size_t i = 0; i < mapper.capacity(); ++i) {
        req.awid.write(static_cast<uint16_t>(i));
        req.awaddr.write(static_cast<uint64_t>(i));
        REQUIRE(mapper.issue_write(req) == true);
    }

    REQUIRE(mapper.can_issue_write() == false);
}

TEST_CASE("Axi4Mapper: reset clears all outstanding", "[axi][mapper][outstanding]") {
    Axi4Mapper mapper(4);

    Axi4Bundle wreq, rreq;
    wreq.awid.write(0x1000);
    wreq.awaddr.write(0x1000);
    rreq.arid.write(0x2000);
    rreq.araddr.write(0x2000);

    REQUIRE(mapper.issue_write(wreq) == true);
    REQUIRE(mapper.issue_read(rreq) == true);

    REQUIRE(mapper.outstanding_wr() == 1);
    REQUIRE(mapper.outstanding_rd() == 1);

    mapper.reset();

    REQUIRE(mapper.outstanding_wr() == 0);
    REQUIRE(mapper.outstanding_rd() == 0);
    REQUIRE(mapper.can_issue_write() == true);
    REQUIRE(mapper.can_issue_read() == true);
}