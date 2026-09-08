// test_pcie_sriov_per_vf_config.cc
// SR-IOV per-VF Config Space 单元测试 (T-P4-1)
// 功能：验证 PcieConfigSpacePerVf 持有 17 份独立 PcieConfigSpace (PF + 16 VF)，
//       VF0 的读写不影响 VF1/PF0，BAR 配置独立。
// 作者 CppTLM Team / 日期 2026-10-13
// 参考: openspec/changes/2026-10-13-cpptlm-dgpu-pcie-sriov-vf-pool/proposal.md T-P4-1
//       design.md §8 (per-VF Config Space 4KB 独立基址)
#include "catch_amalgamated.hpp"
#include "tlm/pcie/pcie_config_space_per_vf_tlm.hh"

using namespace tlm::pcie;

TEST_CASE("PcieConfigSpacePerVf: 17 slots (PF + 16 VF)", "[pcie][sriov][per-vf-config]") {
    PcieConfigSpacePerVf pool;
    REQUIRE(pool.num_slots() == 17u);
    REQUIRE(pool.is_valid_vf_id(0) == true);   // PF
    REQUIRE(pool.is_valid_vf_id(1) == true);   // VF0
    REQUIRE(pool.is_valid_vf_id(16) == true);  // VF15
    REQUIRE(pool.is_valid_vf_id(17) == false); // 越界
}

TEST_CASE("PcieConfigSpacePerVf: per-slot objects are distinct", "[pcie][sriov][per-vf-config]") {
    PcieConfigSpacePerVf pool;
    // 每个 slot 都是独立对象
    REQUIRE(&pool.config_of(0) != &pool.config_of(1));
    REQUIRE(&pool.config_of(1) != &pool.config_of(2));
    REQUIRE(&pool.config_of(0) != &pool.config_of(16));
}

TEST_CASE("PcieConfigSpacePerVf: VF0 write does not affect PF0/VF1",
          "[pcie][sriov][per-vf-config]") {
    PcieConfigSpacePerVf pool;

    // 初始化所有 slot（填充 vendor/device/header 默认值）
    for (uint16_t i = 0; i < 17; ++i) {
        pool.config_of(i).init();
    }

    // 记录写入前的 PF0/VF1 值
    const uint32_t pf0_before = pool.config_of(0).read(0x04); // Command/Status
    const uint32_t vf1_before = pool.config_of(1).read(0x04);

    // VF0 (slot 1) 写 Command register (offset 0x04)
    pool.config_of(1).write(0x04, 0x00000006); // Bus Master Enable + Memory Space

    // VF0 自身改变
    REQUIRE(pool.config_of(1).read(0x04) == 0x00000006u);
    // PF0 不变
    REQUIRE(pool.config_of(0).read(0x04) == pf0_before);
    // VF1 不变
    REQUIRE(pool.config_of(1 + 1).read(0x04) == vf1_before);
}

TEST_CASE("PcieConfigSpacePerVf: independent BAR sizes per VF", "[pcie][sriov][per-vf-config]") {
    PcieConfigSpacePerVf pool;
    for (uint16_t i = 0; i < 17; ++i) {
        pool.config_of(i).init();
    }

    // PF0 (slot 0) BAR0 = 1MB, VF0 (slot 1) BAR0 = 256MB
    pool.config_of(0).write(0x10, 0xFFF00000); // BAR0 大小掩码写入
    pool.config_of(1).write(0x10, 0x00000000); // VF0 BAR0 不同值

    // 独立读写
    REQUIRE(pool.config_of(0).read(0x10) == 0xFFF00000u);
    REQUIRE(pool.config_of(1).read(0x10) == 0x00000000u);
}

TEST_CASE("PcieConfigSpacePerVf: out-of-range vf_id throws / rejected",
          "[pcie][sriov][per-vf-config]") {
    PcieConfigSpacePerVf pool;
    REQUIRE_THROWS_AS(pool.config_of(17), std::out_of_range);
    REQUIRE_THROWS_AS(pool.config_of(255), std::out_of_range);
}
