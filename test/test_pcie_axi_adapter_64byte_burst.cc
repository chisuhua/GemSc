// test/test_pcie_axi_adapter_64byte_burst.cc
// PcieAxiAdapter 512-bit/64-byte burst 测试 (T-P5-3)
// 功能：验证 PcieAxiAdapter 绑定 PcieEndpointIP 后 64-byte burst 写：
//       - awlen=4 最后一拍 wlast 置位
//       - 总传输字节 = (len+1) × 2^awsize
//       - 事务完整到达下游（Axi4StreamAdapter）
// 作者 CppTLM Team / 日期 2026-11-03
// 参考: openspec/changes/2026-11-03-cpptlm-dgpu-axi-stream-adapter/tasks.md T-P5-3

#include "bundles/axi4_bundles_tlm.hh"
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "tlm/pcie/pcie_axi_adapter_tlm.hh"
#include "tlm/pcie/pcie_endpoint_ip.hh"

#include <cstdint>

using namespace bundles;
using namespace tlm::pcie;

TEST_CASE("PcieAxiAdapter: awlen=4 burst write sets wlast on last beat",
          "[axi][pcie][adapter][burst]") {
    EventQueue eq;
    PcieEndpointIP ep("pcie_ep_axi_burst", &eq);
    ep.init();
    PcieAxiAdapter adapter(&ep, &eq);

    // 64-byte burst 写请求：awsize=6 → 2^6=64 bytes/beat, awlen=4 → 5 beats
    Axi4Bundle req;
    req.awid.write(0x1);
    req.awaddr.write(0x1000);
    req.awlen.write(4);
    req.awsize.write(6);  // 64 bytes per beat (512-bit)
    req.awburst.write(1); // INCR

    REQUIRE(adapter.master_write_burst(req) == true);
    REQUIRE(adapter.awlen() == 4);
    REQUIRE(adapter.awsize() == 6);

    // 总传输字节 = (len+1) × 2^awsize = 5 × 64 = 320
    REQUIRE(adapter.total_bytes() == 320u);

    // 依次推 5 拍写数据：最后一拍 wlast=1
    for (int i = 0; i < 5; ++i) {
        const bool is_last = (i == 4);
        REQUIRE(adapter.write_beat(static_cast<uint64_t>(0xAAAA0000u + i), 0xFFFFFFFFFFFFFFFFull) ==
                true);
        REQUIRE(adapter.current_beat_is_last() == is_last);
        // 每拍推送到下游 Axi4StreamAdapter
        REQUIRE(adapter.push_beat_to_downstream() == true);
        adapter.axi().set_master_ready(true);
        adapter.axi().tick();
        adapter.axi().master_req_consume();
    }

    // 事务完整到达下游：5 拍全部消费，wlast 在最后一拍
    REQUIRE(adapter.beats_sent() == 5);
    REQUIRE(adapter.burst_complete() == true);
}

TEST_CASE("PcieAxiAdapter: total bytes formula (len+1) x 2^awsize",
          "[axi][pcie][adapter][burst][formula]") {
    EventQueue eq;
    PcieEndpointIP ep("pcie_ep_axi_formula", &eq);
    ep.init();
    PcieAxiAdapter adapter(&ep, &eq);

    // awlen=4, awsize=6 → 320 bytes
    Axi4Bundle req6;
    req6.awid.write(0x2);
    req6.awaddr.write(0x2000);
    req6.awlen.write(4);
    req6.awsize.write(6);
    req6.awburst.write(1);
    REQUIRE(adapter.master_write_burst(req6) == true);
    REQUIRE(adapter.total_bytes() == 320u);
    adapter.reset_burst(); // 完成上一 burst，允许开启新 burst

    // awlen=0, awsize=6 → 1 × 64 = 64 bytes (单拍 64-byte burst)
    Axi4Bundle req0;
    req0.awid.write(0x3);
    req0.awaddr.write(0x3000);
    req0.awlen.write(0);
    req0.awsize.write(6);
    req0.awburst.write(1);
    REQUIRE(adapter.master_write_burst(req0) == true);
    REQUIRE(adapter.total_bytes() == 64u);
    adapter.reset_burst();

    // awlen=3, awsize=4 → 4 × 16 = 64 bytes
    Axi4Bundle req3;
    req3.awid.write(0x4);
    req3.awaddr.write(0x4000);
    req3.awlen.write(3);
    req3.awsize.write(4);
    req3.awburst.write(1);
    REQUIRE(adapter.master_write_burst(req3) == true);
    REQUIRE(adapter.total_bytes() == 64u);
}

TEST_CASE("PcieAxiAdapter: burst write fully arrives downstream with bid response",
          "[axi][pcie][adapter][burst][resp]") {
    EventQueue eq;
    PcieEndpointIP ep("pcie_ep_axi_resp", &eq);
    ep.init();
    PcieAxiAdapter adapter(&ep, &eq);

    Axi4Bundle req;
    req.awid.write(0x9);
    req.awaddr.write(0x5000);
    req.awlen.write(1);  // 2 beats
    req.awsize.write(6); // 64B/beat → 128 bytes
    req.awburst.write(1);
    REQUIRE(adapter.master_write_burst(req) == true);
    REQUIRE(adapter.total_bytes() == 128u);

    // 推送 2 拍
    for (int i = 0; i < 2; ++i) {
        REQUIRE(adapter.write_beat(0x1000 + i, 0xFFFFFFFFFFFFFFFFull) == true);
        adapter.push_beat_to_downstream();
        adapter.axi().set_master_ready(true);
        adapter.axi().tick();
        adapter.axi().master_req_consume();
    }
    REQUIRE(adapter.burst_complete() == true);

    // 写响应：BID 匹配 awid
    Axi4Bundle resp;
    resp.bid.write(0x9);
    resp.bresp.write(0);
    REQUIRE(adapter.axi().master_resp(resp) == true);
    REQUIRE(adapter.axi().master_resp_data().bid.read() == 0x9);
    REQUIRE(adapter.axi().master_resp_data().bresp.read() == 0);
    adapter.axi().master_resp_consume();

    // outstanding 写请求全部完成
    REQUIRE(adapter.axi().outstanding_wr() == 0);
}

TEST_CASE("PcieAxiAdapter: bound to PcieEndpointIP", "[axi][pcie][adapter][bind]") {
    EventQueue eq;
    PcieEndpointIP ep("pcie_ep_axi_bind", &eq);
    ep.init();
    PcieAxiAdapter adapter(&ep, &eq);

    REQUIRE(adapter.endpoint() == &ep);
    // 三端口 adapter 可访问（axi_master_out / axi_slave_in / cfg_slave_in）
    REQUIRE(adapter.axi().master_req_valid() == false);
    REQUIRE(adapter.axi().slave_req_valid() == false);
    REQUIRE(adapter.axi().cfg_req_valid() == false);
}
