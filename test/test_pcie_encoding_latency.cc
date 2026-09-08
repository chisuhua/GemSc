// test_pcie_encoding_latency.cc
// PcieEncodingLatencyModel: 128b/130b 编码块延迟 + 速率切换延迟单元测试
// Author: CppTLM Team
// Date: 2026-09-29
// 参考: openspec/changes/2026-09-29-cpptlm-dgpu-pcie-130b-encoding/specs/130b-encoding/spec.md
//       Scenario "Gen5 链路延迟(x1/x16)" / "速率切换延迟" / "Gen5 吞吐回归"
//
// 单位修正(per Oracle 二次评审 C2/C3):
//   - 速率单位: GT/s per-lane per-direction
//   - 块延迟(128B/1024-bit per-lane): Gen5 x1=32ns, Gen4 x1=64ns, Gen3 x1=128ns
//   - 速率切换延迟: ~µs 级(含 Gen3+ 均衡协商)

#include "catch_amalgamated.hpp"
#include "tlm/pcie/pcie_encoding_latency_model.hh"

using namespace tlm::pcie;

namespace {

    // 容忍四舍五入误差
    constexpr uint64_t kNsTolerance = 1u;   // 1ns
    constexpr uint64_t kUsTolerance = 100u; // 0.1µs

} // namespace

TEST_CASE("PcieEncodingLatencyModel: Gen5 x1 lane 128B block == 32ns",
          "[pcie][encoding][latency]") {
    // 1024 bits / 32 GT/s / 1 lane = 32 ns
    const uint64_t lat =
        PcieEncodingLatencyModel::block_latency_ns(PcieEncodingLatencyModel::Rate::GEN5, 128, 1);
    REQUIRE(lat == 32u);
}

TEST_CASE("PcieEncodingLatencyModel: Gen5 x16 lane 128B block == 2ns",
          "[pcie][encoding][latency]") {
    // 1024 bits / 32 GT/s / 16 lanes = 2 ns(并行加速)
    const uint64_t lat =
        PcieEncodingLatencyModel::block_latency_ns(PcieEncodingLatencyModel::Rate::GEN5, 128, 16);
    REQUIRE(lat == 2u);
}

TEST_CASE("PcieEncodingLatencyModel: Gen3 x1 lane 128B block == 128ns",
          "[pcie][encoding][latency]") {
    // 1024 bits / 8 GT/s / 1 lane = 128 ns
    const uint64_t lat =
        PcieEncodingLatencyModel::block_latency_ns(PcieEncodingLatencyModel::Rate::GEN3, 128, 1);
    REQUIRE(lat == 128u);
}

TEST_CASE("PcieEncodingLatencyModel: Gen4 x4 lane 128B block == 16ns",
          "[pcie][encoding][latency]") {
    // 1024 bits / 16 GT/s / 4 lanes = 16 ns
    const uint64_t lat =
        PcieEncodingLatencyModel::block_latency_ns(PcieEncodingLatencyModel::Rate::GEN4, 128, 4);
    REQUIRE(lat == 16u);
}

TEST_CASE("PcieEncodingLatencyModel: Gen2 x1 lane 128B block == 204ns",
          "[pcie][encoding][latency]") {
    // 1024 bits / 5 GT/s / 1 lane = 204.8 ns → 整 204 或 205(浮点取舍)
    const uint64_t lat =
        PcieEncodingLatencyModel::block_latency_ns(PcieEncodingLatencyModel::Rate::GEN2, 128, 1);
    REQUIRE((lat == 204u || lat == 205u));
}

TEST_CASE("PcieEncodingLatencyModel: 速率切换 GEN3→GEN5 >= 1µs(含均衡协商)",
          "[pcie][encoding][latency][rate-switch]") {
    // Gen3+ 含均衡协商, µs 级延迟
    const uint64_t delay_us = PcieEncodingLatencyModel::rate_switch_delay_us(
        PcieEncodingLatencyModel::Rate::GEN3, PcieEncodingLatencyModel::Rate::GEN5);
    REQUIRE(delay_us >= 1u);
}

TEST_CASE("PcieEncodingLatencyModel: 速率切换 GEN5→GEN1 仍 >= 1µs(反向协商)",
          "[pcie][encoding][latency][rate-switch]") {
    const uint64_t delay_us = PcieEncodingLatencyModel::rate_switch_delay_us(
        PcieEncodingLatencyModel::Rate::GEN5, PcieEncodingLatencyModel::Rate::GEN1);
    REQUIRE(delay_us >= 1u);
}

TEST_CASE("PcieEncodingLatencyModel: 速率切换同速率 = 0µs(无协商)",
          "[pcie][encoding][latency][rate-switch]") {
    const uint64_t delay_us = PcieEncodingLatencyModel::rate_switch_delay_us(
        PcieEncodingLatencyModel::Rate::GEN5, PcieEncodingLatencyModel::Rate::GEN5);
    REQUIRE(delay_us == 0u);
}

TEST_CASE("PcieEncodingLatencyModel: 边界 lanes=0 返回 0ns(保护)",
          "[pcie][encoding][latency][edge]") {
    const uint64_t lat =
        PcieEncodingLatencyModel::block_latency_ns(PcieEncodingLatencyModel::Rate::GEN5, 128, 0);
    REQUIRE(lat == 0u);
}

TEST_CASE("PcieEncodingLatencyModel: 边界 block_size=0 返回 0ns",
          "[pcie][encoding][latency][edge]") {
    const uint64_t lat =
        PcieEncodingLatencyModel::block_latency_ns(PcieEncodingLatencyModel::Rate::GEN5, 0, 1);
    REQUIRE(lat == 0u);
}

TEST_CASE("PcieEncodingLatencyModel: Rate 枚举值映射正确", "[pcie][encoding][latency][enum]") {
    // per spec: GEN1=2, GEN2=5, GEN3=8, GEN4=16, GEN5=32 GT/s
    REQUIRE(static_cast<uint32_t>(PcieEncodingLatencyModel::Rate::GEN1) == 2u);
    REQUIRE(static_cast<uint32_t>(PcieEncodingLatencyModel::Rate::GEN2) == 5u);
    REQUIRE(static_cast<uint32_t>(PcieEncodingLatencyModel::Rate::GEN3) == 8u);
    REQUIRE(static_cast<uint32_t>(PcieEncodingLatencyModel::Rate::GEN4) == 16u);
    REQUIRE(static_cast<uint32_t>(PcieEncodingLatencyModel::Rate::GEN5) == 32u);
}

TEST_CASE("PcieEncodingLatencyModel: rate_gtps() 公开枚举对应数值",
          "[pcie][encoding][latency][enum]") {
    REQUIRE(PcieEncodingLatencyModel::rate_gtps(PcieEncodingLatencyModel::Rate::GEN5) == 32u);
    REQUIRE(PcieEncodingLatencyModel::rate_gtps(PcieEncodingLatencyModel::Rate::GEN3) == 8u);
    REQUIRE(PcieEncodingLatencyModel::rate_gtps(PcieEncodingLatencyModel::Rate::GEN1) == 2u);
}
