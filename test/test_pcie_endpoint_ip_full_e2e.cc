// test/test_pcie_endpoint_ip_full_e2e.cc
// PcieEndpointIP 全链路 E2E 测试 (Phase 8 整合交付, 解决 Phase 7 Oracle M1)
// 功能：验证 HostBypassTLM/RC ↔ PcieEndpointIP 的 AXI 数据路径真实闭环：
//   - HostBypass 发起的 AXI 写/读请求经真实 AXI 数据路径被 EP 消费
//   - EP 内部真实处理（配置空间/bar_router 状态真实变化）
//   - EP 产生真实响应（不是测试手动塞响应）
// 作者 CppTLM Team / 日期 2027-02-09
// 参考: openspec/changes/2027-02-09-cpptlm-dgpu-pcie-ip-integration/tasks.md T-P8-3
//       openspec/changes/2026-09-01-cpptlm-dgpu-pcie-ip-microarch/design.md §2/§3

#include "bundles/axi4_bundles_tlm.hh"
#include "bundles/pcie_bundles_tlm.hh"
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "framework/axi4_stream_adapter.hh"
#include "tlm/pcie/host_bypass_tlm.hh"
#include "tlm/pcie/pcie_axi_adapter_tlm.hh"
#include "tlm/pcie/pcie_endpoint_ip.hh"
#include "tlm/pcie/pcie_root_complex_tlm.hh"

#include <cstdint>
#include <nlohmann/json.hpp>

using namespace bundles;
using namespace tlm::pcie;
using json = nlohmann::json;

namespace {

    // 建立 HostBypassTLM ↔ PcieEndpointIP 真实 AXI 数据路径闭环。
    // 关键：把 hb 的 AXI master 通道（hb.axi()）与 ep 的 PcieAxiAdapter 的
    // AXI slave 通道（ep_axi.axi()）对接——请求经 hb.master_req() 注入，
    // 由 ep 侧 slave 消费并产生真实响应回传 hb。
    // Phase 8 M1 修复：PcieEndpointIP::tick() 驱动 PcieAxiAdapter 处理 slave 请求。
    struct FullE2EFixture {
        EventQueue eq;
        PcieEndpointIP ep;
        HostBypassTLM hb;

        FullE2EFixture() : ep("pcie_ep_full_e2e", &eq), hb("host_bypass_full_e2e", &eq) {
            ep.init();

            // 启用 EP 的 AXI Adapter（含 AXI4Mapper 注入）
            json cfg;
            cfg["axi_adapter"] = json::object();
            cfg["axi_adapter"]["axi4_mapper_inject"] = true;
            cfg["link_layer"]["enabled"] = true;
            cfg["link_layer"]["bypass_mode"] = "Bypass";
            ep.set_config(cfg);
            ep.on_config_loaded();

            hb.init();
            hb.attach_to_endpoint(&ep);
        }
    };

} // namespace

