// test/test_pcie_root_complex_enumeration.cc
// PcieRootComplexTLM PCIe 枚举测试 (T-P7-3)
// 功能：验证 PcieRootComplexTLM 支持设备/功能发现、配置空间读、BAR 分配
// 作者 CppTLM Team / 日期 2027-01-19
// 参考: openspec/changes/2027-01-19-cpptlm-dgpu-pcie-host-bypass-and-rc/tasks.md T-P7-3

#include "bundles/axi4_bundles_tlm.hh"
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "tlm/pcie/pcie_endpoint_ip.hh"
#include "tlm/pcie/pcie_root_complex_tlm.hh"

#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>

using namespace bundles;
using namespace tlm::pcie;
using json = nlohmann::json;

TEST_CASE("PcieRootComplexTLM: create and attach to endpoint", "[pcie][root-complex][enum]") {
    EventQueue eq;
    PcieEndpointIP ep("pcie_ep_rc", &eq);
    ep.init();

    json cfg;
    cfg["axi_adapter"] = json::object();
    ep.set_config(cfg);
    ep.on_config_loaded();

    PcieRootComplexTLM rc("root_complex_0", &eq);
    rc.init();
    rc.attach_to_endpoint(&ep);

    REQUIRE(rc.endpoint() == &ep);
    REQUIRE(rc.is_attached() == true);
}

TEST_CASE("PcieRootComplexTLM: enumerate device and function", "[pcie][root-complex][enum]") {
    EventQueue eq;
    PcieEndpointIP ep("pcie_ep_rc_enum", &eq);
    ep.init();

    json cfg;
    cfg["axi_adapter"] = json::object();
    ep.set_config(cfg);
    ep.on_config_loaded();

    PcieRootComplexTLM rc("root_complex_enum", &eq);
    rc.init();
    rc.attach_to_endpoint(&ep);

    // 执行枚举：发现设备/功能
    REQUIRE(rc.enumerate() == true);

    // 应发现 1 个设备（PF0，function 0）
    const auto& devices = rc.discovered_devices();
    REQUIRE(devices.size() == 1);
    REQUIRE(devices[0].device_id == 0);
    REQUIRE(devices[0].function == 0);
    REQUIRE(devices[0].vendor_id == 0x10DE);     // 默认 vendor_id
    REQUIRE(devices[0].device_id_reg == 0x1234); // 默认 device_id
    REQUIRE(devices[0].class_code != 0);
    REQUIRE(devices[0].revision_id == 0x01);
}

TEST_CASE("PcieRootComplexTLM: read config space after enumeration",
          "[pcie][root-complex][enum][config]") {
    EventQueue eq;
    PcieEndpointIP ep("pcie_ep_rc_cfg", &eq);
    ep.init();

    json cfg;
    cfg["axi_adapter"] = json::object();
    ep.set_config(cfg);
    ep.on_config_loaded();

    PcieRootComplexTLM rc("root_complex_cfg", &eq);
    rc.init();
    rc.attach_to_endpoint(&ep);

    REQUIRE(rc.enumerate() == true);

    // 读取 PF0 的配置空间
    uint32_t cmd_reg = rc.config_read(0, 0, 0x04); // device=0, function=0, offset=0x04
    REQUIRE(cmd_reg == 0x10u);                     // Capabilities bit

    uint32_t vendor = rc.config_read(0, 0, 0x00);
    // 寄存器 0x00 存储：[31:16]=device_id, [15:0]=vendor_id
    REQUIRE(vendor == 0x123410DEu); // device_id=0x1234, vendor_id=0x10DE

    uint32_t revision = rc.config_read(0, 0, 0x08);
    REQUIRE(revision == 0x01u); // revision_id
}

TEST_CASE("PcieRootComplexTLM: write config space and verify",
          "[pcie][root-complex][enum][config][write]") {
    EventQueue eq;
    PcieEndpointIP ep("pcie_ep_rc_cfg_wr", &eq);
    ep.init();

    json cfg;
    cfg["axi_adapter"] = json::object();
    ep.set_config(cfg);
    ep.on_config_loaded();

    PcieRootComplexTLM rc("root_complex_cfg_wr", &eq);
    rc.init();
    rc.attach_to_endpoint(&ep);

    REQUIRE(rc.enumerate() == true);

    // 写命令寄存器使能 BusMaster + Memory
    REQUIRE(rc.config_write(0, 0, 0x04, 0x0007) == true);

    // 读回验证
    uint32_t cmd_reg = rc.config_read(0, 0, 0x04);
    REQUIRE(cmd_reg == 0x0007u);

    // 直接验证 EP 侧值（单一真源）
    REQUIRE(ep.vf_pool().config_of(0).read(0x04) == 0x0007u);
}

