// test_pcie_dllp_bundle.cc
// PcieDllpBundle + PciePhyConfig: 字段序列化 + 6 种 kind 判定测试
// Author: CppTLM Team
// Date: 2026-09-08
// 参考: openspec/changes/2026-09-01-cpptlm-dgpu-pcie-ip-microarch/design.md §6.2/§6.3
//       specs/link-layer-and-fc/spec.md Scenario "DLLP gen/parse"

#include "bundles/bundle_serialization.hh"
#include "bundles/cpphdl_types.hh"
#include "bundles/pcie_dllp_bundles_tlm.hh"
#include "catch_amalgamated.hpp"

#include <cstring>

using namespace bundles;

TEST_CASE("PcieDllpBundle: 6 kinds + Vendor constants defined", "[pcie][dllp][bundle]") {
    // per design.md §6.3 冻结: kind=8 bits, 6 种 DLLP type + Vendor-specific
    REQUIRE(PcieDllpBundle::ACK == 0);
    REQUIRE(PcieDllpBundle::NAK == 1);
    REQUIRE(PcieDllpBundle::INIT_FC1 == 2);
    REQUIRE(PcieDllpBundle::INIT_FC2 == 3);
    REQUIRE(PcieDllpBundle::UPDATE_FC == 4);
    REQUIRE(PcieDllpBundle::NOP == 5);
    REQUIRE(PcieDllpBundle::VENDOR == 6);

    // kind 是 6+1 (7 个枚举值), 8-bit 字段足够扩展
    REQUIRE(PcieDllpBundle::VENDOR <= 0xFF);
}

TEST_CASE("PcieDllpBundle: default construction is zeroed", "[pcie][dllp][bundle]") {
    PcieDllpBundle b;
    REQUIRE(b.kind.read() == 0u);
    REQUIRE(b.vc_id.read() == 0u);
    REQUIRE(b.credit_P.read() == 0u);
    REQUIRE(b.credit_NP.read() == 0u);
    REQUIRE(b.credit_Cpl.read() == 0u);
    REQUIRE(b.seq_num.read() == 0u);
    REQUIRE(b.seq_num_ack.read() == 0u);
    REQUIRE(b.trans_id.read() == 0u);
}

TEST_CASE("PcieDllpBundle: fields serialize round-trip", "[pcie][dllp][bundle]") {
    PcieDllpBundle src;
    src.kind.write(PcieDllpBundle::UPDATE_FC);
    src.vc_id.write(3);
    src.credit_P.write(0x1234);
    src.credit_NP.write(0x5678);
    src.credit_Cpl.write(0x9ABC);
    src.seq_num.write(0x0FFF); // 12-bit 最大值 (4095)
    src.seq_num_ack.write(0x00FF);
    src.trans_id.write(0xDEADBEEF);

    // 内存序列化 round-trip (per bundle_serialization.hh: memcpy-based)
    std::uint8_t buf[sizeof(PcieDllpBundle)];
    REQUIRE(serialize_bundle(src, buf, sizeof(buf)) == true);

    PcieDllpBundle dst;
    REQUIRE(deserialize_bundle(buf, sizeof(buf), dst) == true);

    REQUIRE(dst.kind.read() == PcieDllpBundle::UPDATE_FC);
    REQUIRE(dst.vc_id.read() == 3u);
    REQUIRE(dst.credit_P.read() == 0x1234u);
    REQUIRE(dst.credit_NP.read() == 0x5678u);
    REQUIRE(dst.credit_Cpl.read() == 0x9ABCu);
    REQUIRE(dst.seq_num.read() == 0x0FFFu);
    REQUIRE(dst.seq_num_ack.read() == 0x00FFu);
    REQUIRE(dst.trans_id.read() == 0xDEADBEEFu);
}

TEST_CASE("PcieDllpBundle: kind predicates", "[pcie][dllp][bundle]") {
    PcieDllpBundle ack;
    ack.kind.write(PcieDllpBundle::ACK);
    REQUIRE(ack.is_ack() == true);
    REQUIRE(ack.is_nak() == false);
    REQUIRE(ack.is_fc() == false);

    PcieDllpBundle nak;
    nak.kind.write(PcieDllpBundle::NAK);
    REQUIRE(nak.is_nak() == true);

    PcieDllpBundle fc1;
    fc1.kind.write(PcieDllpBundle::INIT_FC1);
    REQUIRE(fc1.is_fc() == true);

    PcieDllpBundle fc2;
    fc2.kind.write(PcieDllpBundle::INIT_FC2);
    REQUIRE(fc2.is_fc() == true);

    PcieDllpBundle ufc;
    ufc.kind.write(PcieDllpBundle::UPDATE_FC);
    REQUIRE(ufc.is_fc() == true);

    PcieDllpBundle nop;
    nop.kind.write(PcieDllpBundle::NOP);
    REQUIRE(nop.is_nop() == true);

    PcieDllpBundle vendor;
    vendor.kind.write(PcieDllpBundle::VENDOR);
    REQUIRE(vendor.is_vendor() == true);
}

TEST_CASE("PciePhyConfig: defaults", "[pcie][dllp][bundle][phy]") {
    PciePhyConfig cfg;
    REQUIRE(cfg.max_speed.read() == PciePhyConfig::GEN5);
    REQUIRE(cfg.max_lanes.read() == 16u);
    REQUIRE(cfg.preset_P.read() == 7u);
    REQUIRE(cfg.preset_NP.read() == 7u);
    REQUIRE(cfg.preset_Cpl.read() == 7u);
    REQUIRE(cfg.sr_iov_vf_pool_size.read() == 0u);
    REQUIRE(cfg.hot_plug_supported.read() == 0u);
}

TEST_CASE("PciePhyConfig: fields write/read", "[pcie][dllp][bundle][phy]") {
    PciePhyConfig cfg;
    cfg.max_speed.write(PciePhyConfig::GEN4);
    cfg.max_lanes.write(8);
    cfg.preset_P.write(5);
    cfg.preset_NP.write(6);
    cfg.preset_Cpl.write(4);
    cfg.sr_iov_vf_pool_size.write(16);
    cfg.hot_plug_supported.write(1);

    REQUIRE(cfg.max_speed.read() == PciePhyConfig::GEN4);
    REQUIRE(cfg.max_lanes.read() == 8u);
    REQUIRE(cfg.preset_P.read() == 5u);
    REQUIRE(cfg.preset_NP.read() == 6u);
    REQUIRE(cfg.preset_Cpl.read() == 4u);
    REQUIRE(cfg.sr_iov_vf_pool_size.read() == 16u);
    REQUIRE(cfg.hot_plug_supported.read() == 1u);
}