TEST_CASE("Phase8 E2E: EP 真实消费 HostBypass 配置空间写 (M1 closed)",
          "[pcie][axi][e2e][phase8][config]") {
    EventQueue eq;
    PcieEndpointIP ep("pcie_ep_e2e_cfg", &eq);
    ep.init();

    json cfg;
    cfg["axi_adapter"] = json::object();
    ep.set_config(cfg);
    ep.on_config_loaded();

    HostBypassTLM hb("host_bypass_e2e_cfg", &eq);
    hb.init();
    hb.attach_to_endpoint(&ep);

    // HostBypass 软件写配置寄存器 0x04 (Command Register) = 0x0007
    // 经 AXI 数据路径（非直接 config_write 直写）：
    //   hb.axi_master_req() → EP AXI slave 消费 → EP 配置空间真实写入
    // 走真实 AXI 通道注入请求（如同软件经 AXI 桥访问 EP）
    REQUIRE(hb.axi_master_req_valid() == false);

    // 构造一个 CFG 写请求经 AXI 通道（awaddr 编码配置偏移 0x04, stream 0）
    Axi4Bundle wreq;
    wreq.awid.write(0x10);
    wreq.awaddr.write(0x04); // 配置偏移
    wreq.awlen.write(0);
    wreq.awsize.write(2); // 4 字节
    wreq.awburst.write(1);
    wreq.wdata.write(0x0007);
    wreq.wstrb.write(0xF);
    wreq.wlast.write(1);

    REQUIRE(hb.axi_master_req(wreq) == true);
    hb.set_axi_master_ready(true);

    // 驱动 EP tick()：PcieAxiAdapter 消费 slave 请求，EP 真实处理，产生响应
    // （每周期推进直至请求被 EP 消费并产生响应）
    for (int i = 0; i < 100 && (hb.axi_master_req_valid() || hb.axi_outstanding_wr() > 0); ++i) {
        ep.tick();
        hb.tick();
    }

    // EP 配置空间必须真实写入（不是测试直写）
    REQUIRE(ep.vf_pool().config_of(0).read(0x04) == 0x0007u);
    REQUIRE(hb.axi_master_resp_valid() == true);
    REQUIRE(hb.axi_master_resp_data().bid.read() == 0x10u);
    hb.axi_master_resp_consume();

    // 读回：经 AXI 通道发起读请求 → EP 返回真实配置值
    Axi4Bundle rreq;
    rreq.arid.write(0x20);
    rreq.araddr.write(0x04);
    rreq.arlen.write(0);
    rreq.arsize.write(2);
    rreq.arburst.write(1);
    REQUIRE(hb.axi_master_req(rreq) == true);
    hb.set_axi_master_ready(true);

    for (int i = 0; i < 100 && (hb.axi_master_req_valid() || hb.axi_outstanding_rd() > 0); ++i) {
        ep.tick();
        hb.tick();
    }

    // EP 返回真实读响应（不是测试手写注入）→ hb master_resp_valid
    // 响应数据 = EP 配置空间真实值 0x0007
    REQUIRE(hb.axi_master_resp_valid() == true);
    REQUIRE(hb.axi_master_resp_data().rdata.read() == 0x0007u);
    hb.axi_master_resp_consume();
}

