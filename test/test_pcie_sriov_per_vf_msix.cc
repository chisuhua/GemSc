// test_pcie_sriov_per_vf_msix.cc
// SR-IOV per-VF MSI-X table 单元测试 (T-P4-2)
// 功能：验证 PcieMsixTablePerVf 持有 17 份独立 MsiXTable，
//       PF MSI-X 与 VF0 MSI-X 互不影响（vector 数独立、pending bit 独立）。
// 作者 CppTLM Team / 日期 2026-10-13
// 参考: openspec/changes/2026-10-13-cpptlm-dgpu-pcie-sriov-vf-pool/proposal.md T-P4-2
//       design.md §8 (per-VF MSI-X Table 独立 vector 表)
#include "catch_amalgamated.hpp"
#include "tlm/pcie/pcie_msix_per_vf_tlm.hh"

using namespace tlm::pcie;

TEST_CASE("PcieMsixTablePerVf: 17 slots (PF + 16 VF)", "[pcie][sriov][per-vf-msix]") {
    PcieMsixTablePerVf pool;
    REQUIRE(pool.num_slots() == 17u);
    REQUIRE(pool.is_valid_vf_id(0) == true);
    REQUIRE(pool.is_valid_vf_id(16) == true);
    REQUIRE(pool.is_valid_vf_id(17) == false);
}

TEST_CASE("PcieMsixTablePerVf: per-slot tables are distinct", "[pcie][sriov][per-vf-msix]") {
    PcieMsixTablePerVf pool;
    REQUIRE(&pool.table_of(0) != &pool.table_of(1));
    REQUIRE(&pool.table_of(1) != &pool.table_of(2));
}

TEST_CASE("PcieMsixTablePerVf: independent vector counts per VF", "[pcie][sriov][per-vf-msix]") {
    PcieMsixTablePerVf pool;
    // PF (slot 0) 32 vectors, VF0 (slot 1) 4 vectors
    pool.configure_vectors(0, 32);
    pool.configure_vectors(1, 4);

    REQUIRE(pool.table_of(0).num_vectors() == 32u);
    REQUIRE(pool.table_of(1).num_vectors() == 4u);

    // VF0 vector 4 越界（只有 0..3），PF 相同 vector 合法
    REQUIRE(pool.update_pending(1, 5) == false); // VF0 越界
    REQUIRE(pool.update_pending(0, 5) == true);  // PF 合法
}

TEST_CASE("PcieMsixTablePerVf: PF pending does not affect VF0", "[pcie][sriov][per-vf-msix]") {
    PcieMsixTablePerVf pool;
    pool.configure_vectors(0, 16);
    pool.configure_vectors(1, 16);

    // PF vector 2 pending
    REQUIRE(pool.update_pending(0, 2) == true);
    REQUIRE(pool.pending(0, 2) == true);
    REQUIRE(pool.pending(1, 2) == false); // VF0 不受影响

    // 反向：VF0 vector 3 pending，PF 不受影响
    REQUIRE(pool.update_pending(1, 3) == true);
    REQUIRE(pool.pending(1, 3) == true);
    REQUIRE(pool.pending(0, 3) == false);
}

TEST_CASE("PcieMsixTablePerVf: clear_pending per-VF only", "[pcie][sriov][per-vf-msix]") {
    PcieMsixTablePerVf pool;
    pool.configure_vectors(0, 16);
    pool.configure_vectors(1, 16);

    pool.update_pending(0, 2);
    pool.update_pending(1, 2);

    // 清除 VF0 vector 2 → PF 仍 pending
    REQUIRE(pool.clear_pending(1, 2) == true);
    REQUIRE(pool.pending(1, 2) == false);
    REQUIRE(pool.pending(0, 2) == true);
}

TEST_CASE("PcieMsixTablePerVf: out-of-range vf_id throws", "[pcie][sriov][per-vf-msix]") {
    PcieMsixTablePerVf pool;
    REQUIRE_THROWS_AS(pool.table_of(17), std::out_of_range);
}