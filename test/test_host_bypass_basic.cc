// test/test_host_bypass_basic.cc
// HostBypassTLM 基础桥接测试 (T-P7-1)
// 功能：验证 HostBypassTLM 作为独立组件桥接 AXI ↔ PcieEndpointIP
//       软件经 HostBypassTLM 发起对 EP 的访问，事务经 AXI 送达 PcieEndpointIP
//       响应正确返回，无丢事务
// 作者 CppTLM Team / 日期 2027-01-19
// 参考: openspec/changes/2027-01-19-cpptlm-dgpu-pcie-host-bypass-and-rc/tasks.md T-P7-1

#include "bundles/axi4_bundles_tlm.hh"
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "framework/axi4_stream_adapter.hh"
#include "tlm/pcie/host_bypass_tlm.hh"
#include "tlm/pcie/pcie_endpoint_ip.hh"

#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>

using namespace bundles;
using namespace tlm::pcie;
using json = nlohmann::json;

TEST_CASE("HostBypassTLM: create and attach to endpoint", "[pcie][host-bypass][basic]") {
    EventQueue eq;
    PcieEndpointIP ep("pcie_ep_host_bypass", &eq);
    ep.init();

    json cfg;
    cfg["axi_adapter"] = json::object();
    ep.set_config(cfg);
    ep.on_config_loaded();

    HostBypassTLM hb("host_bypass_0", &eq);
    hb.init();
    hb.attach_to_endpoint(&ep);

    REQUIRE(hb.endpoint() == &ep);
    REQUIRE(hb.event_queue() == &eq);
    REQUIRE(hb.is_attached() == true);
}

TEST_CASE("HostBypassTLM: master write through AXI bridge reaches EP",
          "[pcie][host-bypass][basic][master]") {
    EventQueue eq;
    PcieEndpointIP ep("pcie_ep_hb_master", &eq);
    ep.init();

    json cfg;
    cfg["axi_adapter"] = json::object();
    ep.set_config(cfg);
    ep.on_config_loaded();

    HostBypassTLM hb("host_bypass_master", &eq);
    hb.init();
    hb.attach_to_endpoint(&ep);

    // EP 发起写请求经 HostBypassTLM 的 AXI master 端口
    Axi4Bundle req;
    req.awid.write(0x5);
    req.awaddr.write(0x1000);
    req.awlen.write(0);
    req.awsize.write(3);
    req.wdata.write(0xDEADBEEF);
    req.wlast.write(1);

    REQUIRE(hb.axi_master_req(req) == true);

    // 下游 ready=1, tick → 请求传输
    hb.set_axi_master_ready(true);
    hb.tick();

    // 请求已传输
    REQUIRE(hb.axi_master_req_valid() == false);
    REQUIRE(hb.axi_outstanding_wr() == 1);

    // EP 通过 AXI adapter 返回写响应
    Axi4Bundle resp;
    resp.bid.write(0x5);
    resp.bresp.write(0);
    REQUIRE(hb.axi_master_resp(resp) == true);
    REQUIRE(hb.axi_master_resp_valid() == true);
    REQUIRE(hb.axi_master_resp_data().bid.read() == 0x5);

    // EP 消费响应
    hb.axi_master_resp_consume();
    REQUIRE(hb.axi_master_resp_valid() == false);
    REQUIRE(hb.axi_outstanding_wr() == 0);
}

TEST_CASE("HostBypassTLM: master read through AXI bridge returns rdata",
          "[pcie][host-bypass][basic][master]") {
    EventQueue eq;
    PcieEndpointIP ep("pcie_ep_hb_master_rd", &eq);
    ep.init();

    json cfg;
    cfg["axi_adapter"] = json::object();
    ep.set_config(cfg);
    ep.on_config_loaded();

    HostBypassTLM hb("host_bypass_master_rd", &eq);
    hb.init();
    hb.attach_to_endpoint(&ep);

    Axi4Bundle req;
    req.arid.write(0x7);
    req.araddr.write(0x2000);
    req.arlen.write(0);
    req.arsize.write(3);

    REQUIRE(hb.axi_master_req(req) == true);
    hb.set_axi_master_ready(true);
    hb.tick();

    REQUIRE(hb.axi_master_req_valid() == false);
    REQUIRE(hb.axi_outstanding_rd() == 1);

    // EP 返回读数据
    Axi4Bundle r;
    r.rid.write(0x7);
    r.rdata.write(0xCAFEBABE);
    r.rresp.write(0);
    r.rlast.write(1);
    REQUIRE(hb.axi_master_resp(r) == true);
    REQUIRE(hb.axi_master_resp_valid() == true);
    REQUIRE(hb.axi_master_resp_data().rid.read() == 0x7);
    REQUIRE(hb.axi_master_resp_data().rdata.read() == 0xCAFEBABE);

    hb.axi_master_resp_consume();
    REQUIRE(hb.axi_outstanding_rd() == 0);
}

