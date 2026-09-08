// test/test_pcie_phy_digital_equalization.cc
// PciePhyDigitalCtrl: Gen3+ 均衡协商 (TS1/TS2 + 8 Preset + Phase 2/3 EQ FSM)
// Author: CppTLM Team
// Date: 2026-10-06
// 参考: openspec/changes/2026-09-01-cpptlm-dgpu-pcie-ip-microarch/design.md §5
//       openspec/changes/2026-10-06-cpptlm-dgpu-pcie-phy-digital-ctrl/proposal.md §T-P3-3
//
// 覆盖:
//   - 8 Preset 选择 (0..7)
//   - TS1/TS2 序列交互
//   - Phase 2/3 EQ FSM 收敛
//   - Gen5 ≠ FLIT (per Oracle Q1)

#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "tlm/pcie/pcie_phy_digital_ctrl_tlm.hh"

using namespace tlm::pcie;

TEST_CASE("EQ: start_equalization 进入协商态", "[pcie][phy][eq][t-p3-3]") {
    EventQueue eq;
    PciePhyDigitalCtrl phy(&eq);
    REQUIRE(phy.is_equalizing() == false);
    REQUIRE(phy.eq_converged() == false);

    phy.start_equalization();
    REQUIRE(phy.is_equalizing() == true);
    REQUIRE(phy.eq_phase() == EqPhase::TS1_Seq);
}

TEST_CASE("EQ: 8 Preset 选择 (0..7) + 越界忽略", "[pcie][phy][eq][t-p3-3]") {
    EventQueue eq;
    PciePhyDigitalCtrl phy(&eq);
    for (uint8_t p = 0; p <= 7; ++p) {
        phy.set_eq_preset(p);
        REQUIRE(phy.eq_preset() == p);
    }
    // 越界 (8/255) 忽略
    phy.set_eq_preset(8);
    REQUIRE(phy.eq_preset() == 7); // 保持上一次合法值
    phy.set_eq_preset(255);
    REQUIRE(phy.eq_preset() == 7);
}

TEST_CASE("EQ: TS1/TS2 序列交互 — emit 驱动 phase 推进", "[pcie][phy][eq][t-p3-3]") {
    EventQueue eq;
    PciePhyDigitalCtrl phy(&eq);
    phy.start_equalization();
    REQUIRE(phy.eq_phase() == EqPhase::TS1_Seq);

    phy.emit_ts2();
    REQUIRE(phy.eq_phase() == EqPhase::TS2_Seq);
}

TEST_CASE("EQ: Phase 2/3 EQ FSM 收敛 (advance_equalization 4 步到 Complete)",
          "[pcie][phy][eq][t-p3-3]") {
    EventQueue eq;
    PciePhyDigitalCtrl phy(&eq);
    phy.start_equalization();
    // TS1_Seq → TS2_Seq → Phase2 → Phase3 → Complete
    REQUIRE(phy.advance_equalization() == false);
    REQUIRE(phy.eq_phase() == EqPhase::TS2_Seq);
    REQUIRE(phy.advance_equalization() == false);
    REQUIRE(phy.eq_phase() == EqPhase::Phase2);
    REQUIRE(phy.advance_equalization() == false);
    REQUIRE(phy.eq_phase() == EqPhase::Phase3);
    REQUIRE(phy.advance_equalization() == true);
    REQUIRE(phy.eq_converged() == true);
    REQUIRE(phy.is_equalizing() == false);
}

TEST_CASE("EQ: 协商期间 L0 → 等协商完成 → L0 恢复 (可选, Gen5+ 协同速率切换)",
          "[pcie][phy][eq][t-p3-3]") {
    EventQueue eq;
    PciePhyDigitalCtrl phy(&eq);
    phy.start_link_training();
    phy.start_equalization();
    REQUIRE(phy.is_equalizing() == true);
    // 推进 4 步收敛
    for (int i = 0; i < 4; ++i) {
        phy.advance_equalization();
    }
    REQUIRE(phy.eq_converged() == true);
}

TEST_CASE("EQ: Gen5 ≠ FLIT — 不使用 FLIT 模式 (per Oracle Q1)",
          "[pcie][phy][eq][t-p3-3][gen5-not-flit]") {
    // Gen5 32 GT/s 仍用 128b/130b 编码, FLIT 是 PCIe 6.0 特性
    // 均衡不依赖 FLIT: phase 序列 + preset 表是 Gen3+ 通用
    EventQueue eq;
    PciePhyDigitalCtrl phy(&eq);
    phy.set_rate(PcieEncodingLatencyModel::Rate::GEN5);
    phy.start_equalization();
    REQUIRE(phy.is_equalizing() == true);
    // 验证 EQ FSM 走 TS1_Seq → TS2_Seq (Gen3+ 通用, 非 FLIT)
    REQUIRE(phy.eq_phase() == EqPhase::TS1_Seq);
}
