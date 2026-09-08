// test_pcie_link_layer_per_direction_fc.cc
// Issue #3: 上下行独立 FC 桶 (per Q17 双向链路层, 违反"共享单一 fc_")
//   Bug: tx_tlp 与 rx_tlp_from_host 共用单一 fc_ → 双向 credit 耦合
//   Fix: fc_upstream_ (EP 收侧, host→EP) + fc_downstream_ (EP 发侧, EP→host)
// Author: CppTLM Team
// Date: 2026-09-01
// 参考: openspec/changes/2026-09-08-cpptlm-dgpu-pcie-link-layer-and-fc
//       Phase 1 Oracle Critical Issue #3

#include "bundles/pcie_bundles_tlm.hh"
#include "bundles/pcie_dllp_bundles_tlm.hh"
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "tlm/pcie/pcie_link_layer_tlm.hh"

using namespace tlm::pcie;
using namespace bundles;

namespace {

    // 上行 TLP (EP→host, MMIO_WRITE → Posted)
    PcieTlpBundle make_tx_tlp() {
        PcieTlpBundle t;
        t.kind.write(PcieTlpBundle::MMIO_WRITE);
        t.offset.write(0x1000);
        t.size.write(4);
        t.trans_id.write(1);
        return t;
    }

    // 下行 TLP (host→EP, MEM_WRITE → Posted)
    PcieTlpBundle make_rx_tlp() {
        PcieTlpBundle t;
        t.kind.write(PcieTlpBundle::MEM_WRITE);
        t.offset.write(0x8000);
        t.size.write(4);
        t.trans_id.write(2);
        return t;
    }

} // namespace

TEST_CASE("PerDirFC: upstream rx exhaustion does not block downstream tx",
          "[pcie][ll][per-dir-fc]") {
    EventQueue eq;
    PcieLinkLayerConfig cfg;
    cfg.fc_capacity = 2;
    cfg.fc_init_p = 2;
    cfg.fc_init_np = 2;
    cfg.fc_init_cpl = 2;
    PcieLinkLayer ll(&eq, cfg);

    // 耗尽上游 (EP 收侧, fc_upstream_)：host→EP 2 个 TLP
    PcieTlpBundle rx = make_rx_tlp();
    REQUIRE(ll.rx_tlp_from_host(rx) == true);
    REQUIRE(ll.rx_tlp_from_host(rx) == true);
    REQUIRE(ll.rx_tlp_from_host(rx) == false); // 上游反压

    // 下游 (EP 发侧, fc_downstream_) 不受影响：EP 仍可发送
    PcieTlpBundle tx = make_tx_tlp();
    REQUIRE(ll.tx_tlp(tx) == true);
    REQUIRE(ll.tx_tlp(tx) == true);
}

TEST_CASE("PerDirFC: downstream tx exhaustion does not block upstream rx",
          "[pcie][ll][per-dir-fc]") {
    EventQueue eq;
    PcieLinkLayerConfig cfg;
    cfg.fc_capacity = 2;
    cfg.fc_init_p = 2;
    cfg.fc_init_np = 2;
    cfg.fc_init_cpl = 2;
    PcieLinkLayer ll(&eq, cfg);

    // 耗尽下游 (EP 发侧, fc_downstream_)：EP→host 2 个 TLP
    PcieTlpBundle tx = make_tx_tlp();
    REQUIRE(ll.tx_tlp(tx) == true);
    REQUIRE(ll.tx_tlp(tx) == true);
    REQUIRE(ll.tx_tlp(tx) == false); // 下游反压

    // 上游 (EP 收侧, fc_upstream_) 不受影响：host→EP 仍可接收
    PcieTlpBundle rx = make_rx_tlp();
    REQUIRE(ll.rx_tlp_from_host(rx) == true);
    REQUIRE(ll.rx_tlp_from_host(rx) == true);
}