TEST_CASE("HostBypassTLM: slave_in accepts SoC request and EP responds",
          "[pcie][host-bypass][basic][slave]") {
    EventQueue eq;
    PcieEndpointIP ep("pcie_ep_hb_slave", &eq);
    ep.init();

    json cfg;
    cfg["axi_adapter"] = json::object();
    ep.set_config(cfg);
    ep.on_config_loaded();

    HostBypassTLM hb("host_bypass_slave", &eq);
    hb.init();
    hb.attach_to_endpoint(&ep);

    // SoC 发起写请求进入 Endpoint
    Axi4Bundle req;
    req.awid.write(0x3);
    req.awaddr.write(0x3000);
    req.wdata.write(0x1234);
    req.wlast.write(1);

    REQUIRE(hb.axi_slave_req(req) == true);
    hb.set_axi_slave_ready(true);
    hb.tick();

    REQUIRE(hb.axi_slave_req_valid() == true);
    REQUIRE(hb.axi_slave_req_data().awaddr.read() == 0x3000);
    hb.axi_slave_req_consume();

    // EP 返回写响应
    Axi4Bundle resp;
    resp.bid.write(0x3);
    resp.bresp.write(0);
    REQUIRE(hb.axi_slave_resp(resp) == true);
    REQUIRE(hb.axi_slave_resp_valid() == true);
    REQUIRE(hb.axi_slave_resp_data().bid.read() == 0x3);
    hb.axi_slave_resp_consume();
}

TEST_CASE("HostBypassTLM: cfg_slave_in AXI4-Lite config access",
          "[pcie][host-bypass][basic][cfg]") {
    EventQueue eq;
    PcieEndpointIP ep("pcie_ep_hb_cfg", &eq);
    ep.init();

    json cfg;
    cfg["axi_adapter"] = json::object();
    ep.set_config(cfg);
    ep.on_config_loaded();

    HostBypassTLM hb("host_bypass_cfg", &eq);
    hb.init();
    hb.attach_to_endpoint(&ep);

    // SoC 写配置寄存器
    Axi4LiteBundle wr;
    wr.awaddr.write(0x04);
    wr.awid.write(0x1);
    wr.wdata.write(0xABCD);
    wr.wstrb.write(0xF);

    REQUIRE(hb.axi_cfg_req(wr) == true);
    hb.set_axi_cfg_ready(true);
    hb.tick();
    REQUIRE(hb.axi_cfg_req_valid() == true);
    REQUIRE(hb.axi_cfg_req_data().awaddr.read() == 0x04);
    hb.axi_cfg_req_consume();

    // EP 返回写响应
    Axi4LiteBundle wresp;
    wresp.awid.write(0x1);
    wresp.bresp.write(0);
    REQUIRE(hb.axi_cfg_resp(wresp) == true);
    REQUIRE(hb.axi_cfg_resp_valid() == true);
    hb.axi_cfg_resp_consume();

    // 读配置
    Axi4LiteBundle rd;
    rd.araddr.write(0x04);
    rd.arid.write(0x2);
    REQUIRE(hb.axi_cfg_req(rd) == true);
    hb.set_axi_cfg_ready(true);
    hb.tick();
    REQUIRE(hb.axi_cfg_req_valid() == true);
    REQUIRE(hb.axi_cfg_req_data().araddr.read() == 0x04);
    hb.axi_cfg_req_consume();

    Axi4LiteBundle rresp;
    rresp.arid.write(0x2);
    rresp.rdata.write(0xABCD);
    rresp.rresp.write(0);
    REQUIRE(hb.axi_cfg_resp(rresp) == true);
    REQUIRE(hb.axi_cfg_resp_valid() == true);
    REQUIRE(hb.axi_cfg_resp_data().rdata.read() == 0xABCD);
    hb.axi_cfg_resp_consume();
}

TEST_CASE("HostBypassTLM: detach cleans up endpoint reference",
          "[pcie][host-bypass][basic][detach]") {
    EventQueue eq;
    PcieEndpointIP ep("pcie_ep_hb_detach", &eq);
    ep.init();

    json cfg;
    cfg["axi_adapter"] = json::object();
    ep.set_config(cfg);
    ep.on_config_loaded();

    HostBypassTLM hb("host_bypass_detach", &eq);
    hb.init();
    hb.attach_to_endpoint(&ep);
    REQUIRE(hb.is_attached() == true);

    hb.detach();
    REQUIRE(hb.is_attached() == false);
    REQUIRE(hb.endpoint() == nullptr);
}