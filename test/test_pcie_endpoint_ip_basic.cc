// test/test_pcie_endpoint_ip_basic.cc
// PcieEndpointIP 基础测试 (T-P4-7)
// 功能：创建 PcieEndpointIP(17 端口), 各 VF 独立 BAR/FC/MSI-X,
//       num_ports=17, set_stream_adapter 17 适配器, on_config_loaded 挂接 LL/PHY/Mux
// 作者 CppTLM Team / 日期 2026-10-13
// 参考: openspec/changes/2026-10-13-cpptlm-dgpu-pcie-sriov-vf-pool/proposal.md T-P4-7
#include "bundles/pcie_bundles_tlm.hh"
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "framework/multi_port_stream_adapter.hh"
#include "tlm/pcie/pcie_endpoint_ip.hh"

#include <memory>
#include <nlohmann/json.hpp>

using namespace tlm::pcie;
using json = nlohmann::json;

TEST_CASE("PcieEndpointIP: create 17 ports, num_ports=17", "[pcie][sriov][endpoint-ip][basic]") {
    EventQueue eq;
    PcieEndpointIP ep("pcie_ep_ip", &eq);
    REQUIRE(ep.num_ports() == 17u);
}

TEST_CASE("PcieEndpointIP: set_stream_adapter accepts 17 adapters",
          "[pcie][sriov][endpoint-ip][basic]") {
    EventQueue eq;
    PcieEndpointIP ep("pcie_ep_ip", &eq);
    ep.init();

    using Adapter = cpptlm::MultiPortStreamAdapter<PcieEndpointIP, bundles::PcieTlpBundle,
                                                   bundles::PcieTlpBundle, 17>;
    auto adapter = std::make_unique<Adapter>(&ep);
    cpptlm::StreamAdapterBase* adapters[17] = {nullptr};
    adapters[0] = adapter.get(); // 第 0 个用作数组注入
    // set_stream_adapter(adapters) 应不崩溃
    ep.set_stream_adapter(adapters);
    // 单 adapter 注入也应工作
    ep.set_stream_adapter(adapter.get());
}

TEST_CASE("PcieEndpointIP: on_config_loaded attaches link layer + phy + mux",
          "[pcie][sriov][endpoint-ip][basic]") {
    EventQueue eq;
    PcieEndpointIP ep("pcie_ep_ip_ll", &eq);
    ep.init();

    json cfg;
    cfg["link_layer"]["enabled"] = true;
    cfg["link_layer"]["fc_token_bucket_capacity"] = 16;
    cfg["link_layer"]["fc_initial_credit_p"] = 16;
    cfg["link_layer"]["fc_initial_credit_np"] = 16;
    cfg["link_layer"]["fc_initial_credit_cpl"] = 16;
    ep.set_config(cfg); // → on_config_loaded

    // PcieLinkLayer / Phy / Mux 应按 endpoint name 挂接
    REQUIRE(PcieLinkLayer::for_endpoint("pcie_ep_ip_ll") != nullptr);
    REQUIRE(PciePhyDigitalCtrl::for_endpoint("pcie_ep_ip_ll") != nullptr);
    REQUIRE(PcieBypassMux::for_endpoint("pcie_ep_ip_ll") != nullptr);

    // num_ports 仍为 17（PcieEndpointTLM 冻结不受影响）
    REQUIRE(ep.num_ports() == 17u);
}

TEST_CASE("PcieEndpointIP: per-VF Config Space accessible via vf_pool",
          "[pcie][sriov][endpoint-ip][basic]") {
    EventQueue eq;
    PcieEndpointIP ep("pcie_ep_ip_cfg", &eq);
    ep.init();

    // VF0 (slot 1) 写 Config 0x04
    bundles::PcieTlpBundle t(bundles::PcieTlpBundle::CFG_WRITE, 0, 0x04, 4, 0xDEADu, 0x0000, 1);
    REQUIRE(ep.vf_pool().dispatch_tlp(1, t) == true);
    REQUIRE(ep.vf_pool().config_of(1).read(0x04) == 0xDEADu);
    // PF (slot 0) 未受影响
    REQUIRE(ep.vf_pool().config_of(0).read(0x04) == 0x10u); // Capabilities bit
}

TEST_CASE("PcieEndpointIP: FLR PF resets all, FLR VF resets one",
          "[pcie][sriov][endpoint-ip][basic]") {
    EventQueue eq;
    PcieEndpointIP ep("pcie_ep_ip_flr", &eq);
    ep.init();

    // VF0 + VF1 写不同值
    bundles::PcieTlpBundle t1(bundles::PcieTlpBundle::CFG_WRITE, 0, 0x04, 4, 0x1111u, 0x0000, 1);
    bundles::PcieTlpBundle t2(bundles::PcieTlpBundle::CFG_WRITE, 0, 0x04, 4, 0x2222u, 0x0000, 2);
    ep.vf_pool().dispatch_tlp(1, t1);
    ep.vf_pool().dispatch_tlp(2, t2);
    REQUIRE(ep.vf_pool().config_of(1).read(0x04) == 0x1111u);
    REQUIRE(ep.vf_pool().config_of(2).read(0x04) == 0x2222u);

    // flr_vf(1) → VF0 回 default, VF1 保持
    ep.flr_vf(1);
    REQUIRE(ep.vf_pool().config_of(1).read(0x04) == 0x10u);
    REQUIRE(ep.vf_pool().config_of(2).read(0x04) == 0x2222u);

    // flr_pf → 全部回 default
    ep.flr_pf();
    for (uint16_t sid = 0; sid < 17; ++sid) {
        REQUIRE(ep.vf_pool().config_of(sid).read(0x04) == 0x10u);
    }
}