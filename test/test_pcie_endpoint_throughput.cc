// test_pcie_endpoint_throughput.cc
// PcieLinkLayer 吞吐回归测试: Gen5 速率下 128B 块传输吞吐 >= 95% 理论带宽
// Author: CppTLM Team
// Date: 2026-09-29
// 参考: openspec/changes/2026-09-29-cpptlm-dgpu-pcie-130b-encoding/
//       specs/130b-encoding/spec.md Scenario "Gen5 吞吐回归"
//         WHEN credit 充足 + 无拥塞 + 纯 memcpy 流 THEN 吞吐 >= 95% 理论带宽
//       proposal.md T-P2-2 (方式 1: 累加 _delay_ns 计数, try_pop_tx_tlp 读取时检查)
//
// 理论带宽:
//   Gen5 x16 = 32 GT/s × 16 lanes × (128/130 编码) ≈ 63 GB/s (real-world)
//   模型无 130b 开销: 128B / 2ns = 64 GB/s (model cap)
// 失败模式(注入前): try_pop 立即全部出队 → elapsed≈0 → 吞吐无限 → 上界断言 FAIL

#include "bundles/pcie_bundles_tlm.hh"
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "tlm/pcie/pcie_link_layer_tlm.hh"

using namespace tlm::pcie;
using namespace bundles;

namespace {

    constexpr double kModelCapGBs = 64.0;        // Gen5 x16 模型上限 (128B/2ns)
    constexpr double kRealTheoreticalGBs = 63.0; // Gen5 x16 真实理论 (含 130b 开销)

    // 构造纯 memcpy 流: 连续 MEM_WRITE 128B 块 (bulk, data=0 descriptor)
    PcieTlpBundle make_tlp(uint32_t tid) {
        return PcieTlpBundle(/*kind=*/PcieTlpBundle::MEM_WRITE,
                             /*bar=*/1, /*off=*/0x1000, /*size=*/128,
                             /*data=*/0, /*rid=*/0x0100, /*tid=*/tid);
    }

    // credit 充足配置: 4096 容量 ≥ 发送量, 无拥塞
    PcieLinkLayerConfig make_abundant_config() {
        PcieLinkLayerConfig cfg;
        cfg.fc_capacity = 4096;
        cfg.fc_init_p = 4096;
        cfg.fc_init_np = 4096;
        cfg.fc_init_cpl = 4096;
        cfg.retry_buffer_size = 4096;
        return cfg;
    }

} // namespace

TEST_CASE("PcieLinkLayer 吞吐: Gen5 x16 128B 块 >= 95% 理论带宽",
          "[pcie][ll][throughput][encoding]") {
    EventQueue eq;
    PcieLinkLayer ll(&eq, make_abundant_config());
    // 开启 128b/130b 编码延迟: Gen5, x16 lanes, 128B 参考块
    ll.set_encoding_latency(PcieEncodingLatencyModel::Rate::GEN5,
                            /*active_lanes=*/16, /*block_bytes=*/128);

    // 纯 memcpy 流: 1024 个 128B 块 (retry_buf SEQ_WINDOW=2048 上限内)
    constexpr std::size_t kNumBlocks = 1024;
    for (std::size_t i = 0; i < kNumBlocks; ++i) {
        REQUIRE(ll.tx_tlp(make_tlp(static_cast<uint32_t>(i))) == true);
    }
    REQUIRE(ll.tx_tlp_out_count() == kNumBlocks);

    // 消耗 wire 输出, 以 EventQueue cycle 作为虚拟 ns 时钟推进
    // (方式 1: try_pop 在 wire 未完成前一块传输时返回 false → 测试推进时钟)
    std::size_t popped = 0;
    PcieTlpBundle out;
    const uint64_t start_cycle = eq.getCurrentCycle();
    uint64_t guard = 0;
    constexpr uint64_t kMaxNs = 10'000'000; // 10ms 安全上限 (1024×2ns=2048ns 理论)
    while (popped < kNumBlocks && guard++ < kMaxNs) {
        if (ll.try_pop_tx_tlp(out)) {
            ++popped;
            continue;
        }
        eq.run(1); // 推进 1 cycle = 1 ns
    }
    const uint64_t elapsed_ns = eq.getCurrentCycle() - start_cycle;
    REQUIRE(popped == kNumBlocks);
    REQUIRE(elapsed_ns > 0);

    // 吞吐 = N × 128B / elapsed_ns (B/ns = GB/s)
    const double bw = static_cast<double>(kNumBlocks * 128u) / static_cast<double>(elapsed_ns);
    // 下界: >= 95% 真实理论带宽 (63 GB/s)
    REQUIRE(bw >= 0.95 * kRealTheoreticalGBs);
    // 上界: 不超过模型上限 (64 GB/s) 的 5% — 无延迟注入时吞吐无限 → 此断言 FAIL
    REQUIRE(bw <= 1.05 * kModelCapGBs);
}

