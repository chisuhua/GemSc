// test/test_pcie_axi_adapter_ids.cc
// AXI4 outstanding 请求 ID 关联测试 (T-P5-5)
// 功能：验证 awid→bid / arid→rid 关联：多 outstanding 请求、OOO 响应匹配、
//       读写独立 ID 空间、错误 ID 不匹配
// 作者 CppTLM Team / 日期 2026-11-03
// 参考: openspec/changes/2026-11-03-cpptlm-dgpu-axi-stream-adapter/tasks.md T-P5-5

#include "bundles/axi4_bundles_tlm.hh"
#include "catch_amalgamated.hpp"
#include "framework/axi4_stream_adapter.hh"

#include <algorithm>
#include <cstdint>

using namespace bundles;

namespace {
    bool contains(const std::deque<uint16_t>& ids, uint16_t id) {
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    }
} // namespace

TEST_CASE("Axi4StreamAdapter: multiple outstanding write IDs, OOO bid matching",
          "[axi][adapter][ids][wr]") {
    cpptlm::Axi4StreamAdapter a;

    // 3 个 outstanding 写请求（awid=1,2,3）
    for (uint16_t id : {1, 2, 3}) {
        Axi4Bundle req;
        req.awid.write(id);
        req.awaddr.write(0x1000 + id * 8);
        req.wdata.write(id);
        req.wlast.write(1);
        REQUIRE(a.master_req(req) == true);
        a.set_master_ready(true);
        a.tick();
        a.master_req_consume();
    }
    REQUIRE(a.outstanding_wr() == 3);
    REQUIRE(contains(a.outstanding_wr_ids(), 1));
    REQUIRE(contains(a.outstanding_wr_ids(), 2));
    REQUIRE(contains(a.outstanding_wr_ids(), 3));

    // OOO 写响应：bid=2 先回
    Axi4Bundle r2;
    r2.bid.write(2);
    r2.bresp.write(0);
    REQUIRE(a.master_resp(r2) == true);
    REQUIRE(a.outstanding_wr() == 2);
    REQUIRE(contains(a.outstanding_wr_ids(), 1));
    REQUIRE_FALSE(contains(a.outstanding_wr_ids(), 2));
    REQUIRE(contains(a.outstanding_wr_ids(), 3));
    a.master_resp_consume();

    // bid=1
    Axi4Bundle r1;
    r1.bid.write(1);
    r1.bresp.write(0);
    REQUIRE(a.master_resp(r1) == true);
    REQUIRE(a.outstanding_wr() == 1);
    REQUIRE_FALSE(contains(a.outstanding_wr_ids(), 1));
    REQUIRE(contains(a.outstanding_wr_ids(), 3));
    a.master_resp_consume();

    // bid=3 → 全部完成
    Axi4Bundle r3;
    r3.bid.write(3);
    r3.bresp.write(0);
    REQUIRE(a.master_resp(r3) == true);
    REQUIRE(a.outstanding_wr() == 0);
    REQUIRE(a.outstanding_wr_ids().empty());
}

TEST_CASE("Axi4StreamAdapter: multiple outstanding read IDs, OOO rid matching",
          "[axi][adapter][ids][rd]") {
    cpptlm::Axi4StreamAdapter a;

    // 2 个 outstanding 读请求（arid=4,5）
    for (uint16_t id : {4, 5}) {
        Axi4Bundle req;
        req.arid.write(id);
        req.araddr.write(0x2000 + id * 8);
        REQUIRE(a.master_req(req) == true);
        a.set_master_ready(true);
        a.tick();
        a.master_req_consume();
    }
    REQUIRE(a.outstanding_rd() == 2);
    REQUIRE(contains(a.outstanding_rd_ids(), 4));
    REQUIRE(contains(a.outstanding_rd_ids(), 5));

    // OOO 读响应：rid=5 先回（rlast）
    Axi4Bundle r5;
    r5.rid.write(5);
    r5.rdata.write(0x55);
    r5.rresp.write(0);
    r5.rlast.write(1);
    REQUIRE(a.master_resp(r5) == true);
    REQUIRE(a.outstanding_rd() == 1);
    REQUIRE_FALSE(contains(a.outstanding_rd_ids(), 5));
    REQUIRE(contains(a.outstanding_rd_ids(), 4));
    a.master_resp_consume();

    // rid=4
    Axi4Bundle r4;
    r4.rid.write(4);
    r4.rdata.write(0x44);
    r4.rresp.write(0);
    r4.rlast.write(1);
    REQUIRE(a.master_resp(r4) == true);
    REQUIRE(a.outstanding_rd() == 0);
    REQUIRE(a.outstanding_rd_ids().empty());
}

