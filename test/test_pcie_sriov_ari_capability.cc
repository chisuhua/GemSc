// test_pcie_sriov_ari_capability.cc
// SR-IOV ARI (Alternative Routing-ID Interpretation) 单元测试 (T-P4-3)
// 功能：AriRouter 支持 8-bit Function Number 紧凑 routing-id 解析
//       （Function 0=PF, 1..16=VF0..VF15），ARI disabled 时回退 16-bit BDF。
// 作者 CppTLM Team / 日期 2026-10-13
// 参考: openspec/changes/2026-10-13-cpptlm-dgpu-pcie-sriov-vf-pool/proposal.md T-P4-3
//       design.md §8 (PF/VF 路由表, ARI 紧凑路由)
#include "catch_amalgamated.hpp"
#include "tlm/pcie/pcie_ari_router_tlm.hh"

using namespace tlm::pcie;

TEST_CASE("AriRouter: default ARI disabled, legacy BDF parsing unchanged", "[pcie][sriov][ari]") {
    AriRouter router; // 默认 ari_enabled=false
    REQUIRE(router.ari_enabled() == false);

    // BDF 完整解析: bus=0x00, dev=0x00, fn=0 → PF (slot 0)
    const uint16_t bdf_pf = 0x0000; // bus=0 dev=0 fn=0
    REQUIRE(router.route_id_to_vf(bdf_pf) == 0);
}

TEST_CASE("AriRouter: ARI enabled maps 8-bit function number to slot", "[pcie][sriov][ari]") {
    AriRouter router;
    router.set_ari_enabled(true);
    REQUIRE(router.ari_enabled() == true);

    // ARI: 整个 16-bit routing-id 低 8 位是 Function Number (0=PF, 1..16=VF)
    REQUIRE(router.route_id_to_vf(0x0000) == 0);  // Function 0 → PF
    REQUIRE(router.route_id_to_vf(0x0001) == 1);  // Function 1 → VF0
    REQUIRE(router.route_id_to_vf(0x0002) == 2);  // Function 2 → VF1
    REQUIRE(router.route_id_to_vf(0x0010) == 16); // Function 16 → VF15
    REQUIRE(router.route_id_to_vf(0x0011) == 0);  // Function 17 越界 → fallback PF
}

TEST_CASE("AriRouter: ARI function number ignores bus/dev bits", "[pcie][sriov][ari]") {
    AriRouter router;
    router.set_ari_enabled(true);

    // ARI 启用时低 8 位为准：bus/dev 位被忽略
    REQUIRE(router.route_id_to_vf(0x1234) == 0); // fn=0x34=52 越界 → PF
}

TEST_CASE("AriRouter: toggle ARI enabled via config space bit", "[pcie][sriov][ari]") {
    AriRouter router;

    // PCI Express Capability ARI Forwarding Enable bit (bit 0 of ARI Capability Control)
    // 低 8 位 Function Number 路由
    router.set_ari_enabled(false);
    REQUIRE(router.route_id_to_vf(0x0000) == 0); // BDF: fn=0 → PF

    router.set_ari_enabled(true);
    REQUIRE(router.route_id_to_vf(0x0001) == 1); // ARI: fn=1 → VF0

    router.set_ari_enabled(false);
    REQUIRE(router.route_id_to_vf(0x0001) == 0); // BDF: fn=1 → dev=0 fn=1 无 VF → PF
}

TEST_CASE("AriRouter: invalid function number rejected when ARI enabled", "[pcie][sriov][ari]") {
    AriRouter router;
    router.set_ari_enabled(true);

    // Function 数 > 16（超 VF 池）→ 返回无效标记
    REQUIRE(router.is_valid_function(0) == true);
    REQUIRE(router.is_valid_function(1) == true);
    REQUIRE(router.is_valid_function(16) == true);
    REQUIRE(router.is_valid_function(17) == false);
}