TEST_CASE("PerDirFC: host UpdateFC replenishes upstream (EP rx side) only",
          "[pcie][ll][per-dir-fc]") {
    EventQueue eq;
    PcieLinkLayerConfig cfg;
    cfg.fc_capacity = 2;
    cfg.fc_init_p = 2;
    cfg.fc_init_np = 2;
    cfg.fc_init_cpl = 2;
    PcieLinkLayer ll(&eq, cfg);

    // 耗尽上游
    PcieTlpBundle rx = make_rx_tlp();
    REQUIRE(ll.rx_tlp_from_host(rx) == true);
    REQUIRE(ll.rx_tlp_from_host(rx) == true);
    REQUIRE(ll.rx_tlp_from_host(rx) == false);

    // host 发 UpdateFC(P=2) → 只补上游 (EP 收侧)
    REQUIRE(ll.rx_dllp_from_host(ll.make_update_fc(2, 0, 0)) == PcieLinkLayer::Dispatch::UPDATE_FC);
    REQUIRE(ll.rx_tlp_from_host(rx) == true);
    REQUIRE(ll.rx_tlp_from_host(rx) == true);
    REQUIRE(ll.rx_tlp_from_host(rx) == false);

    // 下游不受 UpdateFC 影响：仍能发送（下游未耗尽）
    PcieTlpBundle tx = make_tx_tlp();
    REQUIRE(ll.tx_tlp(tx) == true);
    REQUIRE(ll.tx_tlp(tx) == true);
}

TEST_CASE("PerDirFC: InitFC mirror-fills both buckets (EP sends after recovery)",
          "[pcie][ll][per-dir-fc]") {
    EventQueue eq;
    PcieLinkLayerConfig cfg;
    cfg.fc_capacity = 2;
    cfg.fc_init_p = 2;
    cfg.fc_init_np = 2;
    cfg.fc_init_cpl = 2;
    PcieLinkLayer ll(&eq, cfg);

    // 耗尽下游 (EP 发侧)
    PcieTlpBundle tx = make_tx_tlp();
    REQUIRE(ll.tx_tlp(tx) == true);
    REQUIRE(ll.tx_tlp(tx) == true);
    REQUIRE(ll.tx_tlp(tx) == false);

    // InitFC(P=3) 简化镜像 → 同时填充 fc_upstream_ + fc_downstream_
    REQUIRE(ll.rx_dllp_from_host(ll.make_init_fc1(3, 3, 3)) == PcieLinkLayer::Dispatch::INIT_FC1);

    // 下游恢复：EP 可发送（镜像填充生效）
    REQUIRE(ll.tx_tlp(tx) == true);
    REQUIRE(ll.tx_tlp(tx) == true);
    REQUIRE(ll.tx_tlp(tx) == true);

    // 上游也恢复：host→EP 可接收（镜像填充生效）
    PcieTlpBundle rx = make_rx_tlp();
    REQUIRE(ll.rx_tlp_from_host(rx) == true);
    REQUIRE(ll.rx_tlp_from_host(rx) == true);
    REQUIRE(ll.rx_tlp_from_host(rx) == true);
}

TEST_CASE("PerDirFC: update_fc delegate replenishes the DLLP-facing bucket",
          "[pcie][ll][per-dir-fc]") {
    EventQueue eq;
    PcieLinkLayerConfig cfg;
    cfg.fc_capacity = 2;
    cfg.fc_init_p = 2;
    cfg.fc_init_np = 2;
    cfg.fc_init_cpl = 2;
    PcieLinkLayer ll(&eq, cfg);

    // 耗尽上游
    PcieTlpBundle rx = make_rx_tlp();
    REQUIRE(ll.rx_tlp_from_host(rx) == true);
    REQUIRE(ll.rx_tlp_from_host(rx) == true);
    REQUIRE(ll.rx_tlp_from_host(rx) == false);

    // update_fc 公共委托补上游 (DLLP 面向桶，与 rx_dllp UpdateFC 语义一致)
    ll.update_fc(FcTokenBucket::Type::Posted, 2);
    REQUIRE(ll.rx_tlp_from_host(rx) == true);
    REQUIRE(ll.rx_tlp_from_host(rx) == true);
}