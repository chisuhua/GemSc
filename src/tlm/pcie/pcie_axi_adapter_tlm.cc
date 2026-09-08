// src/tlm/pcie/pcie_axi_adapter_tlm.cc
// PcieAxiAdapter 实现（64-byte burst 写序列化 + AXI Stream Adapter 持有）
// 功能描述：将 EP 发起的 burst 写转换为多拍 W 通道（awlen+1 拍, wlast 最后一拍），
//           总字节 = (awlen+1) × 2^awsize。
// 作者 CppTLM Team / 日期 2026-11-03
// 参考: openspec/changes/2026-11-03-cpptlm-dgpu-axi-stream-adapter/spec.md

#include "tlm/pcie/pcie_axi_adapter_tlm.hh"

#include "tlm/pcie/pcie_endpoint_ip.hh"

#include <memory>
#include <unordered_map>

namespace tlm::pcie {

    namespace {

        // PcieEndpointIP ↔ PcieAxiAdapter composition 注册表（按 EP 模块名）
        // 冻结 .h 布局不能加成员 → EP 侧通过 on_config_loaded() 挂接，这里持有生命周期。
        std::unordered_map<std::string, std::unique_ptr<PcieAxiAdapter>>& axi_registry() {
            static std::unordered_map<std::string, std::unique_ptr<PcieAxiAdapter>> registry;
            return registry;
        }

    } // namespace

    PcieAxiAdapter* PcieAxiAdapter::attach_to_endpoint(const std::string& endpoint_name,
                                                       EventQueue* eq) {
        auto& reg = axi_registry();
        auto it = reg.find(endpoint_name);
        if (it != reg.end()) {
            it->second = std::make_unique<PcieAxiAdapter>(nullptr, eq);
            return it->second.get();
        }
        auto [new_it, _] =
            reg.emplace(endpoint_name, std::make_unique<PcieAxiAdapter>(nullptr, eq));
        return new_it->second.get();
    }

    PcieAxiAdapter* PcieAxiAdapter::for_endpoint(const std::string& endpoint_name) noexcept {
        auto& reg = axi_registry();
        auto it = reg.find(endpoint_name);
        return (it != reg.end()) ? it->second.get() : nullptr;
    }

    void PcieAxiAdapter::detach_from_endpoint(const std::string& endpoint_name) noexcept {
        axi_registry().erase(endpoint_name);
    }

    PcieAxiAdapter::PcieAxiAdapter(PcieEndpointIP* ep, EventQueue* eq) : ep_(ep), eq_(eq) {
    }

    bool PcieAxiAdapter::master_write_burst(const bundles::Axi4Bundle& req) {
        if (burst_active_) {
            return false; // 已有 burst 在途（backpressure）
        }
        awid_ = static_cast<uint8_t>(req.awid.read());
        awaddr_ = req.awaddr.read();
        awlen_ = static_cast<uint8_t>(req.awlen.read());
        awsize_ = static_cast<uint8_t>(req.awsize.read());
        awburst_ = static_cast<uint8_t>(req.awburst.read());

        // 总传输字节 = (len+1) × 2^awsize
        const uint64_t beats = static_cast<uint64_t>(awlen_) + 1u;
        total_bytes_ = static_cast<uint32_t>(beats << awsize_);
        num_beats_ = static_cast<std::size_t>(beats);

        burst_active_ = true;
        beat_index_ = 0;
        beats_sent_ = 0;
        cur_written_ = false;
        return true;
    }

    bool PcieAxiAdapter::write_beat(uint64_t data, uint64_t strb) {
        if (!burst_active_) {
            return false;
        }
        if (beat_index_ >= num_beats_) {
            return false;
        }
        cur_data_ = data;
        cur_strb_ = strb;
        cur_written_ = true;
        return true;
    }

    bool PcieAxiAdapter::current_beat_is_last() const noexcept {
        if (!burst_active_) {
            return false;
        }
        return beat_index_ == (num_beats_ - 1);
    }

    bool PcieAxiAdapter::push_beat_to_downstream() {
        if (!burst_active_ || !cur_written_) {
            return false;
        }
        if (beat_index_ >= num_beats_) {
            return false;
        }

        bundles::Axi4Bundle beat;
        beat.awid.write(awid_);
        // INCR burst: 地址 = base + beat_index × 2^awsize
        beat.awaddr.write(awaddr_ + (static_cast<uint64_t>(beat_index_) << awsize_));
        beat.awlen.write(awlen_);
        beat.awsize.write(awsize_);
        beat.awburst.write(awburst_);
        beat.wdata.write(cur_data_);
        beat.wstrb.write(cur_strb_);
        beat.wlast.write(current_beat_is_last() ? 1 : 0);

        if (beat_index_ == 0) {
            outstanding_wr_ids_.push_back(static_cast<uint16_t>(awid_));
        }

        if (!axi_.master_req(beat, beat_index_ == 0)) {
            return false; // 下游未消费上一拍（backpressure）
        }

        ++beat_index_;
        ++beats_sent_;
        cur_written_ = false;

        if (beat_index_ >= num_beats_) {
            burst_active_ = false; // burst 完成
        }
        return true;
    }

    bool PcieAxiAdapter::burst_complete() const noexcept {
        return !burst_active_ && num_beats_ > 0 && beats_sent_ == num_beats_;
    }

    void PcieAxiAdapter::reset_burst() {
        burst_active_ = false;
        awid_ = 0;
        awaddr_ = 0;
        awlen_ = 0;
        awsize_ = 0;
        awburst_ = 0;
        total_bytes_ = 0;
        num_beats_ = 0;
        beat_index_ = 0;
        beats_sent_ = 0;
        cur_data_ = 0;
        cur_strb_ = 0;
        cur_written_ = false;
    }

} // namespace tlm::pcie
