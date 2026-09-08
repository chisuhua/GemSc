// test_pcie_link_layer_ack_nak.cc
// PcieLinkLayer ACK/NAK 重传（累积确认 + 12-bit wrap）测试
// Author: CppTLM Team
// Date: 2026-09-08
// 参考: openspec/changes/2026-09-01-cpptlm-dgpu-pcie-ip-microarch/design.md §4
//       PCIe 5.0 Base Specification §3.6 "Retransmission"
//       specs/link-layer-and-fc/spec.md Scenario "ACK/NAK retry(累积确认)"

#include "bundles/pcie_bundles_tlm.hh"
#include "bundles/pcie_dllp_bundles_tlm.hh"
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "tlm/pcie/pcie_link_layer_tlm.hh"

using namespace tlm::pcie;
using namespace bundles;

// 发送 N 个 TLP，全部出队（假想 host 已收）
static void tx_and_drain(PcieLinkLayer& ll, int n) {
    PcieTlpBundle t(PcieTlpBundle::MMIO_WRITE, 0, 0x1000, 4, 1, 0x0100, 1);
    for (int i = 0; i < n; ++i) {
        REQUIRE(ll.tx_tlp(t) == true);
    }
    PcieTlpBundle out;
    for (int i = 0; i < n; ++i) {
        REQUIRE(ll.try_pop_tx_tlp(out) == true); // host 消费
    }
    REQUIRE(ll.try_pop_tx_tlp(out) == false);
}

TEST_CASE("AckNak: ACK cumulatively clears retry buffer up to ack seq", "[pcie][ll][ack-nak]") {
    EventQueue eq;
    PcieLinkLayer ll(&eq);

    tx_and_drain(ll, 3); // seq 0,1,2
    REQUIRE(ll.retry_buffer_size() == 3u);

    // ACK seq=1 → 累积清空 seq ≤ 1（非全清：seq2 保留）
    ll.rx_dllp(ll.make_ack(1));
    REQUIRE(ll.retry_buffer_size() == 1u);
    REQUIRE(ll.last_acked_seq() == 1u);

    // ACK seq=2 → 全部清空
    ll.rx_dllp(ll.make_ack(2));
    REQUIRE(ll.retry_buffer_size() == 0u);
    REQUIRE(ll.last_acked_seq() == 2u);
}

TEST_CASE("AckNak: NAK retransmits all TLPs with seq >= nak seq", "[pcie][ll][ack-nak]") {
    EventQueue eq;
    PcieLinkLayer ll(&eq);

    tx_and_drain(ll, 4); // seq 0,1,2,3
    REQUIRE(ll.retry_buffer_size() == 4u);

    // NAK seq=2 → 重发 seq 2,3
    ll.rx_dllp(ll.make_nak(2));
    REQUIRE(ll.tx_tlp_out_count() == 2u);

    PcieTlpBundle out;
    REQUIRE(ll.try_pop_tx_tlp(out) == true);
    REQUIRE(ll.try_pop_tx_tlp(out) == true);
    REQUIRE(ll.try_pop_tx_tlp(out) == false);
    // retry buffer 保守保留（等待后续 ACK）
    REQUIRE(ll.retry_buffer_size() == 4u);
}

TEST_CASE("AckNak: NAK seq=0 retransmits everything", "[pcie][ll][ack-nak]") {
    EventQueue eq;
    PcieLinkLayer ll(&eq);

    tx_and_drain(ll, 3);
    ll.rx_dllp(ll.make_nak(0));
    REQUIRE(ll.tx_tlp_out_count() == 3u);
}

