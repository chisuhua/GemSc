// test/test_pcie_sriov_flr.cc
// SR-IOV FLR (Function Level Reset) 简化实现测试 (T-P4-5)
// 功能：flr_pf() 全复位 (PF+16 VF)，flr_vf(vfx) 仅复位对应 VF。
//       FLR 后该 VF Config Space 回到 default，FC token bucket 复位，seq# 归 0。
//       其他 PF/VF 不受影响。
// 作者 CppTLM Team / 日期 2026-10-13
// 参考: openspec/changes/2026-10-13-cpptlm-dgpu-pcie-sriov-vf-pool/proposal.md T-P4-5
//       decisions.md §Q10 (简化 FLR: PF 全状态 + Vx 状态)
#include "bundles/pcie_bundles_tlm.hh"
#include "catch_amalgamated.hpp"
#include "tlm/pcie/pcie_sriov_vf_pool_tlm.hh"

using namespace tlm::pcie;

TEST_CASE("PcieSriovVfPool: flr_vf resets only target VF Config Space",
          "[pcie][sriov][vf-pool][flr]") {
    PcieSriovVfPool pool;
    pool.init_all();

    // 写 VF0 (slot 1) offset 0x04 = 0xCAFE
    bundles::PcieTlpBundle t(bundles::PcieTlpBundle::CFG_WRITE, 0, 0x04, 4, 0xCAFEu, 0x0000, 1);
    REQUIRE(pool.dispatch_tlp(1, t) == true);
    REQUIRE(pool.config_of(1).read(0x04) == 0xCAFEu);

    // FLR VF0 → 该 VF Config Space 回 default (Command=0x10 Capabilities bit)
    pool.flr_vf(1);
    REQUIRE(pool.config_of(1).read(0x04) == 0x10u);

    // PF (slot 0) 与 VF1 (slot 2) 完全未动
    REQUIRE(pool.config_of(0).read(0x04) == 0x10u);
    REQUIRE(pool.config_of(2).read(0x04) == 0x10u);
    // VF0 其他字段也回 default: 0x00 vendor/device 未被改
    // (dispatch_tlp 只写 0x04, 写后 read 0x04=CAFE; FLR 后回 0x10)
}

TEST_CASE("PcieSriovVfPool: flr_pf resets all 17 slots (PF + 16 VF)",
          "[pcie][sriov][vf-pool][flr]") {
    PcieSriovVfPool pool;
    pool.init_all();

    // 全 17 slot 写不同值
    for (uint16_t sid = 0; sid < 17; ++sid) {
        bundles::PcieTlpBundle t(bundles::PcieTlpBundle::CFG_WRITE, 0, 0x04, 4, 0xDEADu, 0x0000,
                                 sid);
        REQUIRE(pool.dispatch_tlp(sid, t) == true);
    }
    REQUIRE(pool.config_of(0).read(0x04) == 0xDEADu);
    REQUIRE(pool.config_of(16).read(0x04) == 0xDEADu);

    // FLR PF → 全 17 slot 回 default
    pool.flr_pf();
    for (uint16_t sid = 0; sid < 17; ++sid) {
        REQUIRE(pool.config_of(sid).read(0x04) == 0x10u);
    }
}

TEST_CASE("PcieSriovVfPool: flr_vf clears target VF MSI-X pending only",
          "[pcie][sriov][vf-pool][flr]") {
    PcieSriovVfPool pool;

    REQUIRE(pool.dispatch_msix(1, 3) == true); // VF0 v3 pending
    REQUIRE(pool.dispatch_msix(0, 2) == true); // PF v2 pending
    REQUIRE(pool.dispatch_msix(2, 5) == true); // VF1 v5 pending
    REQUIRE(pool.msix_pending(1, 3) == true);
    REQUIRE(pool.msix_pending(0, 2) == true);
    REQUIRE(pool.msix_pending(2, 5) == true);

    pool.flr_vf(1);
    REQUIRE(pool.msix_pending(1, 3) == false); // VF0 cleared
    REQUIRE(pool.msix_pending(0, 2) == true);  // PF unchanged
    REQUIRE(pool.msix_pending(2, 5) == true);  // VF1 unchanged
}

TEST_CASE("PcieSriovVfPool: flr_pf clears all MSI-X pending across 17 slots",
          "[pcie][sriov][vf-pool][flr]") {
    PcieSriovVfPool pool;
    for (uint16_t sid = 0; sid < 17; ++sid) {
        REQUIRE(pool.dispatch_msix(sid, 0) == true);
    }
    for (uint16_t sid = 0; sid < 17; ++sid) {
        REQUIRE(pool.msix_pending(sid, 0) == true);
    }
    pool.flr_pf();
    for (uint16_t sid = 0; sid < 17; ++sid) {
        REQUIRE(pool.msix_pending(sid, 0) == false);
    }
}

