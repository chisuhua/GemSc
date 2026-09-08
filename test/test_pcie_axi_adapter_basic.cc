// test/test_pcie_axi_adapter_basic.cc
// AXI4 Stream Adapter 基础读写与响应测试 (T-P5-2)
// 功能：验证 Axi4StreamAdapter 三端口 (axi_master_out / axi_slave_in / cfg_slave_in)
//       基础读写、响应返回、valid/ready 握手、不丢事务
// 作者 CppTLM Team / 日期 2026-11-03
// 参考: openspec/changes/2026-11-03-cpptlm-dgpu-axi-stream-adapter/tasks.md T-P5-2

#include "bundles/axi4_bundles_tlm.hh"
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "framework/axi4_stream_adapter.hh"
#include "tlm/gpu/pcie_endpoint_tlm.h"
#include "tlm/pcie/pcie_axi_adapter_tlm.hh"
#include "tlm/pcie/pcie_endpoint_ip.hh"

#include <cstdint>
#include <nlohmann/json.hpp>

using namespace bundles;
using json = nlohmann::json;

TEST_CASE("Axi4StreamAdapter: master write request reaches downstream with awid/awaddr",
          "[axi][adapter][basic][master]") {
    cpptlm::Axi4StreamAdapter a;

    // EP 发起写请求
    Axi4Bundle req;
    req.awid.write(0x5);
    req.awaddr.write(0x1000);
    req.awlen.write(0);
    req.awsize.write(3);
    req.wdata.write(0xDEADBEEF);
    req.wlast.write(1);

    REQUIRE(a.master_req(req) == true);

    // 下游 ready=1, tick → 请求到达
    a.set_master_ready(true);
    a.tick();

    // 请求已传输（valid 清除），outstanding 写 ID 登记
    REQUIRE(a.master_req_valid() == false);
    REQUIRE(a.outstanding_wr() == 1);
    REQUIRE(a.outstanding_rd() == 0);

    // SoC 返回写响应 BID=5 (匹配 awid)
    Axi4Bundle resp;
    resp.bid.write(0x5);
    resp.bresp.write(0); // OKAY
    REQUIRE(a.master_resp(resp) == true);
    REQUIRE(a.master_resp_valid() == true);
    REQUIRE(a.master_resp_data().bid.read() == 0x5);
    REQUIRE(a.master_resp_data().bresp.read() == 0);

    // EP 消费响应 → outstanding 清除
    a.master_resp_consume();
    REQUIRE(a.master_resp_valid() == false);
    REQUIRE(a.outstanding_wr() == 0);
}

TEST_CASE("Axi4StreamAdapter: master read request returns rdata with rid",
          "[axi][adapter][basic][master]") {
    cpptlm::Axi4StreamAdapter a;

    Axi4Bundle req;
    req.arid.write(0x7);
    req.araddr.write(0x2000);
    req.arlen.write(0);
    req.arsize.write(3);

    REQUIRE(a.master_req(req) == true);
    a.set_master_ready(true);
    a.tick();

    REQUIRE(a.master_req_valid() == false);
    REQUIRE(a.outstanding_rd() == 1);

    // SoC 返回读数据 RID=7
    Axi4Bundle r;
    r.rid.write(0x7);
    r.rdata.write(0xCAFEBABE);
    r.rresp.write(0);
    r.rlast.write(1);
    REQUIRE(a.master_resp(r) == true);
    REQUIRE(a.master_resp_valid() == true);
    REQUIRE(a.master_resp_data().rid.read() == 0x7);
    REQUIRE(a.master_resp_data().rdata.read() == 0xCAFEBABE);

    a.master_resp_consume();
    REQUIRE(a.outstanding_rd() == 0);
}

TEST_CASE("Axi4StreamAdapter: slave_in accepts SoC request and returns response",
          "[axi][adapter][basic][slave]") {
    cpptlm::Axi4StreamAdapter a;

    // SoC 发起写请求进入 Endpoint
    Axi4Bundle req;
    req.awid.write(0x3);
    req.awaddr.write(0x3000);
    req.wdata.write(0x1234);
    req.wlast.write(1);

    REQUIRE(a.slave_req(req) == true);
    // EP ready=1, tick → 请求送达 EP
    a.set_slave_ready(true);
    a.tick();

    REQUIRE(a.slave_req_valid() == true);
    REQUIRE(a.slave_req_data().awaddr.read() == 0x3000);
    REQUIRE(a.slave_req_data().awid.read() == 0x3);
    a.slave_req_consume();

    // EP 返回写响应
    Axi4Bundle resp;
    resp.bid.write(0x3);
    resp.bresp.write(0);
    REQUIRE(a.slave_resp(resp) == true);
    REQUIRE(a.slave_resp_valid() == true);
    REQUIRE(a.slave_resp_data().bid.read() == 0x3);
    a.slave_resp_consume();
}

