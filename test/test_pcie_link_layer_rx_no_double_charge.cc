// test/test_pcie_link_layer_rx_no_double_charge.cc
// PcieLinkLayer: Rx Wire-Busy 修复 (修 Phase 2 评审 #2)
// Author: CppTLM Team
// Date: 2026-10-06
// 参考: openspec/changes/2026-10-06-cpptlm-dgpu-pcie-phy-digital-ctrl/proposal.md §T-P3-6
//       pcie_link_layer_tlm.cc rx_tlp_from_host (FC check first then advance busy)
//
// 覆盖:
//   - FC 不足时 rx_tlp_from_host 返回 false
//   - 重试时不应被双倍计费 (wire 延迟只消耗一次)

#include "bundles/pcie_bundles_tlm.hh"
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "tlm/pcie/pcie_link_layer_tlm.hh"

using namespace tlm::pcie;
using namespace bundles;

namespace {

    // NonPosted credit 仅 1: 第 2 个 MMIO_READ 必然 FC reject
    PcieLinkLayerConfig single_np_config() {
        PcieLinkLayerConfig c;
        c.fc_capacity = 1;
        c.fc_init_p = 1;
        c.fc_init_np = 1;
        c.fc_init_cpl = 1;
        return c;
    }

    PcieTlpBundle make_mmio_read(uint32_t tid) {
        return PcieTlpBundle(PcieTlpBundle::MMIO_READ, 0, 0x1000, 4, 0, 0x0100, tid);
    }

} // namespace

TEST_CASE("RxNoDoubleCharge: FC 不足返回 false + 不 advance wire busy",
          "[pcie][ll][rx-no-double][t-p3-6]") {
    EventQueue eq;
    PcieLinkLayer ll(&eq, single_np_config());
    ll.set_encoding_latency(PcieEncodingLatencyModel::Rate::GEN5,
                            /*active_lanes=*/1, /*block_bytes=*/128);

    // read#1 (cycle 0): NP 1→0, 通过, advance busy → 32ns
    REQUIRE(ll.rx_tlp_from_host(make_mmio_read(1)) == true);
    const uint64_t busy_after_pass = ll.rx_wire_busy_until_ns_debug();
    REQUIRE(busy_after_pass == 32u);

    // read#2 (cycle 0): FC reject (NP=0) → false, busy 保持 32 (未双倍计费)
    REQUIRE(ll.rx_tlp_from_host(make_mmio_read(2)) == false);
    REQUIRE(ll.rx_wire_busy_until_ns_debug() == busy_after_pass);

    // read#3 (cycle 0): 仍 FC reject → busy 仍不 advance
    REQUIRE(ll.rx_tlp_from_host(make_mmio_read(3)) == false);
    REQUIRE(ll.rx_wire_busy_until_ns_debug() == busy_after_pass);
}

TEST_CASE("RxNoDoubleCharge: UpdateFC 补充后重试只计费一次 (修复核心)",
          "[pcie][ll][rx-no-double][t-p3-6][regression]") {
    EventQueue eq;
    PcieLinkLayer ll(&eq, single_np_config());
    ll.set_encoding_latency(PcieEncodingLatencyModel::Rate::GEN5,
                            /*active_lanes=*/1, /*block_bytes=*/128);

    // read#1 (cycle 0): NP 1→0, busy → 32
    REQUIRE(ll.rx_tlp_from_host(make_mmio_read(1)) == true);
    REQUIRE(ll.rx_wire_busy_until_ns_debug() == 32u);

    // 2 次 FC reject (cycle 0) — 修复后不 advance
    REQUIRE(ll.rx_tlp_from_host(make_mmio_read(2)) == false);
    REQUIRE(ll.rx_tlp_from_host(make_mmio_read(3)) == false);
    REQUIRE(ll.rx_wire_busy_until_ns_debug() == 32u);

    // 推进 40ns (wire ready: 32ns 已过)
    eq.run(40);

    // UpdateFC 补充 NP credit
    ll.update_fc(FcTokenBucket::Type::NonPosted, 1);

    // read#4 (cycle 40): 通过, busy → 72 (仅 +32ns, 非 +64 双倍)
    REQUIRE(ll.rx_tlp_from_host(make_mmio_read(4)) == true);
    REQUIRE(ll.rx_wire_busy_until_ns_debug() == 72u);
}

TEST_CASE("RxNoDoubleCharge: 修复后 FC 通过 + wire ready 都满足才接受",
          "[pcie][ll][rx-no-double][t-p3-6][both-required]") {
    EventQueue eq;
    PcieLinkLayer ll(&eq, single_np_config());
    ll.set_encoding_latency(PcieEncodingLatencyModel::Rate::GEN5,
                            /*active_lanes=*/1, /*block_bytes=*/128);

    // read#1 (cycle 0): NP 1→0, busy → 32
    REQUIRE(ll.rx_tlp_from_host(make_mmio_read(1)) == true);
    // 立即再来: wire busy → false (即使 wire 检查在 FC 后, busy 仍未到)
    REQUIRE(ll.rx_tlp_from_host(make_mmio_read(2)) == false);

    // 推进 32ns 后 wire ready, 但 FC (NP=0) 仍不足 → false
    eq.run(32);
    REQUIRE(ll.rx_tlp_from_host(make_mmio_read(3)) == false); // FC reject

    // UpdateFC + 推进: 通过
    ll.update_fc(FcTokenBucket::Type::NonPosted, 1);
    REQUIRE(ll.rx_tlp_from_host(make_mmio_read(4)) == true);
}

TEST_CASE("RxNoDoubleCharge: rate_switching 期间 rx_tlp_from_host 拒绝 (修评审 #1)",
          "[pcie][ll][rx-no-double][t-p3-6][rate-switch]") {
    EventQueue eq;
    PcieLinkLayer ll(&eq);
    ll.trigger_rate_switch(PcieEncodingLatencyModel::Rate::GEN3,
                           PcieEncodingLatencyModel::Rate::GEN5);
    REQUIRE(ll.is_rate_switching() == true);
    // 切换期间 rx_tlp_from_host 拒绝 (即使 FC 充足)
    REQUIRE(ll.rx_tlp_from_host(make_mmio_read(1)) == false);
}
