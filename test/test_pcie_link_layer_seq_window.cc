// test_pcie_link_layer_seq_window.cc
// Issue #4: Seq 窗口卫生 — outstanding 上限 + stale ACK guard
//   Bug A: retry_buf_ 无上限；`emplace` 在满时静默 no-op（TLP 已发但不可恢复）
//   Bug B: on_ack_received 对 delta>=2048 的巨大前向 ACK 当成有效 → 抹空 retry buffer
//   Fix:  tx_tlp 在 outstanding>=2048 反压返回 false；stale ACK (delta>2048) 忽略
// Author: CppTLM Team
// Date: 2026-09-01
// 参考: openspec/changes/2026-09-08-cpptlm-dgpu-pcie-link-layer-and-fc
//       Phase 1 Oracle Critical Issue #4

#include "bundles/pcie_bundles_tlm.hh"
#include "bundles/pcie_dllp_bundles_tlm.hh"
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "tlm/pcie/pcie_link_layer_tlm.hh"

using namespace tlm::pcie;
using namespace bundles;

TEST_CASE("SeqWindow: tx backpressures at 2048 outstanding (no silent no-op)",
          "[pcie][ll][seq-window]") {
    EventQueue eq;
    PcieLinkLayerConfig cfg;
    cfg.fc_capacity = 4096; // FC 不成为瓶颈（只测 seq 窗口）
    cfg.fc_init_p = 4096;
    cfg.fc_init_np = 4096;
    cfg.fc_init_cpl = 4096;
    PcieLinkLayer ll(&eq, cfg);

    PcieTlpBundle t(PcieTlpBundle::MMIO_WRITE, 0, 0x1000, 4, 1, 0x0100, 1);

    // 2048 个 TLP：全部成功（PCIe half-window 最大 outstanding = 2048）
    for (int i = 0; i < 2048; ++i) {
        REQUIRE(ll.tx_tlp(t) == true);
    }
    REQUIRE(ll.retry_buffer_size() == 2048u);

    // 第 2049 个 → 反压返回 false（不能超出 half-window）
    // 当前 bug：retry_buf_ 无上限 → 仍返回 true 静默 no-op 入 retry buffer
    REQUIRE(ll.tx_tlp(t) == false);
    REQUIRE(ll.retry_buffer_size() == 2048u);
}

TEST_CASE("SeqWindow: stale ACK (delta > 2048) is ignored, buffer not wiped",
          "[pcie][ll][seq-window]") {
    EventQueue eq;
    PcieLinkLayer ll(&eq);

    PcieTlpBundle t(PcieTlpBundle::MMIO_WRITE, 0, 0x1000, 4, 1, 0x0100, 1);
    for (int i = 0; i < 4; ++i) {
        REQUIRE(ll.tx_tlp(t) == true); // seq 0..3
    }
    PcieTlpBundle drain;
    while (ll.try_pop_tx_tlp(drain)) { /* drain */
    }
    ll.rx_dllp(ll.make_ack(3)); // 正常 ACK → last_acked=3
    REQUIRE(ll.retry_buffer_size() == 0u);

    for (int i = 0; i < 4; ++i) {
        REQUIRE(ll.tx_tlp(t) == true); // seq 4..7
    }
    while (ll.try_pop_tx_tlp(drain)) { /* drain */
    }
    REQUIRE(ll.retry_buffer_size() == 4u);

    // stale ACK: ack_seq=2052 → delta=2049 (> 2048, 超出 half-window) → 忽略
    // 当前 bug：被当成巨大前向 ACK → 抹空整个 retry buffer
    ll.rx_dllp(ll.make_ack(2052));
    REQUIRE(ll.retry_buffer_size() == 4u);
    REQUIRE(ll.last_acked_seq() == 3u);

    // 正常 ACK(7) 仍工作（不被 stale guard 卡死）
    ll.rx_dllp(ll.make_ack(7));
    REQUIRE(ll.retry_buffer_size() == 0u);
    REQUIRE(ll.last_acked_seq() == 7u);
}

TEST_CASE("SeqWindow: delta == 2048 full-window ACK remains valid (boundary)",
          "[pcie][ll][seq-window]") {
    EventQueue eq;
    PcieLinkLayer ll(&eq);

    // last_acked 初始 = SEQ_MASK(4095)；ACK(2047) → delta=2048（合法满窗确认）
    PcieTlpBundle t(PcieTlpBundle::MMIO_WRITE, 0, 0x1000, 4, 1, 0x0100, 1);
    for (int i = 0; i < 4; ++i) {
        REQUIRE(ll.tx_tlp(t) == true); // seq 0..3
    }
    PcieTlpBundle drain;
    while (ll.try_pop_tx_tlp(drain)) { /* drain */
    }

    ll.rx_dllp(ll.make_ack(2047));
    REQUIRE(ll.retry_buffer_size() == 0u); // 必须仍被确认（非 stale）
    REQUIRE(ll.last_acked_seq() == 2047u);
}