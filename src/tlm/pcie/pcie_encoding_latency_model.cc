// src/tlm/pcie/pcie_encoding_latency_model.cc
// PcieEncodingLatencyModel 实现：128b/130b 块延迟 + 速率切换延迟
// 作者 CppTLM Team / 日期 2026-09-29
// 参考: openspec/changes/2026-09-29-cpptlm-dgpu-pcie-130b-encoding/
//       specs/130b-encoding/spec.md "Requirement: encoding-latency-model"

#include "tlm/pcie/pcie_encoding_latency_model.hh"

namespace tlm::pcie {

    uint64_t PcieEncodingLatencyModel::block_latency_ns(Rate rate, std::size_t block_bytes,
                                                        std::size_t active_lanes) noexcept {
        // 边界保护: 0 lanes (防除零) / 0 block → 无传输 → 0 延迟
        if (active_lanes == 0u || block_bytes == 0u)
            return 0u;

        const uint64_t rate_gt = static_cast<uint64_t>(rate); // GT/s per-lane
        if (rate_gt == 0u)
            return 0u;

        // block_latency_ns = (block_bytes × 8 bits) / rate_GT/s / active_lanes
        //   精确计算: 先乘后除 (64-bit 足够 128B×8 = 1024 bits, 无溢出)
        const uint64_t bits = static_cast<uint64_t>(block_bytes) * 8u;
        const uint64_t total = bits / rate_gt; // per-lane 全速率耗时
        const uint64_t per_lane = total / static_cast<uint64_t>(active_lanes);
        return per_lane;
    }

    uint32_t PcieEncodingLatencyModel::gen_index(Rate rate) noexcept {
        switch (rate) {
        case Rate::GEN1:
            return 0u;
        case Rate::GEN2:
            return 1u;
        case Rate::GEN3:
            return 2u;
        case Rate::GEN4:
            return 3u;
        case Rate::GEN5:
            return 4u;
        }
        return 0u;
    }

    uint64_t PcieEncodingLatencyModel::rate_switch_delay_us(Rate from, Rate to) noexcept {
        // 同速率: 无链路重训练 → 0µs
        if (from == to)
            return 0u;

        // ~µs 级切换延迟 (per spec.md "速率切换延迟" Scenario)
        // 含 Gen3+ 均衡协商: 涉及从 128b/130b → 8b/10b (Gen1/2) 或 128b/130b
        // (Gen3/4/5) 编码模式切换 + 链路重训练。统一取 1µs 量级:
        //   - 跨编码族 (Gen1/2 ↔ Gen3/4/5) 需完整重训练 + 均衡 → 2µs
        //   - 同族 (Gen3↔Gen4↔Gen5) 均衡协商为主 → 1µs
        //   反向切换 (GEN5→GEN1) 同样 ≥ 1µs (downshift 重训练)。
        const uint32_t fi = gen_index(from);
        const uint32_t ti = gen_index(to);
        const bool cross_family = ((fi < 2u) != (ti < 2u)); // Gen1/2 vs Gen3/4/5
        return cross_family ? 2u : 1u;
    }

} // namespace tlm::pcie