TEST_CASE("Axi4StreamAdapter: read/write independent ID spaces",
          "[axi][adapter][ids][independent]") {
    cpptlm::Axi4StreamAdapter a;

    // 写 awid=1 + 读 arid=2 并发
    Axi4Bundle wr;
    wr.awid.write(1);
    wr.awaddr.write(0x3000);
    wr.wlast.write(1);
    REQUIRE(a.master_req(wr) == true);
    a.set_master_ready(true);
    a.tick();
    a.master_req_consume();

    Axi4Bundle rd;
    rd.arid.write(2);
    rd.araddr.write(0x4000);
    REQUIRE(a.master_req(rd) == true);
    a.set_master_ready(true);
    a.tick();
    a.master_req_consume();

    REQUIRE(a.outstanding_wr() == 1);
    REQUIRE(a.outstanding_rd() == 1);

    // 写响应只清 write outstanding，不影响 read
    Axi4Bundle b;
    b.bid.write(1);
    b.bresp.write(0);
    REQUIRE(a.master_resp(b) == true);
    REQUIRE(a.outstanding_wr() == 0);
    REQUIRE(a.outstanding_rd() == 1);
    a.master_resp_consume();

    // 读响应只清 read outstanding
    Axi4Bundle r;
    r.rid.write(2);
    r.rdata.write(0x22);
    r.rlast.write(1);
    REQUIRE(a.master_resp(r) == true);
    REQUIRE(a.outstanding_rd() == 0);
}

TEST_CASE("Axi4StreamAdapter: mismatched response ID does not remove outstanding",
          "[axi][adapter][ids][mismatch]") {
    cpptlm::Axi4StreamAdapter a;

    Axi4Bundle wr;
    wr.awid.write(9);
    wr.awaddr.write(0x5000);
    wr.wlast.write(1);
    REQUIRE(a.master_req(wr) == true);
    a.set_master_ready(true);
    a.tick();
    a.master_req_consume();
    REQUIRE(a.outstanding_wr() == 1);

    // 错误 bid（无匹配）→ outstanding 保持
    Axi4Bundle wrong;
    wrong.bid.write(7);
    wrong.bresp.write(0);
    REQUIRE(a.master_resp(wrong) == true);
    REQUIRE(a.outstanding_wr() == 1);
    REQUIRE(contains(a.outstanding_wr_ids(), 9));
    a.master_resp_consume();

    // 正确 bid=9 → 清除
    Axi4Bundle ok;
    ok.bid.write(9);
    ok.bresp.write(0);
    REQUIRE(a.master_resp(ok) == true);
    REQUIRE(a.outstanding_wr() == 0);
}

TEST_CASE("Axi4StreamAdapter: multi-beat read burst keeps outstanding until RLAST (M2)",
          "[axi][adapter][ids][rd][burst]") {
    cpptlm::Axi4StreamAdapter a;

    // 4 拍读 burst（arlen=3, arid=7）：登记 1 个 outstanding
    Axi4Bundle req;
    req.arid.write(7);
    req.araddr.write(0x4000);
    req.arlen.write(3);
    REQUIRE(a.master_req(req) == true);
    a.set_master_ready(true);
    a.tick();
    a.master_req_consume();
    REQUIRE(a.outstanding_rd() == 1);
    REQUIRE(contains(a.outstanding_rd_ids(), 7));

    // 前 3 拍 RLAST=0：outstanding 必须保持（M2: 旧实现 rid!=0 即清除, 这里验证修复）
    for (int i = 0; i < 3; ++i) {
        Axi4Bundle beat;
        beat.rid.write(7);
        beat.rdata.write(static_cast<uint64_t>(i));
        beat.rresp.write(0);
        beat.rlast.write(0);
        REQUIRE(a.master_resp(beat) == true);
        REQUIRE(a.outstanding_rd() == 1); // 中途不清
        REQUIRE(contains(a.outstanding_rd_ids(), 7));
        a.master_resp_consume();
    }

    // 末拍 RLAST=1：outstanding 清除
    Axi4Bundle last;
    last.rid.write(7);
    last.rdata.write(0xDEADBEEF);
    last.rresp.write(0);
    last.rlast.write(1);
    REQUIRE(a.master_resp(last) == true);
    REQUIRE(a.outstanding_rd() == 0);
    REQUIRE(a.outstanding_rd_ids().empty());
}