TEST_CASE("Phase8 E2E: BAR 写经 AXI 到达 EP 内部处理后返回真实响应 (M1 closed)",
          "[pcie][axi][e2e][phase8][bar]") {
    EventQueue eq;
    PcieEndpointIP ep("pcie_ep_e2e_bar", &eq);
    ep.init();

    json cfg;
    cfg["axi_adapter"] = json::object();
    ep.set_config(cfg);
    ep.on_config_loaded();

    HostBypassTLM hb("host_bypass_e2e_bar", &eq);
    hb.init();
    hb.attach_to_endpoint(&ep);

    // 配置 BAR0 为 64-bit prefetchable (offset 0x10)
    // 经真实 AXI 配置写路径写入 BAR0 基址 0x10000000
    Axi4Bundle wcfg;
    wcfg.awid.write(0x30);
    wcfg.awaddr.write(0x10); // BAR0 配置偏移
    wcfg.awlen.write(0);
    wcfg.awsize.write(2);
    wcfg.awburst.write(1);
    wcfg.wdata.write(0x10000008);
    wcfg.wstrb.write(0xF);
    wcfg.wlast.write(1);
    REQUIRE(hb.axi_master_req(wcfg) == true);
    hb.set_axi_master_ready(true);
    for (int i = 0; i < 100 && (hb.axi_master_req_valid() || hb.axi_outstanding_wr() > 0); ++i) {
        ep.tick();
        hb.tick();
    }
    REQUIRE(ep.vf_pool().config_of(0).read(0x10) == 0x10000008u);
    REQUIRE(hb.axi_master_resp_valid() == true);
    REQUIRE(hb.axi_master_resp_data().bid.read() == 0x30u);
    hb.axi_master_resp_consume();

    // BAR0 空间写：经 AXI master 通道路由到 EP BAR 内部
    // （真实数据路径：请求经 hb.master_req → EP AXI slave 消费 → EP bar 内部处理）
    Axi4Bundle bw;
    bw.awid.write(0x31);
    bw.awaddr.write(0x10000000); // BAR0 空间
    bw.awlen.write(0);
    bw.awsize.write(3); // 8 字节
    bw.awburst.write(1);
    bw.wdata.write(0xDEADBEEFDEADBEEF);
    bw.wstrb.write(0xFF);
    bw.wlast.write(1);
    REQUIRE(hb.axi_master_req(bw) == true);
    hb.set_axi_master_ready(true);
    for (int i = 0; i < 100 && (hb.axi_master_req_valid() || hb.axi_outstanding_wr() > 0); ++i) {
        ep.tick();
        hb.tick();
    }
    REQUIRE(hb.axi_master_resp_valid() == true);
    REQUIRE(hb.axi_master_resp_data().bid.read() == 0x31u);
    hb.axi_master_resp_consume();

    // BAR 读回：经 AXI 发起读 → EP 内部 bar_router 返回真实值
    Axi4Bundle br;
    br.arid.write(0x32);
    br.araddr.write(0x10000000);
    br.arlen.write(0);
    br.arsize.write(3);
    br.arburst.write(1);
    REQUIRE(hb.axi_master_req(br) == true);
    hb.set_axi_master_ready(true);
    for (int i = 0; i < 100 && (hb.axi_master_req_valid() || hb.axi_outstanding_rd() > 0); ++i) {
        ep.tick();
        hb.tick();
    }

    // EP 返回真实读响应（BAR 内部值，不是手写注入）
    // BAR slot 是 32-bit 寄存器 (per Oracle P1-2 hardening):
    //   rdata 高 32 bit 总是 0,低 32 bit = bar_store_[addr & ~3] 的 32-bit 值
    REQUIRE(hb.axi_master_resp_valid() == true);
    REQUIRE(hb.axi_master_resp_data().rdata.read() == 0xDEADBEEFu);
    hb.axi_master_resp_consume();
}

TEST_CASE("Phase8 E2E: PcieRootComplexTLM 枚举 PF0-only 后经 AXI 访问 EP (M2 doc'd)",
          "[pcie][axi][e2e][phase8][rc]") {
    EventQueue eq;
    PcieEndpointIP ep("pcie_ep_e2e_rc", &eq);
    ep.init();

    json cfg;
    cfg["axi_adapter"] = json::object();
    ep.set_config(cfg);
    ep.on_config_loaded();

    PcieRootComplexTLM rc("root_complex_e2e", &eq);
    rc.init();
    rc.attach_to_endpoint(&ep);

    // 枚举：RC 发现 PF0-only (Phase 7 Oracle M2 已知边界)
    REQUIRE(rc.enumerate() == true);
    REQUIRE(rc.discovered_devices().size() == 1u); // PF0-only
    REQUIRE(rc.discovered_devices()[0].vendor_id == 0x10DE);

    // RC 经 AXI master 通道发起写请求 → EP AXI slave 消费 → EP 真实处理
    Axi4Bundle wreq;
    wreq.awid.write(0x50);
    wreq.awaddr.write(0x04); // Command Register
    wreq.awlen.write(0);
    wreq.awsize.write(2);
    wreq.awburst.write(1);
    wreq.wdata.write(0x0007);
    wreq.wstrb.write(0xF);
    wreq.wlast.write(1);
    REQUIRE(rc.axi_master_req(wreq) == true);
    rc.set_axi_master_ready(true);
    for (int i = 0; i < 100 && (rc.axi_master_req_valid() || rc.axi_outstanding_wr() > 0); ++i) {
        ep.tick();
        rc.tick();
    }

    // EP 配置空间真实写入
    REQUIRE(ep.vf_pool().config_of(0).read(0x04) == 0x0007u);
}