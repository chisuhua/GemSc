// test/test_pcie_axi_adapter_backpressure.cc
// AXI4 Stream Adapter valid/ready 反压不丢事务测试 (T-P5-4)
// 功能：验证下游 ready=0 时数据不丢失、valid 保持直到 ready=1；
//       事务在 backpressure 周期内完整完成（无丢/重复）
// 作者 CppTLM Team / 日期 2026-11-03
// 参考: openspec/changes/2026-11-03-cpptlm-dgpu-axi-stream-adapter/tasks.md T-P5-4

#include "bundles/axi4_bundles_tlm.hh"
#include "catch_amalgamated.hpp"
#include "framework/axi4_stream_adapter.hh"

#include <cstdint>

using namespace bundles;

TEST_CASE("Axi4StreamAdapter: master req held when downstream ready=0, no loss",
          "[axi][adapter][backpressure][master]") {
    cpptlm::Axi4StreamAdapter a;

    Axi4Bundle req;
    req.awid.write(0x1);
    req.awaddr.write(0x1000);
    req.wdata.write(0xDEAD);
    req.wlast.write(1);

    REQUIRE(a.master_req(req) == true);
    REQUIRE(a.master_req_valid() == true);

    // 下游 ready=0：数据不丢失，valid 保持
    a.set_master_ready(false);
    a.tick();
    REQUIRE(a.master_req_valid() == true);                // valid 保持
    REQUIRE(a.master_req_data().awaddr.read() == 0x1000); // 数据未丢
    REQUIRE(a.master_req_data().wdata.read() == 0xDEAD);

    // 再次 tick（仍 ready=0）：依旧保持
    a.tick();
    REQUIRE(a.master_req_valid() == true);
    REQUIRE(a.outstanding_wr() == 1); // 请求仍在途

    // ready=1：事务转移
    a.set_master_ready(true);
    a.tick();
    REQUIRE(a.master_req_valid() == false);               // 已转移
    REQUIRE(a.master_req_data().awaddr.read() == 0x1000); // 数据完整
}

TEST_CASE("Axi4StreamAdapter: slave req held when EP not ready, no loss",
          "[axi][adapter][backpressure][slave]") {
    cpptlm::Axi4StreamAdapter a;

    Axi4Bundle req;
    req.awid.write(0x2);
    req.awaddr.write(0x2000);
    req.wdata.write(0xBEEF);
    req.wlast.write(1);

    REQUIRE(a.slave_req(req) == true);
    REQUIRE(a.slave_req_valid() == true);

    // EP ready=0：valid 保持
    a.set_slave_ready(false);
    a.tick();
    REQUIRE(a.slave_req_valid() == true);
    REQUIRE(a.slave_req_data().awaddr.read() == 0x2000);
    REQUIRE(a.slave_req_data().wdata.read() == 0xBEEF);

    // EP ready=1 后 EP 读取 + consume
    a.set_slave_ready(true);
    a.tick();
    REQUIRE(a.slave_req_valid() == true); // EP 侧仍可见
    a.slave_req_consume();
    REQUIRE(a.slave_req_valid() == false);
}

TEST_CASE("Axi4StreamAdapter: cfg req held under backpressure, no loss",
          "[axi][adapter][backpressure][cfg]") {
    cpptlm::Axi4StreamAdapter a;

    Axi4LiteBundle wr;
    wr.awaddr.write(0x04);
    wr.awid.write(0x1);
    wr.wdata.write(0x1234);

    REQUIRE(a.cfg_req(wr) == true);
    a.set_cfg_ready(false);
    a.tick();
    REQUIRE(a.cfg_req_valid() == true);
    REQUIRE(a.cfg_req_data().awaddr.read() == 0x04);
    REQUIRE(a.cfg_req_data().wdata.read() == 0x1234);

    a.set_cfg_ready(true);
    a.tick();
    a.cfg_req_consume();
    REQUIRE(a.cfg_req_valid() == false);
}

TEST_CASE("Axi4StreamAdapter: full transaction completes through backpressure cycles",
          "[axi][adapter][backpressure][transaction]") {
    cpptlm::Axi4StreamAdapter a;

    // 多拍 burst 写（经 backpressure 交错）
    for (int i = 0; i < 4; ++i) {
        Axi4Bundle req;
        req.awid.write(0x7);
        req.awaddr.write(0x3000 + static_cast<uint64_t>(i) * 8);
        req.wdata.write(0x1000 + static_cast<uint64_t>(i));
        req.wlast.write(i == 3 ? 1 : 0);

        REQUIRE(a.master_req(req, i == 0) == true);

        // 交替 ready=0/1 制造 backpressure
        a.set_master_ready(false);
        a.tick();
        REQUIRE(a.master_req_valid() == true); // 反压保持

        a.set_master_ready(true);
        a.tick();
        REQUIRE(a.master_req_valid() == false); // 转移完成
    }

    // 4 拍全部完成（burst 单事务 1 个 outstanding）
    REQUIRE(a.outstanding_wr() == 1);

    // 响应返回并消费
    Axi4Bundle resp;
    resp.bid.write(0x7);
    resp.bresp.write(0);
    REQUIRE(a.master_resp(resp) == true);
    a.master_resp_consume();
    REQUIRE(a.outstanding_wr() == 0);
}
