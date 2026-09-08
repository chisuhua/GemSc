// test/test_host_bypass_software_bringup.cc
// HostBypassTLM 软件 bring-up 场景测试 (T-P7-2)
// 功能：验证 HostBypassTLM 支持配置空间写/读回、BAR 访问
//       模拟软件 bring-up：
//         - 配置空间写 → 值实际写入 EP 配置空间 → 读回值正确
//         - BAR 访问经 AXI 路由到 EP（地址/ID 正确呈现，响应返回）
// 作者 CppTLM Team / 日期 2027-01-19
// 参考: openspec/changes/2027-01-19-cpptlm-dgpu-pcie-host-bypass-and-rc/tasks.md T-P7-2

#include "bundles/axi4_bundles_tlm.hh"
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "tlm/pcie/host_bypass_tlm.hh"
#include "tlm/pcie/pcie_endpoint_ip.hh"

#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>

using namespace bundles;
using namespace tlm::pcie;
using json = nlohmann::json;

TEST_CASE("HostBypassTLM: config space write then read back real EP value",
          "[pcie][host-bypass][software][config]") {
    EventQueue eq;
    PcieEndpointIP ep("pcie_ep_sw_bringup", &eq);
    ep.init();

    json cfg;
    cfg["axi_adapter"] = json::object();
    ep.set_config(cfg);
    ep.on_config_loaded();

    HostBypassTLM hb("host_bypass_sw", &eq);
    hb.init();
    hb.attach_to_endpoint(&ep);

    // 软件写配置寄存器 0x04 (Command Register) = 0x0007 (Memory + IO + BusMaster)
    // 经 HostBypassTLM → 直达 EP 配置空间
    REQUIRE(hb.config_write(0x04, 0x0007) == true);

    // 读回：值应实际存储于 EP 配置空间
    REQUIRE(hb.config_read(0x04) == 0x0007u);

    // 直接验证 EP 侧真实值（单一真源）
    REQUIRE(ep.vf_pool().config_of(0).read(0x04) == 0x0007u);
}

TEST_CASE("HostBypassTLM: config space write does not touch other offsets",
          "[pcie][host-bypass][software][config]") {
    EventQueue eq;
    PcieEndpointIP ep("pcie_ep_sw_cfg_isolation", &eq);
    ep.init();

    json cfg;
    cfg["axi_adapter"] = json::object();
    ep.set_config(cfg);
    ep.on_config_loaded();

    HostBypassTLM hb("host_bypass_sw_iso", &eq);
    hb.init();
    hb.attach_to_endpoint(&ep);

    // 写 0x18 (BAR3) 不应影响 0x04 (Command)
    REQUIRE(hb.config_write(0x18, 0xDEAD0001) == true);
    REQUIRE(hb.config_read(0x18) == 0xDEAD0001u);
    REQUIRE(hb.config_read(0x04) == 0x10u); // Capabilities bit（未动）
}

TEST_CASE("HostBypassTLM: config access routes to per-VF config space",
          "[pcie][host-bypass][software][config][vf]") {
    EventQueue eq;
    PcieEndpointIP ep("pcie_ep_sw_cfg_vf", &eq);
    ep.init();

    json cfg;
    cfg["axi_adapter"] = json::object();
    ep.set_config(cfg);
    ep.on_config_loaded();

    HostBypassTLM hb("host_bypass_sw_vf", &eq);
    hb.init();
    hb.attach_to_endpoint(&ep);

    // VF0 (stream_id=1) 与 PF (stream_id=0) 配置空间独立
    REQUIRE(hb.config_write(0x04, 0x1111, /*stream_id=*/1) == true);
    REQUIRE(hb.config_read(0x04, /*stream_id=*/1) == 0x1111u);
    REQUIRE(hb.config_read(0x04, /*stream_id=*/0) == 0x10u); // PF 未受影响
}

TEST_CASE("HostBypassTLM: BAR write routed via AXI master channel",
          "[pcie][host-bypass][software][bar]") {
    EventQueue eq;
    PcieEndpointIP ep("pcie_ep_bar_access", &eq);
    ep.init();

    json cfg;
    cfg["axi_adapter"] = json::object();
    ep.set_config(cfg);
    ep.on_config_loaded();

    HostBypassTLM hb("host_bypass_bar", &eq);
    hb.init();
    hb.attach_to_endpoint(&ep);

    // 软件写 BAR0 地址空间 0x10000000（8 字节）
    // 应经 AXI master 通道路由到 EP（地址/ID 正确呈现）
    REQUIRE(hb.bar_write(0x10000000, 0xDEADBEEFDEADBEEF, /*bytes=*/8) == true);

    // AXI 请求呈现（awaddr 落在 BAR 范围，wdata 正确）
    REQUIRE(hb.axi_master_req_valid() == true);
    REQUIRE(hb.axi_master_req_data().awaddr.read() == 0x10000000u);
    REQUIRE(hb.axi_master_req_data().wdata.read() == 0xDEADBEEFDEADBEEFu);

    // 下游（EP）ready → 传输完成
    const uint16_t wid = hb.axi_master_req_data().awid.read();
    hb.set_axi_master_ready(true);
    hb.tick();
    REQUIRE(hb.axi_master_req_valid() == false);
    REQUIRE(hb.axi_outstanding_wr() == 1);

    // EP 返回写响应 → 事务完成
    Axi4Bundle wresp;
    wresp.bid.write(wid);
    wresp.bresp.write(0);
    REQUIRE(hb.axi_master_resp(wresp) == true);
    hb.axi_master_resp_consume();
    REQUIRE(hb.axi_outstanding_wr() == 0);
}