TEST_CASE("Axi4StreamAdapter: cfg_slave_in AXI4-Lite config access", "[axi][adapter][basic][cfg]") {
    cpptlm::Axi4StreamAdapter a;

    // SoC 写配置寄存器
    Axi4LiteBundle wr;
    wr.awaddr.write(0x04);
    wr.awid.write(0x1);
    wr.wdata.write(0xABCD);
    wr.wstrb.write(0xF);

    REQUIRE(a.cfg_req(wr) == true);
    a.set_cfg_ready(true);
    a.tick();
    REQUIRE(a.cfg_req_valid() == true);
    REQUIRE(a.cfg_req_data().awaddr.read() == 0x04);
    REQUIRE(a.cfg_req_data().wdata.read() == 0xABCD);
    a.cfg_req_consume();

    // EP 返回写响应
    Axi4LiteBundle wresp;
    wresp.awid.write(0x1);
    wresp.bresp.write(0);
    REQUIRE(a.cfg_resp(wresp) == true);
    REQUIRE(a.cfg_resp_valid() == true);
    a.cfg_resp_consume();

    // 读配置
    Axi4LiteBundle rd;
    rd.araddr.write(0x04);
    rd.arid.write(0x2);
    REQUIRE(a.cfg_req(rd) == true);
    a.set_cfg_ready(true);
    a.tick();
    REQUIRE(a.cfg_req_valid() == true);
    REQUIRE(a.cfg_req_data().araddr.read() == 0x04);
    a.cfg_req_consume();

    Axi4LiteBundle rresp;
    rresp.arid.write(0x2);
    rresp.rdata.write(0xABCD);
    rresp.rresp.write(0);
    REQUIRE(a.cfg_resp(rresp) == true);
    REQUIRE(a.cfg_resp_valid() == true);
    REQUIRE(a.cfg_resp_data().rdata.read() == 0xABCD);
    a.cfg_resp_consume();
}

TEST_CASE("Axi4StreamAdapter: initial state has no valid transactions",
          "[axi][adapter][basic][state]") {
    cpptlm::Axi4StreamAdapter a;
    REQUIRE(a.master_req_valid() == false);
    REQUIRE(a.master_resp_valid() == false);
    REQUIRE(a.slave_req_valid() == false);
    REQUIRE(a.slave_resp_valid() == false);
    REQUIRE(a.cfg_req_valid() == false);
    REQUIRE(a.cfg_resp_valid() == false);
    REQUIRE(a.outstanding_wr() == 0);
    REQUIRE(a.outstanding_rd() == 0);
}

TEST_CASE("PcieEndpointTLM: axi_adapter config attaches PcieAxiAdapter",
          "[axi][adapter][basic][compose]") {
    tlm::gpu::PcieEndpointTLM ep("pcie_ep_axi_cfg", nullptr);
    ep.init();

    json cfg;
    cfg["axi_adapter"] = json::object(); // 启用 AXI Adapter 挂接
    ep.set_config(cfg);                  // → on_config_loaded → attach_to_endpoint

    REQUIRE(tlm::pcie::PcieAxiAdapter::for_endpoint("pcie_ep_axi_cfg") != nullptr);
    REQUIRE(tlm::pcie::PcieAxiAdapter::for_endpoint("pcie_ep_axi_cfg")->event_queue() == nullptr);

    // 清理（detach 防跨测试污染）
    tlm::pcie::PcieAxiAdapter::detach_from_endpoint("pcie_ep_axi_cfg");
    REQUIRE(tlm::pcie::PcieAxiAdapter::for_endpoint("pcie_ep_axi_cfg") == nullptr);
}

TEST_CASE("PcieEndpointTLM: without axi_adapter config no adapter attached",
          "[axi][adapter][basic][compose][regression]") {
    tlm::gpu::PcieEndpointTLM ep("pcie_ep_axi_plain", nullptr);
    ep.init();
    ep.set_config(json::object()); // 无 axi_adapter

    REQUIRE(tlm::pcie::PcieAxiAdapter::for_endpoint("pcie_ep_axi_plain") == nullptr);
}

TEST_CASE("PcieEndpointIP: axi_adapter config binds real endpoint pointer (M3)",
          "[axi][adapter][basic][compose][endpoint-ip]") {
    EventQueue eq;
    tlm::pcie::PcieEndpointIP ep("pcie_ep_ip_axi_cfg", &eq);
    ep.init();

    json cfg;
    cfg["axi_adapter"] = json::object();
    ep.set_config(cfg); // → on_config_loaded → attach_composition → attach + set_endpoint(this)

    auto* ax = tlm::pcie::PcieAxiAdapter::for_endpoint("pcie_ep_ip_axi_cfg");
    REQUIRE(ax != nullptr);
    REQUIRE(ax->endpoint() == &ep); // M3: composition 路径必须回填真实 EP 指针
    REQUIRE(ax->event_queue() == &eq);

    tlm::pcie::PcieAxiAdapter::detach_from_endpoint("pcie_ep_ip_axi_cfg");
    REQUIRE(tlm::pcie::PcieAxiAdapter::for_endpoint("pcie_ep_ip_axi_cfg") == nullptr);
}