TEST_CASE("PcieLinkLayer 吞吐: Gen5 x1 128B 块吞吐约为 x16 的 1/16 (并行加速)",
          "[pcie][ll][throughput][encoding]") {
    EventQueue eq;
    PcieLinkLayer ll(&eq, make_abundant_config());
    ll.set_encoding_latency(PcieEncodingLatencyModel::Rate::GEN5,
                            /*active_lanes=*/1, /*block_bytes=*/128);

    constexpr std::size_t kNumBlocks = 1024;
    for (std::size_t i = 0; i < kNumBlocks; ++i) {
        REQUIRE(ll.tx_tlp(make_tlp(static_cast<uint32_t>(i))) == true);
    }

    std::size_t popped = 0;
    PcieTlpBundle out;
    const uint64_t start_cycle = eq.getCurrentCycle();
    uint64_t guard = 0;
    constexpr uint64_t kMaxNs = 10'000'000; // x1 理论 1024×32ns=32768ns
    while (popped < kNumBlocks && guard++ < kMaxNs) {
        if (ll.try_pop_tx_tlp(out)) {
            ++popped;
            continue;
        }
        eq.run(1);
    }
    const uint64_t elapsed_ns = eq.getCurrentCycle() - start_cycle;
    REQUIRE(popped == kNumBlocks);

    const double bw = static_cast<double>(kNumBlocks * 128u) / static_cast<double>(elapsed_ns);
    // x1 = 4 GB/s 模型 (128B/32ns), 容忍 ±10%
    REQUIRE(bw >= 0.9 * 4.0);
    REQUIRE(bw <= 1.1 * 4.0);
}

TEST_CASE("PcieLinkLayer 吞吐: 未启用编码延迟时 wire 立即出队 (Phase 1 回归)",
          "[pcie][ll][throughput][regression]") {
    EventQueue eq;
    PcieLinkLayer ll(&eq, make_abundant_config());
    // 默认: 无编码延迟 → Phase 1 行为不变
    REQUIRE(ll.encoding_latency_enabled() == false);

    REQUIRE(ll.tx_tlp(make_tlp(1)) == true);
    REQUIRE(ll.tx_tlp(make_tlp(2)) == true);
    PcieTlpBundle out;
    REQUIRE(ll.try_pop_tx_tlp(out) == true);
    REQUIRE(ll.try_pop_tx_tlp(out) == true);
    REQUIRE(ll.try_pop_tx_tlp(out) == false); // 队列已空
}

TEST_CASE("PcieLinkLayer 吞吐: 编码延迟下 wire 在块传输期间阻塞后续 TLP",
          "[pcie][ll][throughput][encoding][blocking]") {
    EventQueue eq;
    PcieLinkLayer ll(&eq, make_abundant_config());
    ll.set_encoding_latency(PcieEncodingLatencyModel::Rate::GEN5,
                            /*active_lanes=*/16, /*block_bytes=*/128);

    REQUIRE(ll.tx_tlp(make_tlp(1)) == true);
    REQUIRE(ll.tx_tlp(make_tlp(2)) == true);

    // cycle 0: 首个块 0→2ns 传输中 → try_pop 返回 false (wire busy)
    PcieTlpBundle out;
    REQUIRE(ll.try_pop_tx_tlp(out) == false);
    REQUIRE(ll.try_pop_tx_tlp(out) == false);

    // 推进到 2ns: 第 1 块完成 → 出队
    eq.run(2);
    REQUIRE(ll.try_pop_tx_tlp(out) == true);
    // 第 2 块 2→4ns 传输中 → 阻塞
    REQUIRE(ll.try_pop_tx_tlp(out) == false);
    // 推进到 4ns: 第 2 块完成
    eq.run(2);
    REQUIRE(ll.try_pop_tx_tlp(out) == true);
}