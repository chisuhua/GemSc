// test_pcie_sriov_vf_pool_routing.cc
// SR-IOV VF Pool 路由表单元测试 (T-P4-4)
// 功能：PcieSriovVfPool 将 17 端口 stream_id (0..16) 分发到对应 PF/VF 内部状态，
//       stream_id=0→PF, 1..16→VF0..VF15；错误 stream_id 拒绝。
// 作者 CppTLM Team / 日期 2026-10-13
// 参考: openspec/changes/2026-10-13-cpptlm-dgpu-pcie-sriov-vf-pool/proposal.md T-P4-4
//       design.md §8 (VF Pool 路由表, stream_id 路由)
#include "bundles/pcie_bundles_tlm.hh"
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "tlm/pcie/pcie_sriov_vf_pool_tlm.hh"

using namespace tlm::pcie;

TEST_CASE("PcieSriovVfPool: 17 stream_ids valid, out-of-range rejected",
          "[pcie][sriov][vf-pool][routing]") {
    PcieSriovVfPool pool;
    REQUIRE(pool.num_ports() == 17u);
    for (uint16_t sid = 0; sid < 17; ++sid) {
        REQUIRE(pool.is_valid_stream_id(sid) == true);
    }
    REQUIRE(pool.is_valid_stream_id(17) == false);
    REQUIRE(pool.is_valid_stream_id(0xFFFF) == false);
}

TEST_CASE("PcieSriovVfPool: dispatch_tlp routes to per-VF config space",
          "[pcie][sriov][vf-pool][routing]") {
    PcieSriovVfPool pool;
    pool.init_all();

    EventQueue eq;

    // stream_id=0 → PF config write (Command register 0x04)
    bundles::PcieTlpBundle tlp_pf(bundles::PcieTlpBundle::CFG_WRITE, 0, 0x04, 4, 0x00000006, 0x0000,
                                  1);
    REQUIRE(pool.dispatch_tlp(0, tlp_pf) == true);
    REQUIRE(pool.config_of(0).read(0x04) == 0x00000006u);

    // stream_id=1 → VF0 config write
    bundles::PcieTlpBundle tlp_vf0(bundles::PcieTlpBundle::CFG_WRITE, 0, 0x04, 4, 0x00000002,
                                   0x0000, 2);
    REQUIRE(pool.dispatch_tlp(1, tlp_vf0) == true);
    REQUIRE(pool.config_of(1).read(0x04) == 0x00000002u);
    // PF 未被影响
    REQUIRE(pool.config_of(0).read(0x04) == 0x00000006u);
}

TEST_CASE("PcieSriovVfPool: dispatch_tlp rejects out-of-range stream_id",
          "[pcie][sriov][vf-pool][routing]") {
    PcieSriovVfPool pool;
    pool.init_all();

    bundles::PcieTlpBundle tlp(bundles::PcieTlpBundle::CFG_READ, 0, 0x04, 4, 0, 0x0000, 3);
    REQUIRE(pool.dispatch_tlp(17, tlp) == false);
    REQUIRE(pool.dispatch_tlp(0xFFFF, tlp) == false);
}

TEST_CASE("PcieSriovVfPool: dispatch_msix routes to per-VF MSI-X",
          "[pcie][sriov][vf-pool][routing]") {
    PcieSriovVfPool pool;
    pool.init_all();

    // PF vector 2 pending
    REQUIRE(pool.dispatch_msix(0, 2) == true);
    REQUIRE(pool.msix_pending(0, 2) == true);
    REQUIRE(pool.msix_pending(1, 2) == false);

    // VF0 vector 3 pending
    REQUIRE(pool.dispatch_msix(1, 3) == true);
    REQUIRE(pool.msix_pending(1, 3) == true);
    REQUIRE(pool.msix_pending(0, 3) == false);
}

TEST_CASE("PcieSriovVfPool: dispatch_msix rejects out-of-range stream_id",
          "[pcie][sriov][vf-pool][routing]") {
    PcieSriovVfPool pool;
    REQUIRE(pool.dispatch_msix(17, 0) == false);
    REQUIRE(pool.dispatch_msix(0xFFFF, 0) == false);
}

TEST_CASE("PcieSriovVfPool: stream_id 16 maps to VF15 (last VF)",
          "[pcie][sriov][vf-pool][routing]") {
    PcieSriovVfPool pool;
    pool.init_all();

    // VF15 config write via stream_id=16
    bundles::PcieTlpBundle tlp(bundles::PcieTlpBundle::CFG_WRITE, 0, 0x04, 4, 0x00000005, 0x0000,
                               9);
    REQUIRE(pool.dispatch_tlp(16, tlp) == true);
    REQUIRE(pool.config_of(16).read(0x04) == 0x00000005u);
    // PF 未被影响: 仍是 init() 写入的 (1u<<4)=0x10 (Capabilities List bit)
    REQUIRE(pool.config_of(0).read(0x04) == 0x00000010u);
}