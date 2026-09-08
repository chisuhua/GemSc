// test_pcie_link_layer_no_rx_buffer_leak.cc
// Issue #2: downstream_rx_buf_ 只写不读、无界增长（内存泄漏 + 语义错误）
//   Bug: 每个下行 TLP push_back 到 downstream_rx_buf_，无访问器、从不读取
//   Fix: 删除该成员（语义 = 只为 ACK 生成，TLP 直接送事务层 sink）
// Author: CppTLM Team
// Date: 2026-09-01
// 参考: openspec/changes/2026-09-08-cpptlm-dgpu-pcie-link-layer-and-fc
//       Phase 1 Oracle Critical Issue #2

#include "bundles/pcie_bundles_tlm.hh"
#include "bundles/pcie_dllp_bundles_tlm.hh"
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "tlm/pcie/pcie_link_layer_tlm.hh"

using namespace tlm::pcie;
using namespace bundles;

TEST_CASE("NoRxLeak: 1000 downstream TLPs leave no internal rx buffer accumulation",
          "[pcie][ll][no-rx-leak]") {
    EventQueue eq;
    PcieLinkLayerConfig cfg;
    cfg.fc_capacity = 2000; // 足够容纳 1000 MEM_WRITE（Posted）不触发反压
    cfg.fc_init_p = 2000;
    PcieLinkLayer ll(&eq, cfg);

    int sink_count = 0;
    ll.set_tlp_sink([&sink_count](const PcieTlpBundle&) { ++sink_count; });

    PcieTlpBundle tlp(PcieTlpBundle::MEM_WRITE, 1, 0x8000, 4, 0, 0x0100, 7);
    for (int i = 0; i < 1000; ++i) {
        REQUIRE(ll.rx_tlp_from_host(tlp) == true);
    }

    // 全部送事务层
    REQUIRE(sink_count == 1000);
    // 全部生成 ACK（每次收 TLP 一个累积 ACK）
    REQUIRE(ll.tx_dllp_out_count() == 1000u);

    // 内部不累积任何下行 TLP 副本（修复后 downstream_rx_buf_ 不存在 → 0）
    REQUIRE(ll.downstream_rx_buffer_size() == 0u);
}