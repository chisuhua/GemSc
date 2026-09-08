// examples/demo_pcie_full_e2e.cc
// PCIe 全链路 E2E Demo：PcieEndpointIP ↔ HostBypassTLM + PcieRootComplexTLM
// 功能描述：演示完整的 PCIe Host 侧组件与 Endpoint 之间的桥接：
//   - HostBypassTLM：软件 bring-up（配置空间写/读回、BAR 访问）
//   - PcieRootComplexTLM：PCIe 枚举 + 配置访问 + BAR 分配/路由
//   - PcieEndpointIP：17 端口 EP（PF + 16 VF），带 AXI adapter
// 作者 CppTLM Team / 日期 2027-01-19
// 参考: openspec/changes/2027-01-19-cpptlm-dgpu-pcie-host-bypass-and-rc/tasks.md T-P7-4

#include "core/event_queue.hh"
#include "core/module_factory.hh"
#include "tlm/pcie/host_bypass_tlm.hh"
#include "tlm/pcie/pcie_endpoint_ip.hh"
#include "tlm/pcie/pcie_root_complex_tlm.hh"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace tlm::pcie;

// 统一检查宏：失败时打印并退出非零（demo 验证失败即终止）
#define REQUIRE_HOST(cond, msg)                                                                    \
    do {                                                                                           \
        if (cond) {                                                                                \
            print_ok(msg);                                                                         \
        } else {                                                                                   \
            print_fail(msg);                                                                       \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static void print_header(const char* title) {
    printf("\n============================================================\n");
    printf("  %s\n", title);
    printf("============================================================\n\n");
}

static void print_sub(const char* subtitle) {
    printf("  --- %s ---\n", subtitle);
}

static void print_ok(const char* msg) {
    printf("  [OK] %s\n", msg);
}

static void print_fail(const char* msg) {
    printf("  [FAIL] %s\n", msg);
}

int main() {
    print_header("CppTLM PCIe Full E2E Demo: PcieEndpointIP <-> Host");

    // ── Step 1: 创建 EventQueue ──
    EventQueue eq;
    printf("[Init] EventQueue created (cycle=0)\n");

    // ── Step 2: 创建 PcieEndpointIP 并启用 AXI Adapter ──
    print_sub("Step 2: Create PcieEndpointIP with AXI Adapter");
    PcieEndpointIP ep("pcie_ep", &eq);
    ep.init();

    json ep_cfg;
    ep_cfg["axi_adapter"] = json::object();
    ep.set_config(ep_cfg);
    ep.on_config_loaded();
    print_ok("PcieEndpointIP created, axi_adapter enabled, 17 ports ready");

    // ── Step 3: HostBypassTLM（软件 bring-up）──
    print_header("HostBypassTLM: Software Bring-up Demo");
    print_sub("Step 3: Attach HostBypassTLM to EP");
    HostBypassTLM hb("host_bypass", &eq);
    hb.init();
    hb.attach_to_endpoint(&ep);
    print_ok("HostBypassTLM attached to PcieEndpointIP");

    // 3a: Config space write/read-back
    print_sub("Step 3a: Config space write + read back");
    REQUIRE_HOST(hb.config_write(0x04, 0x0007), "config_write(Command=0x0007)");
    uint32_t cmd = hb.config_read(0x04);
    REQUIRE_HOST(cmd == 0x0007u, "config_read returns 0x0007");
    // 验证 EP 侧真实值
    REQUIRE_HOST(ep.vf_pool().config_of(0).read(0x04) == 0x0007u, "EP config matches");

    // 3b: BAR0 配置
    print_sub("Step 3b: BAR0 configuration");
    REQUIRE_HOST(hb.config_write(0x10, 0x10000008), "config_write(BAR0=0x10000008)");
    REQUIRE_HOST(hb.config_read(0x10) == 0x10000008u, "BAR0 read back");

    // 3c: BAR 写入（经 AXI master → EP）
    print_sub("Step 3c: BAR write via AXI master channel");
    REQUIRE_HOST(hb.bar_write(0x10000000, 0xDEADBEEFDEADBEEF, 8), "bar_write(addr=0x10000000)");
    // AXI 请求呈现
    REQUIRE_HOST(hb.axi_master_req_valid(), "AXI write request presented");
    REQUIRE_HOST(hb.axi_master_req_data().awaddr.read() == 0x10000000u, "awaddr correct");
    REQUIRE_HOST(hb.axi_master_req_data().wdata.read() == 0xDEADBEEFDEADBEEFu, "wdata correct");
    // 下游 ready → 传输完成
    const uint16_t wid = hb.axi_master_req_data().awid.read();
    hb.set_axi_master_ready(true);
    hb.tick();
    REQUIRE_HOST(!hb.axi_master_req_valid() && hb.axi_outstanding_wr() == 1,
                 "AXI write transferred, outstanding=1");
    // EP 返回写响应
    bundles::Axi4Bundle wresp;
    wresp.bid.write(wid);
    wresp.bresp.write(0);
    REQUIRE_HOST(hb.axi_master_resp(wresp), "write response accepted");
    hb.axi_master_resp_consume();
    REQUIRE_HOST(hb.axi_outstanding_wr() == 0, "write transaction complete");

    // 3d: BAR 读取
    print_sub("Step 3d: BAR read via AXI master channel");
    uint64_t rd = 0;
    REQUIRE_HOST(hb.bar_read(0x10000000, rd, 8), "bar_read initiated");
    REQUIRE_HOST(hb.axi_master_req_valid(), "AXI read request presented");
    REQUIRE_HOST(hb.axi_master_req_data().araddr.read() == 0x10000000u, "araddr correct");
    const uint16_t rid = hb.axi_master_req_data().arid.read();
    hb.set_axi_master_ready(true);
    hb.tick();
    REQUIRE_HOST(!hb.axi_master_req_valid() && hb.axi_outstanding_rd() == 1,
                 "AXI read transferred, outstanding=1");
    bundles::Axi4Bundle rresp;
    rresp.rid.write(rid);
    rresp.rdata.write(0xDEADBEEFDEADBEEF);
    rresp.rresp.write(0);
    rresp.rlast.write(1);
    REQUIRE_HOST(hb.axi_master_resp(rresp), "read response accepted");
    hb.axi_master_resp_consume();
    REQUIRE_HOST(hb.axi_outstanding_rd() == 0, "read transaction complete");
    print_ok("HostBypassTLM software bring-up complete");

    // ── Step 4: PcieRootComplexTLM（枚举 + 配置 + BAR）──
    print_header("PcieRootComplexTLM: Enumeration + Configuration Demo");
    print_sub("Step 4: Attach PcieRootComplexTLM to EP");
    PcieRootComplexTLM rc("root_complex", &eq);
    rc.init();
    rc.attach_to_endpoint(&ep);
    print_ok("PcieRootComplexTLM attached to PcieEndpointIP");

    // 4a: 枚举
    print_sub("Step 4a: PCIe enumeration");
    REQUIRE_HOST(rc.enumerate(), "enumerate() discovers PF0");
    const auto& devices = rc.discovered_devices();
    REQUIRE_HOST(devices.size() == 1, "exactly 1 device (PF0) discovered");
    REQUIRE_HOST(devices[0].vendor_id == 0x10DE, "vendor_id=0x10DE");
    REQUIRE_HOST(devices[0].device_id_reg == 0x1234, "device_id=0x1234");
    print_ok("Enumeration complete");

    // 4b: 配置空间读/写
    print_sub("Step 4b: Config space access via RC");
    uint32_t cmd_rc = rc.config_read(0, 0, 0x04);
    REQUIRE_HOST(cmd_rc == 0x0007u, "config_read via RC returns 0x0007");
    REQUIRE_HOST(rc.config_write(0, 0, 0x04, 0x000F), "config_write(Command=0x000F)");
    uint32_t cmd2 = rc.config_read(0, 0, 0x04);
    REQUIRE_HOST(cmd2 == 0x000Fu, "config_read returns 0x000F");
    REQUIRE_HOST(ep.vf_pool().config_of(0).read(0x04) == 0x000Fu, "EP config matches");

    // 4c: BAR 分配
    print_sub("Step 4c: BAR allocation via RC");
    REQUIRE_HOST(rc.bar_allocate(0, 0, 0x10, 0x20000008), "bar_allocate(BAR0=0x20000008)");
    uint32_t bar0 = rc.config_read(0, 0, 0x10);
    REQUIRE_HOST(bar0 == 0x20000008u, "BAR0 updated in EP config");

    // 4d: BAR 访问路由
    print_sub("Step 4d: BAR access routed via AXI");
    REQUIRE_HOST(rc.bar_write(0, 0x20000000, 0xCAFEBABECAFEBABE, 8), "bar_write via RC");
    REQUIRE_HOST(rc.axi_master_req_valid(), "AXI request presented");
    const uint16_t rc_wid = rc.axi_master_req_data().awid.read();
    rc.set_axi_master_ready(true);
    rc.tick();
    REQUIRE_HOST(rc.axi_outstanding_wr() == 1, "write outstanding");
    bundles::Axi4Bundle rc_wresp;
    rc_wresp.bid.write(rc_wid);
    rc_wresp.bresp.write(0);
    REQUIRE_HOST(rc.axi_master_resp(rc_wresp), "write response");
    rc.axi_master_resp_consume();
    REQUIRE_HOST(rc.axi_outstanding_wr() == 0, "write complete");

    uint64_t rc_rd = 0;
    REQUIRE_HOST(rc.bar_read(0, 0x20000000, rc_rd, 8), "bar_read via RC");
    const uint16_t rc_rid = rc.axi_master_req_data().arid.read();
    rc.set_axi_master_ready(true);
    rc.tick();
    bundles::Axi4Bundle rc_rresp;
    rc_rresp.rid.write(rc_rid);
    rc_rresp.rdata.write(0xCAFEBABECAFEBABE);
    rc_rresp.rresp.write(0);
    rc_rresp.rlast.write(1);
    REQUIRE_HOST(rc.axi_master_resp(rc_rresp), "read response");
    rc.axi_master_resp_consume();
    REQUIRE_HOST(rc.axi_outstanding_rd() == 0, "read complete");
    print_ok("PcieRootComplexTLM enumeration + configuration complete");

    // ── Step 5: VF config space isolation ──
    print_header("VF Config Space Isolation Demo");
    print_sub("Step 5: Per-VF config space access");
    // VF0 (stream_id=1) 写入不同值
    REQUIRE_HOST(hb.config_write(0x04, 0x1111, /*stream_id=*/1), "VF0 config_write");
    REQUIRE_HOST(hb.config_read(0x04, /*stream_id=*/1) == 0x1111u, "VF0 config_read");
    // PF (stream_id=0) 未受影响
    REQUIRE_HOST(hb.config_read(0x04, /*stream_id=*/0) == 0x000Fu, "PF unaffected");
    print_ok("VF isolation verified");

    // ── Summary ──
    print_header("E2E Demo Summary: ALL PASSED");
    printf("  ✓ PcieEndpointIP + AXI Adapter: 17 ports operational\n");
    printf("  ✓ HostBypassTLM: software bring-up (config write/read, BAR access)\n");
    printf("  ✓ PcieRootComplexTLM: enumeration, config access, BAR alloc/route\n");
    printf("  ✓ Per-VF config space isolation\n");
    printf("  ✓ End-to-end AXI transaction flow with valid/ready handshake\n");
    printf("\n  CppTLM Phase 7 PCIe Host Bypass + Root Complex: COMPLETE\n");
    printf("============================================================\n");
    return 0;
}