TEST_CASE("PcieRootComplexTLM: allocate BAR and route access to EP",
          "[pcie][root-complex][enum][bar]") {
    EventQueue eq;
    PcieEndpointIP ep("pcie_ep_rc_bar", &eq);
    ep.init();

    json cfg;
    cfg["axi_adapter"] = json::object();
    ep.set_config(cfg);
    ep.on_config_loaded();

    PcieRootComplexTLM rc("root_complex_bar", &eq);
    rc.init();
    rc.attach_to_endpoint(&ep);

    REQUIRE(rc.enumerate() == true);

    // 分配 BAR0：64-bit prefetchable，大小 256MB (0x10000000)
    REQUIRE(rc.bar_allocate(0, 0, 0x10, 0x10000008) == true); // device=0, func=0, BAR0_offset=0x10

    // 验证 BAR 写入 EP 配置空间
    uint32_t bar0 = rc.config_read(0, 0, 0x10);
    REQUIRE(bar0 == 0x10000008u);

    // 通过 RC 访问 BAR 空间（经 AXI master 路由到 EP）
    uint64_t write_data = 0xDEADBEEFDEADBEEF;
    REQUIRE(rc.bar_write(0, 0x10000000, write_data, 8) == true);

    // AXI 请求呈现（awaddr 在 BAR 范围内，wdata 正确）
    REQUIRE(rc.axi_master_req_valid() == true);
    REQUIRE(rc.axi_master_req_data().awaddr.read() == 0x10000000u);
    REQUIRE(rc.axi_master_req_data().wdata.read() == write_data);

    // 下游 ready → 传输完成
    const uint16_t wid = rc.axi_master_req_data().awid.read();
    rc.set_axi_master_ready(true);
    rc.tick();
    REQUIRE(rc.axi_master_req_valid() == false);
    REQUIRE(rc.axi_outstanding_wr() == 1);

    // EP 返回写响应
    Axi4Bundle wresp;
    wresp.bid.write(wid);
    wresp.bresp.write(0);
    REQUIRE(rc.axi_master_resp(wresp) == true);
    rc.axi_master_resp_consume();
    REQUIRE(rc.axi_outstanding_wr() == 0);
}

TEST_CASE("PcieRootComplexTLM: bar_read routes via AXI and returns EP data",
          "[pcie][root-complex][enum][bar][read]") {
    EventQueue eq;
    PcieEndpointIP ep("pcie_ep_rc_bar_rd", &eq);
    ep.init();

    json cfg;
    cfg["axi_adapter"] = json::object();
    ep.set_config(cfg);
    ep.on_config_loaded();

    PcieRootComplexTLM rc("root_complex_bar_rd", &eq);
    rc.init();
    rc.attach_to_endpoint(&ep);

    REQUIRE(rc.enumerate() == true);
    REQUIRE(rc.bar_allocate(0, 0, 0x10, 0x10000008) == true);

    // 通过 RC 从 BAR 空间读取
    uint64_t read_data = 0;
    REQUIRE(rc.bar_read(0, 0x10000000, read_data, 8) == true);

    // AXI 读请求呈现
    REQUIRE(rc.axi_master_req_valid() == true);
    REQUIRE(rc.axi_master_req_data().araddr.read() == 0x10000000u);
    const uint16_t rid = rc.axi_master_req_data().arid.read();
    REQUIRE(rid != 0);

    rc.set_axi_master_ready(true);
    rc.tick();
    REQUIRE(rc.axi_master_req_valid() == false);
    REQUIRE(rc.axi_outstanding_rd() == 1);

    // EP 返回读数据
    Axi4Bundle rresp;
    rresp.rid.write(rid);
    rresp.rdata.write(0xCAFEBABECAFEBABE);
    rresp.rresp.write(0);
    rresp.rlast.write(1);
    REQUIRE(rc.axi_master_resp(rresp) == true);
    rc.axi_master_resp_consume();
    REQUIRE(rc.axi_outstanding_rd() == 0);
}

TEST_CASE("PcieRootComplexTLM: VF enumeration (per-VF config space)",
          "[pcie][root-complex][enum][vf]") {
    EventQueue eq;
    PcieEndpointIP ep("pcie_ep_rc_vf", &eq);
    ep.init();

    json cfg;
    cfg["axi_adapter"] = json::object();
    ep.set_config(cfg);
    ep.on_config_loaded();

    PcieRootComplexTLM rc("root_complex_vf", &eq);
    rc.init();
    rc.attach_to_endpoint(&ep);

    REQUIRE(rc.enumerate() == true);

    // 应发现 1 个设备（PF0），但支持 per-VF 访问
    const auto& devices = rc.discovered_devices();
    REQUIRE(devices.size() == 1);

    // 写 VF0 配置空间
    REQUIRE(rc.config_write(0, 0, 0x04, 0x2222, /*stream_id=*/1) == true);
    REQUIRE(rc.config_read(0, 0, 0x04, /*stream_id=*/1) == 0x2222u);

    // PF (stream_id=0) 未受影响
    REQUIRE(rc.config_read(0, 0, 0x04, /*stream_id=*/0) == 0x10u);
}

TEST_CASE("PcieRootComplexTLM: detach cleans up", "[pcie][root-complex][detach]") {
    EventQueue eq;
    PcieEndpointIP ep("pcie_ep_rc_detach", &eq);
    ep.init();

    json cfg;
    cfg["axi_adapter"] = json::object();
    ep.set_config(cfg);
    ep.on_config_loaded();

    PcieRootComplexTLM rc("root_complex_detach", &eq);
    rc.init();
    rc.attach_to_endpoint(&ep);
    REQUIRE(rc.is_attached() == true);

    rc.detach();
    REQUIRE(rc.is_attached() == false);
    REQUIRE(rc.endpoint() == nullptr);
}