TEST_CASE("PcieSriovVfPool: flr_vf resets target FC token bucket to capacity",
          "[pcie][sriov][vf-pool][flr]") {
    PcieSriovVfPool pool;
    constexpr uint32_t CAP = 256;

    // VF0 (slot 1) 消耗 5 个 NP token (capacity 256)
    for (int i = 0; i < 5; ++i) {
        REQUIRE(pool.fc_engine().bucket(1).consume(FcTokenBucket::Type::NonPosted) == true);
    }
    REQUIRE(pool.fc_engine().bucket(1).token_count(FcTokenBucket::Type::NonPosted) == CAP - 5);
    // PF 桶未动
    REQUIRE(pool.fc_engine().bucket(0).token_count(FcTokenBucket::Type::NonPosted) == CAP);

    pool.flr_vf(1);
    // VF0 桶回 capacity
    REQUIRE(pool.fc_engine().bucket(1).token_count(FcTokenBucket::Type::NonPosted) == CAP);
    // PF 桶仍然未动
    REQUIRE(pool.fc_engine().bucket(0).token_count(FcTokenBucket::Type::NonPosted) == CAP);
}

TEST_CASE("PcieSriovVfPool: flr_pf resets all 17 FC token buckets", "[pcie][sriov][vf-pool][flr]") {
    PcieSriovVfPool pool;
    constexpr uint32_t CAP = 256;

    // 全 17 slot 桶消耗 1 个 Posted token
    for (uint16_t sid = 0; sid < 17; ++sid) {
        REQUIRE(pool.fc_engine().bucket(sid).consume(FcTokenBucket::Type::Posted) == true);
    }
    pool.flr_pf();
    for (uint16_t sid = 0; sid < 17; ++sid) {
        REQUIRE(pool.fc_engine().bucket(sid).token_count(FcTokenBucket::Type::Posted) == CAP);
    }
}

TEST_CASE("PcieSriovVfPool: flr resets per-VF seq counter", "[pcie][sriov][vf-pool][flr]") {
    PcieSriovVfPool pool;
    REQUIRE(pool.next_seq(1) == 0u); // 第一次: 返回 0
    REQUIRE(pool.next_seq(1) == 1u);
    REQUIRE(pool.next_seq(1) == 2u);
    REQUIRE(pool.next_seq(0) == 0u); // PF 独立
    REQUIRE(pool.next_seq(0) == 1u);

    pool.flr_vf(1);
    REQUIRE(pool.seq_of(1) == 0u);
    REQUIRE(pool.seq_of(0) == 2u); // PF 未受影响 (next_seq(0) 被调 2 次)

    REQUIRE(pool.next_seq(1) == 0u); // VF0 重新从 0 开始

    pool.flr_pf();
    REQUIRE(pool.seq_of(0) == 0u);
    REQUIRE(pool.seq_of(1) == 0u);
}

TEST_CASE("PcieSriovVfPool: flr_vf rejects invalid vf_id", "[pcie][sriov][vf-pool][flr]") {
    PcieSriovVfPool pool;
    pool.flr_vf(17); // 越界, 应为 no-op
    pool.flr_vf(0xFFFF);
    // pool 状态不变
    REQUIRE(pool.config_of(0).read(0x04) == 0x10u);
    REQUIRE(pool.seq_of(0) == 0u);
}

TEST_CASE("PcieSriovVfPool: flr_vf(0) rejects PF slot (C1 Oracle fix)",
          "[pcie][sriov][vf-pool][flr]") {
    PcieSriovVfPool pool;
    pool.init_all();

    // PF (slot 0) 写入非默认值 + 消耗 seq# + MSI-X pending
    bundles::PcieTlpBundle t(bundles::PcieTlpBundle::CFG_WRITE, 0, 0x04, 4, 0xBEEFu, 0x0000, 0);
    REQUIRE(pool.dispatch_tlp(0, t) == true);
    REQUIRE(pool.next_seq(0) == 0u);
    REQUIRE(pool.next_seq(0) == 1u);
    REQUIRE(pool.dispatch_msix(0, 2) == true);
    REQUIRE(pool.config_of(0).read(0x04) == 0xBEEFu);
    REQUIRE(pool.seq_of(0) == 2u);
    REQUIRE(pool.msix_pending(0, 2) == true);

    // flr_vf(0): vf_id=0 是 PF, 必须拒绝 — PF 全部状态保持不变
    pool.flr_vf(0);

    REQUIRE(pool.config_of(0).read(0x04) == 0xBEEFu); // Config 未复位
    REQUIRE(pool.seq_of(0) == 2u);                    // seq# 未复位
    REQUIRE(pool.msix_pending(0, 2) == true);         // MSI-X pending 未清
}

TEST_CASE("PcieSriovVfPool: flr_pf resets ARI forwarding enable (C2 Oracle fix)",
          "[pcie][sriov][vf-pool][flr]") {
    PcieSriovVfPool pool;
    pool.init_all();

    // 先开启 ARI Forwarding Enable（PCI Express Capability Control bit 0）
    pool.ari_router().set_ari_enabled(true);
    REQUIRE(pool.ari_router().ari_enabled() == true);

    // FLR PF → ARI 状态回默认 (disabled)
    pool.flr_pf();

    REQUIRE(pool.ari_router().ari_enabled() == false);
}
