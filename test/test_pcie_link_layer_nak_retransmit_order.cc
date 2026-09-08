// test_pcie_link_layer_nak_retransmit_order.cc
// Issue #1: NAK 重传顺序必须升序弹出（per PCIe 5.0 §3.6）
//   Bug: 旧实现升序 push_front → wire 输出降序
//   Fix: 降序 push_front → wire 输出升序
// Author: CppTLM Team
// Date: 2026-09-01
// 参考: openspec/changes/2026-09-08-cpptlm-dgpu-pcie-link-layer-and-fc
//       Phase 1 Oracle Critical Issue #1

#include "bundles/pcie_bundles_tlm.hh"
#include "bundles/pcie_dllp_bundles_tlm.hh"
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "tlm/pcie/pcie_link_layer_tlm.hh"

using namespace tlm::pcie;
using namespace bundles;

// 通过 offset 字段携带 seq 号,验证弹出顺序
static PcieTlpBundle make_tlp_with_seq(uint16_t seq) {
    PcieTlpBundle t;
    t.kind.write(PcieTlpBundle::MMIO_WRITE);
    t.offset.write(seq); // seq 编码到 offset
    t.size.write(4);
    t.trans_id.write(1);
    return t;
}

TEST_CASE("NakOrder: NAK retransmit emits TLPs in ascending seq (wire order)",
          "[pcie][ll][nak-order]") {
    EventQueue eq;
    PcieLinkLayer ll(&eq);

    REQUIRE(ll.tx_tlp(make_tlp_with_seq(0)) == true); // seq 0
    REQUIRE(ll.tx_tlp(make_tlp_with_seq(1)) == true); // seq 1
    REQUIRE(ll.tx_tlp(make_tlp_with_seq(2)) == true); // seq 2
    REQUIRE(ll.tx_tlp(make_tlp_with_seq(3)) == true); // seq 3
    REQUIRE(ll.tx_tlp(make_tlp_with_seq(4)) == true); // seq 4
    REQUIRE(ll.retry_buffer_size() == 5u);

    // Drain wire queue so NAK retransmit populates a clean wire queue.
    PcieTlpBundle drain;
    while (ll.try_pop_tx_tlp(drain)) { /* drain */
    }
    REQUIRE(ll.tx_tlp_out_count() == 0u);

    // NAK(2) → 重发 seq 2,3,4 (升序弹出)
    ll.rx_dllp(ll.make_nak(2));
    REQUIRE(ll.tx_tlp_out_count() == 3u);

    // 第一次 try_pop_tx_tlp 必须返回 seq 2（最小），不是 seq 4
    // 当前 bug 行为:升序 push_front → 输出降序 → 第一次弹 seq 4
    PcieTlpBundle out0, out1, out2;
    REQUIRE(ll.try_pop_tx_tlp(out0) == true);
    REQUIRE(out0.offset.read() == 2u); // 必须先看到 seq=2

    REQUIRE(ll.try_pop_tx_tlp(out1) == true);
    REQUIRE(out1.offset.read() == 3u); // 然后 seq=3

    REQUIRE(ll.try_pop_tx_tlp(out2) == true);
    REQUIRE(out2.offset.read() == 4u); // 最后 seq=4

    REQUIRE(ll.try_pop_tx_tlp(out0) == false);
}

TEST_CASE("NakOrder: NAK(0) retransmits all in ascending seq", "[pcie][ll][nak-order]") {
    EventQueue eq;
    PcieLinkLayer ll(&eq);

    for (uint16_t seq = 0; seq < 5; ++seq) {
        REQUIRE(ll.tx_tlp(make_tlp_with_seq(seq)) == true);
    }
    PcieTlpBundle drain;
    while (ll.try_pop_tx_tlp(drain)) { /* drain */
    }

    ll.rx_dllp(ll.make_nak(0));
    REQUIRE(ll.tx_tlp_out_count() == 5u);

    PcieTlpBundle out;
    for (uint16_t expected_seq = 0; expected_seq < 5; ++expected_seq) {
        REQUIRE(ll.try_pop_tx_tlp(out) == true);
        REQUIRE(out.offset.read() == expected_seq);
    }
    REQUIRE(ll.try_pop_tx_tlp(out) == false);
}
