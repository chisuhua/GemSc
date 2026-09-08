// src/framework/axi4_bundle_to_signal.cc
// AXI4Signals → Axi4Bundle 转换实现
// 功能描述：将信号平面结构体还原为 TLM Bundle
// 作者 CppTLM Team / 日期 2026-12-22
// 参考: openspec/changes/2026-12-22-cpptlm-dgpu-axi4-mapper/tasks.md T-P6-1

#include "framework/axi4_bundle_to_signal.hh"

namespace cpptlm {

    void signal_to_bundle(const AXI4Signals& signals, bundles::Axi4Bundle& bundle) {
        // ========== 写地址通道 ==========
        bundle.awaddr.write(signals.awaddr);
        bundle.awlen.write(signals.awlen);
        bundle.awsize.write(signals.awsize);
        bundle.awburst.write(signals.awburst);
        bundle.awid.write(signals.awid);

        // ========== 写数据通道 ==========
        bundle.wdata.write(signals.wdata);
        bundle.wstrb.write(signals.wstrb);
        bundle.wlast.write(signals.wlast != 0);

        // ========== 写响应通道 ==========
        bundle.bid.write(signals.bid);
        bundle.bresp.write(signals.bresp);

        // ========== 读地址通道 ==========
        bundle.araddr.write(signals.araddr);
        bundle.arlen.write(signals.arlen);
        bundle.arsize.write(signals.arsize);
        bundle.arburst.write(signals.arburst);
        bundle.arid.write(signals.arid);

        // ========== 读数据通道 ==========
        bundle.rid.write(signals.rid);
        bundle.rdata.write(signals.rdata);
        bundle.rresp.write(signals.rresp);
        bundle.rlast.write(signals.rlast != 0);
    }

} // namespace cpptlm