TEST_CASE("AckNak: 12-bit seq wrap at 4095→0", "[pcie][ll][ack-nak][wrap]") {
    EventQueue eq;
    PcieLinkLayerConfig cfg;
    cfg.fc_capacity = 8192; // 足够大量发送不触发 FC 反压
    cfg.fc_init_p = 8192;
    PcieLinkLayer ll(&eq, cfg);

    PcieTlpBundle t(PcieTlpBundle::MMIO_WRITE, 0, 0x1000, 4, 1, 0x0100, 1);

    // 第一波 2048 个 TLP：seq 0...2047
    for (int i = 0; i < 2048; ++i) {
        REQUIRE(ll.tx_tlp(t) == true);
    }
    REQUIRE(ll.retry_buffer_size() == 2048u);
    PcieTlpBundle out;
    for (int i = 0; i < 2048; ++i) {
        REQUIRE(ll.try_pop_tx_tlp(out) == true);
    }
    ll.rx_dllp(ll.make_ack(2047)); // 累积确认第一波
    REQUIRE(ll.retry_buffer_size() == 0u);

    // 第二波 2048 个 TLP：seq 2048...4095（到达空间顶端）
    for (int i = 0; i < 2048; ++i) {
        REQUIRE(ll.tx_tlp(t) == true);
    }
    REQUIRE(ll.next_tx_seq() == 0u); // 即将 wrap: 4095 → 0
    for (int i = 0; i < 2048; ++i) {
        REQUIRE(ll.try_pop_tx_tlp(out) == true);
    }
    ll.rx_dllp(ll.make_ack(4095)); // 累积确认到 4095 → 清空
    REQUIRE(ll.retry_buffer_size() == 0u);
    REQUIRE(ll.last_acked_seq() == 4095u);

    // wrap 后继续发送：seq 重新从 0 分配
    REQUIRE(ll.tx_tlp(t) == true); // seq 0
    REQUIRE(ll.tx_tlp(t) == true); // seq 1
    REQUIRE(ll.tx_tlp(t) == true); // seq 2
    REQUIRE(ll.next_tx_seq() == 3u);
    REQUIRE(ll.retry_buffer_size() == 3u);

    // 跨 wrap 的 ACK(2)：从 last_acked(4095) 推进 delta=3 → 确认 seq 0,1,2
    for (int i = 0; i < 3; ++i) {
        REQUIRE(ll.try_pop_tx_tlp(out) == true);
    }
    ll.rx_dllp(ll.make_ack(2));
    REQUIRE(ll.retry_buffer_size() == 0u);
    REQUIRE(ll.last_acked_seq() == 2u);
}

TEST_CASE("AckNak: error injector TLP loss + NAK recovery end-to-end", "[pcie][ll][ack-nak][e2e]") {
    EventQueue eq;
    PcieLinkLayerConfig cfg;
    cfg.link_error_injection_enabled = true;
    PcieLinkLayer ll(&eq, cfg);

    // 发送 3 个 TLP, 注入 seq=1 丢包
    PcieTlpBundle t(PcieTlpBundle::MMIO_WRITE, 0, 0x1000, 4, 1, 0x0100, 1);
    REQUIRE(ll.tx_tlp(t) == true); // seq0
    REQUIRE(ll.tx_tlp(t) == true); // seq1
    REQUIRE(ll.tx_tlp(t) == true); // seq2
    ll.error_injector().inject_tlp_loss(1);

    // host 收侧: 收到 0, 丢 1, 收 2
    PcieTlpBundle out;
    REQUIRE(ll.try_pop_tx_tlp(out) == true); // seq0
    REQUIRE(ll.try_pop_tx_tlp(out) == true); // seq1 被丢 → 返回 seq2
    REQUIRE(ll.tlp_drop_count() == 1u);
    REQUIRE(ll.try_pop_tx_tlp(out) == false);

    // host 检出差 seq1 → 发 NAK(1) → EP 重发 seq1,seq2
    ll.rx_dllp(ll.make_nak(1));
    REQUIRE(ll.tx_tlp_out_count() == 2u);
    REQUIRE(ll.try_pop_tx_tlp(out) == true);
    REQUIRE(ll.try_pop_tx_tlp(out) == true);
    REQUIRE(ll.try_pop_tx_tlp(out) == false);

    // host 收齐 → ACK(2) → retry buffer 清空
    ll.rx_dllp(ll.make_ack(2));
    REQUIRE(ll.retry_buffer_size() == 0u);
}