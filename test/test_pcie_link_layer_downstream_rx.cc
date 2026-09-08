// test_pcie_link_layer_downstream_rx.cc
// PcieLinkLayer 下行 (host→EP) Rx 路径测试 (per Q17 双向链路层)
// Author: CppTLM Team
// Date: 2026-09-08
// 参考: openspec/changes/2026-09-01-cpptlm-dgpu-pcie-ip-microarch/decisions.md §Q17
//       specs/link-layer-and-fc/spec.md Scenario "下行(host→EP)Rx 路径(双向)"

#include "bundles/pcie_bundles_tlm.hh"
#include "bundles/pcie_dllp_bundles_tlm.hh"
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "tlm/pcie/pcie_link_layer_tlm.hh"

#include <vector>

using namespace tlm::pcie;
using namespace bundles;

TEST_CASE("DownstreamRx: rx TLP from host produces ACK DLLP to host", "[pcie][ll][downstream]") {
    EventQueue eq;
    PcieLinkLayer ll(&eq);

    std::vector<PcieTlpBundle> tls;
    ll.set_tlp_sink([&tls](const PcieTlpBundle& t) { tls.push_back(t); });

    PcieTlpBundle tlp(PcieTlpBundle::MEM_WRITE, 1, 0x8000, 16, 0, 0x0100, 7);
    REQUIRE(ll.rx_tlp_from_host(tlp) == true);

    // ACK DLLP 生成并返回 host
    REQUIRE(ll.tx_dllp_out_count() == 1u);
    PcieDllpBundle ack;
    REQUIRE(ll.try_pop_tx_dllp(ack) == true);
    REQUIRE(ack.is_ack() == true);
    REQUIRE(ack.seq_num_ack.read() == 0u); // 第一个下行 seq == 0

    // TLP 送事务层 sink
    REQUIRE(tls.size() == 1u);
    REQUIRE(tls[0].offset.read() == 0x8000u);
    REQUIRE(tls[0].trans_id.read() == 7u);
}

TEST_CASE("DownstreamRx: ACK seq increments per received TLP", "[pcie][ll][downstream]") {
    EventQueue eq;
    PcieLinkLayer ll(&eq);

    int sink_count = 0;
    ll.set_tlp_sink([&sink_count](const PcieTlpBundle&) { ++sink_count; });

    PcieTlpBundle tlp(PcieTlpBundle::CFG_READ, 0, 0x0004, 4, 0, 0x0100, 1);
    REQUIRE(ll.rx_tlp_from_host(tlp) == true);
    REQUIRE(ll.rx_tlp_from_host(tlp) == true);
    REQUIRE(ll.rx_tlp_from_host(tlp) == true);

    // 3 个 ACK DLLP，seq 0,1,2
    REQUIRE(ll.tx_dllp_out_count() == 3u);
    uint16_t expected = 0;
    PcieDllpBundle ack;
    while (ll.try_pop_tx_dllp(ack)) {
        REQUIRE(ack.is_ack() == true);
        REQUIRE(ack.seq_num_ack.read() == expected);
        ++expected;
    }
    REQUIRE(expected == 3u);
    REQUIRE(sink_count == 3);
}

TEST_CASE("DownstreamRx: DLLP/TLP dispatch to correct paths", "[pcie][ll][downstream]") {
    EventQueue eq;
    PcieLinkLayer ll(&eq);

    // DLLP → 只走 DLLP 处理，不进 tlp_sink
    int tlp_sink_hits = 0;
    int dllp_sink_hits = 0;
    ll.set_tlp_sink([&tlp_sink_hits](const PcieTlpBundle&) { ++tlp_sink_hits; });
    ll.set_dllp_sink([&dllp_sink_hits](const PcieDllpBundle&) { ++dllp_sink_hits; });

    // UpdateFC DLLP → 更新 FC（下行方向也有效）
    auto ufc = ll.make_update_fc(0, 0, 0);
    REQUIRE(ll.rx_dllp_from_host(ufc) == PcieLinkLayer::Dispatch::UPDATE_FC);
    REQUIRE(dllp_sink_hits == 1);
    REQUIRE(tlp_sink_hits == 0);

    // TLP → 送事务层 + 生成 ACK，不进 dllp_sink
    PcieTlpBundle tlp(PcieTlpBundle::MMIO_READ, 0, 0x0020, 4, 0, 0x0100, 2);
    REQUIRE(ll.rx_tlp_from_host(tlp) == true);
    REQUIRE(tlp_sink_hits == 1);
    REQUIRE(dllp_sink_hits == 1);
}

TEST_CASE("DownstreamRx: FC backpressure blocks TLP forwarding", "[pcie][ll][downstream]") {
    EventQueue eq;
    PcieLinkLayerConfig cfg;
    cfg.fc_capacity = 2;
    cfg.fc_init_p = 2;
    cfg.fc_init_np = 2;
    cfg.fc_init_cpl = 2;
    PcieLinkLayer ll(&eq, cfg);

    int sink_count = 0;
    ll.set_tlp_sink([&sink_count](const PcieTlpBundle&) { ++sink_count; });

    PcieTlpBundle w(PcieTlpBundle::MEM_WRITE, 1, 0x8000, 4, 0, 0x0100, 1);
    REQUIRE(ll.rx_tlp_from_host(w) == true);  // P credit 2→1
    REQUIRE(ll.rx_tlp_from_host(w) == true);  // P credit 1→0
    REQUIRE(ll.rx_tlp_from_host(w) == false); // 反压：不消费，不转发
    REQUIRE(sink_count == 2);
    REQUIRE(ll.tx_dllp_out_count() == 2u); // 只有前 2 个生成 ACK

    // UpdateFC 恢复 → 可继续接收
    auto ufc = ll.make_update_fc(1, 0, 0);
    REQUIRE(ll.rx_dllp_from_host(ufc) == PcieLinkLayer::Dispatch::UPDATE_FC);
    REQUIRE(ll.rx_tlp_from_host(w) == true);
    REQUIRE(sink_count == 3);
}

TEST_CASE("DownstreamRx: downstream retry buffer independent of upstream",
          "[pcie][ll][downstream]") {
    EventQueue eq;
    PcieLinkLayer ll(&eq);

    // 上行：发送 2 TLP → 上行 retry buffer + 上行 seq
    PcieTlpBundle t(PcieTlpBundle::MMIO_WRITE, 0, 0x1000, 4, 1, 0x0100, 1);
    REQUIRE(ll.tx_tlp(t) == true);
    REQUIRE(ll.tx_tlp(t) == true);

    // 下行：接收 2 TLP → ACK 生成
    PcieTlpBundle r(PcieTlpBundle::MEM_READ, 1, 0x8000, 4, 0, 0x0100, 2);
    REQUIRE(ll.rx_tlp_from_host(r) == true);
    REQUIRE(ll.rx_tlp_from_host(r) == true);

    // 上行 retry buffer: 2 (tx), 下行不共享
    REQUIRE(ll.retry_buffer_size() == 2u);
    REQUIRE(ll.next_tx_seq() == 2u); // 上行 seq 0,1
    // 下行 ACK seq 独立从 0 开始
    PcieDllpBundle ack;
    REQUIRE(ll.try_pop_tx_dllp(ack) == true);
    REQUIRE(ack.seq_num_ack.read() == 0u);
    REQUIRE(ll.try_pop_tx_dllp(ack) == true);
    REQUIRE(ack.seq_num_ack.read() == 1u);
}