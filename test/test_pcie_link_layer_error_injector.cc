// test_pcie_link_layer_error_injector.cc
// PcieLinkLayer link_error_injector_t: Q15 错误注入接口测试
// Author: CppTLM Team
// Date: 2026-09-08
// 参考: openspec/changes/2026-09-01-cpptlm-dgpu-pcie-ip-microarch/decisions.md §Q15
//       specs/link-layer-and-fc/spec.md Scenario "错误注入接口(Q15)"

#include "bundles/pcie_bundles_tlm.hh"
#include "bundles/pcie_dllp_bundles_tlm.hh"
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "tlm/pcie/pcie_link_layer_tlm.hh"

using namespace tlm::pcie;
using namespace bundles;

TEST_CASE("ErrorInjector: default disabled has no side effects", "[pcie][ll][error-inject]") {
    EventQueue eq;
    PcieLinkLayer ll(&eq); // 默认 cfg.link_error_injection_enabled=false

    REQUIRE(ll.error_injector().enabled == false);

    // 发送 TLP + DLLP，无注入时正常出队
    PcieTlpBundle tlp(PcieTlpBundle::MMIO_WRITE, 0, 0x1000, 4, 1, 0x0100, 1);
    REQUIRE(ll.tx_tlp(tlp) == true);
    REQUIRE(ll.tx_tlp(tlp) == true);
    REQUIRE(ll.tx_tlp_out_count() == 2u);

    PcieTlpBundle out;
    REQUIRE(ll.try_pop_tx_tlp(out) == true);
    REQUIRE(ll.try_pop_tx_tlp(out) == true);
    REQUIRE(ll.try_pop_tx_tlp(out) == false);
    REQUIRE(ll.tlp_drop_count() == 0u);

    // DLLP 无损
    ll.tx_dllp(ll.make_ack(1));
    PcieDllpBundle dout;
    REQUIRE(ll.try_pop_tx_dllp(dout) == true);
    REQUIRE(ll.try_pop_tx_dllp(dout) == false);
    REQUIRE(ll.dllp_drop_count() == 0u);
}

TEST_CASE("ErrorInjector: inject_tlp_loss drops TLP with matching seq on wire",
          "[pcie][ll][error-inject]") {
    EventQueue eq;
    PcieLinkLayerConfig cfg;
    cfg.link_error_injection_enabled = true;
    PcieLinkLayer ll(&eq, cfg);
    REQUIRE(ll.error_injector().enabled == true);

    // 3 个 TLP → seq 0,1,2
    PcieTlpBundle t(PcieTlpBundle::MMIO_WRITE, 0, 0x1000, 4, 1, 0x0100, 1);
    REQUIRE(ll.tx_tlp(t) == true);
    REQUIRE(ll.tx_tlp(t) == true);
    REQUIRE(ll.tx_tlp(t) == true);
    REQUIRE(ll.tx_tlp_out_count() == 3u);

    // 注入 seq=1 丢包
    ll.error_injector().inject_tlp_loss(1);

    PcieTlpBundle out;
    REQUIRE(ll.try_pop_tx_tlp(out) == true); // seq0 正常
    REQUIRE(ll.try_pop_tx_tlp(out) == true); // seq1 被丢弃 → 弹出 seq2
    REQUIRE(ll.tlp_drop_count() == 1u);
    REQUIRE(ll.retry_buffer_size() == 3u);    // retry buffer 仍保留 seq1
    REQUIRE(ll.try_pop_tx_tlp(out) == false); // 队列已空
}

TEST_CASE("ErrorInjector: inject_dllp_loss drops next DLLP", "[pcie][ll][error-inject]") {
    EventQueue eq;
    PcieLinkLayerConfig cfg;
    cfg.link_error_injection_enabled = true;
    PcieLinkLayer ll(&eq, cfg);

    ll.tx_dllp(ll.make_ack(5));
    ll.tx_dllp(ll.make_nop());

    ll.error_injector().inject_dllp_loss();

    PcieDllpBundle dout;
    REQUIRE(ll.try_pop_tx_dllp(dout) == true); // ACK 被丢 → 返回下一个 NOP
    REQUIRE(ll.dllp_drop_count() == 1u);
    REQUIRE(dout.is_nop() == true);
    REQUIRE(ll.try_pop_tx_dllp(dout) == false); // 队列已空
}

TEST_CASE("ErrorInjector: inject_nak triggers retransmit at tick (sim receive NAK)",
          "[pcie][ll][error-inject]") {
    EventQueue eq;
    PcieLinkLayerConfig cfg;
    cfg.link_error_injection_enabled = true;
    PcieLinkLayer ll(&eq, cfg);

    PcieTlpBundle t(PcieTlpBundle::MMIO_WRITE, 0, 0x1000, 4, 1, 0x0100, 1);
    REQUIRE(ll.tx_tlp(t) == true); // seq0
    REQUIRE(ll.tx_tlp(t) == true); // seq1
    REQUIRE(ll.tx_tlp(t) == true); // seq2
    REQUIRE(ll.tx_tlp_out_count() == 3u);

    // 全部出队（假想 host 已收到）
    PcieTlpBundle out;
    for (int i = 0; i < 3; ++i) {
        REQUIRE(ll.try_pop_tx_tlp(out) == true);
    }
    REQUIRE(ll.tx_tlp_out_count() == 0u);

    // host 报告缺 seq1 → 注入 NAK(1)
    ll.error_injector().inject_nak(1);
    ll.tick(); // 消费注入 → 等效于收到 NAK DLLP → 重发 seq ≥ 1

    REQUIRE(ll.tx_tlp_out_count() == 2u); // seq1, seq2 重发
    REQUIRE(ll.try_pop_tx_tlp(out) == true);
    REQUIRE(ll.try_pop_tx_tlp(out) == true);
    REQUIRE(ll.try_pop_tx_tlp(out) == false);
}

TEST_CASE("ErrorInjector: NAK injection no-op when disabled", "[pcie][ll][error-inject]") {
    EventQueue eq;
    PcieLinkLayer ll(&eq); // disabled

    PcieTlpBundle t(PcieTlpBundle::MMIO_WRITE, 0, 0x1000, 4, 1, 0x0100, 1);
    REQUIRE(ll.tx_tlp(t) == true);
    REQUIRE(ll.tx_tlp(t) == true);

    ll.error_injector().inject_nak(0);
    ll.tick();
    REQUIRE(ll.tx_tlp_out_count() == 2u); // 无重发（disabled 无副作用）
    REQUIRE(ll.tlp_drop_count() == 0u);
    REQUIRE(ll.dllp_drop_count() == 0u);
}