TEST_CASE("HostBypassTLM: BAR read routed via AXI master channel returns data",
          "[pcie][host-bypass][software][bar][read]") {
    EventQueue eq;
    PcieEndpointIP ep("pcie_ep_bar_mem", &eq);
    ep.init();

    json cfg;
    cfg["axi_adapter"] = json::object();
    ep.set_config(cfg);
    ep.on_config_loaded();

    HostBypassTLM hb("host_bypass_bar_mem", &eq);
    hb.init();
    hb.attach_to_endpoint(&ep);

    uint64_t data = 0;
    REQUIRE(hb.bar_read(0x10000000, data, /*bytes=*/8) == true);

    // AXI 读请求呈现（araddr 落在 BAR 范围，arid 非 0）
    REQUIRE(hb.axi_master_req_valid() == true);
    REQUIRE(hb.axi_master_req_data().araddr.read() == 0x10000000u);
    const uint16_t rid = hb.axi_master_req_data().arid.read();
    REQUIRE(rid != 0);

    hb.set_axi_master_ready(true);
    hb.tick();
    REQUIRE(hb.axi_master_req_valid() == false);
    REQUIRE(hb.axi_outstanding_rd() == 1);

    // EP 返回读数据 → bar_read 完成
    Axi4Bundle rresp;
    rresp.rid.write(rid);
    rresp.rdata.write(0xCAFEBABECAFEBABE);
    rresp.rresp.write(0);
    rresp.rlast.write(1);
    REQUIRE(hb.axi_master_resp(rresp) == true);
    hb.axi_master_resp_consume();
    REQUIRE(hb.axi_outstanding_rd() == 0);
}

TEST_CASE("HostBypassTLM: software bring-up full sequence (config + BAR)",
          "[pcie][host-bypass][software][e2e]") {
    EventQueue eq;
    PcieEndpointIP ep("pcie_ep_sw_full", &eq);
    ep.init();

    json cfg;
    cfg["axi_adapter"] = json::object();
    ep.set_config(cfg);
    ep.on_config_loaded();

    HostBypassTLM hb("host_bypass_sw_full", &eq);
    hb.init();
    hb.attach_to_endpoint(&ep);

    // 1. 使能命令寄存器（BusMaster）
    REQUIRE(hb.config_write(0x04, 0x0007) == true);
    REQUIRE(hb.config_read(0x04) == 0x0007u);

    // 2. 配置 BAR0 为 64-bit prefetchable memory
    REQUIRE(hb.config_write(0x10, 0x10000008) == true);
    REQUIRE(hb.config_read(0x10) == 0x10000008u);

    // 3. BAR0 空间写 → 经 AXI 路由到 EP
    REQUIRE(hb.bar_write(0x10000000, 0xABCDEF0123456789, 8) == true);
    REQUIRE(hb.axi_master_req_data().awaddr.read() == 0x10000000u);
    const uint16_t wid = hb.axi_master_req_data().awid.read();
    hb.set_axi_master_ready(true);
    hb.tick();
    Axi4Bundle wresp;
    wresp.bid.write(wid);
    wresp.bresp.write(0);
    REQUIRE(hb.axi_master_resp(wresp) == true);
    hb.axi_master_resp_consume();

    // 4. BAR0 空间读回 → 经 AXI 路由到 EP
    uint64_t rd = 0;
    REQUIRE(hb.bar_read(0x10000000, rd, 8) == true);
    const uint16_t rid = hb.axi_master_req_data().arid.read();
    hb.set_axi_master_ready(true);
    hb.tick();
    Axi4Bundle rresp;
    rresp.rid.write(rid);
    rresp.rdata.write(0xABCDEF0123456789);
    rresp.rresp.write(0);
    rresp.rlast.write(1);
    REQUIRE(hb.axi_master_resp(rresp) == true);
    hb.axi_master_resp_consume();
    REQUIRE(hb.axi_outstanding_wr() == 0);
    REQUIRE(hb.axi_outstanding_rd() == 0);
}

TEST_CASE("HostBypassTLM: config access returns failure when not attached",
          "[pcie][host-bypass][software][detach]") {
    EventQueue eq;
    HostBypassTLM hb("host_bypass_no_ep", &eq);
    hb.init();

    // 未挂接 EP → 配置访问失败（不崩溃）
    REQUIRE(hb.config_write(0x04, 0x0007) == false);
    REQUIRE(hb.config_read(0x04) == 0xFFFFFFFFu);
    REQUIRE(hb.bar_write(0x10000000, 0, 8) == false);

    uint64_t data = 0;
    REQUIRE(hb.bar_read(0x10000000, data, 8) == false);
}