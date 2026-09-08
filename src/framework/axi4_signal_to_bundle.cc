// src/framework/axi4_signal_to_bundle.cc
// Axi4Bundle → AXI4Signals 转换实现
// 功能描述：将 TLM Bundle 的所有字段写入信号平面结构体
// 作者 CppTLM Team / 日期 2026-12-22
// 参考: openspec/changes/2026-12-22-cpptlm-dgpu-axi4-mapper/tasks.md T-P6-1

#include "framework/axi4_signal_to_bundle.hh"

namespace cpptlm {

    void bundle_to_signal(const bundles::Axi4Bundle& bundle, AXI4Signals& signals) {
        // ========== 写地址通道 ==========
        signals.awaddr = bundle.awaddr.read();
        signals.awlen = static_cast<uint8_t>(bundle.awlen.read());
        signals.awsize = static_cast<uint8_t>(bundle.awsize.read());
        signals.awburst = static_cast<uint8_t>(bundle.awburst.read());
        signals.awid = static_cast<uint16_t>(bundle.awid.read());

        // ========== 写数据通道 ==========
        signals.wdata = bundle.wdata.read();
        signals.wstrb = bundle.wstrb.read();
        signals.wlast = bundle.wlast.read() ? 1 : 0;

        // ========== 写响应通道 ==========
        signals.bid = static_cast<uint16_t>(bundle.bid.read());
        signals.bresp = static_cast<uint8_t>(bundle.bresp.read());

        // ========== 读地址通道 ==========
        signals.araddr = bundle.araddr.read();
        signals.arlen = static_cast<uint8_t>(bundle.arlen.read());
        signals.arsize = static_cast<uint8_t>(bundle.arsize.read());
        signals.arburst = static_cast<uint8_t>(bundle.arburst.read());
        signals.arid = static_cast<uint16_t>(bundle.arid.read());

        // ========== 读数据通道 ==========
        signals.rid = static_cast<uint16_t>(bundle.rid.read());
        signals.rdata = bundle.rdata.read();
        signals.rresp = static_cast<uint8_t>(bundle.rresp.read());
        signals.rlast = bundle.rlast.read() ? 1 : 0;
    }

} // namespace cpptlm