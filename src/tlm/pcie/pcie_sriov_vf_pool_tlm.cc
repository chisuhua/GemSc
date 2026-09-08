// src/tlm/pcie/pcie_sriov_vf_pool_tlm.cc
// PcieSriovVfPool 实现：dispatch_tlp / dispatch_msix / next_seq / FLR (T-P4-4/5)
// 作者 CppTLM Team / 日期 2026-10-13
#include "tlm/pcie/pcie_sriov_vf_pool_tlm.hh"

namespace tlm::pcie {

    namespace {
        constexpr uint16_t kSeqMask = 0x0FFFu; // 12-bit wrap
    }

    bool PcieSriovVfPool::dispatch_tlp(uint16_t stream_id, const bundles::PcieTlpBundle& tlp) {
        if (!is_valid_stream_id(stream_id)) {
            return false;
        }
        auto& cfg = config_pool_.config_of(stream_id);
        const uint8_t kind = tlp.kind.read();
        const uint16_t cfg_offset = static_cast<uint16_t>(tlp.offset.read());
        const uint32_t trans_id = static_cast<uint32_t>(tlp.trans_id.read());
        switch (kind) {
        case bundles::PcieTlpBundle::CFG_READ:
            // NP 请求: 登记 outstanding Completion 关联 (per Q12)
            completions_.register_np(stream_id, trans_id);
            return true;
        case bundles::PcieTlpBundle::CFG_WRITE:
            cfg.write(cfg_offset, static_cast<uint32_t>(tlp.data.read()));
            return true;
        default:
            return true;
        }
    }

    bool PcieSriovVfPool::dispatch_completion(uint16_t stream_id, uint32_t trans_id,
                                              const CompletionTracker::CplData& cpl) {
        return completions_.complete(stream_id, trans_id, cpl);
    }

    bool PcieSriovVfPool::dispatch_msix(uint16_t stream_id, uint16_t vector) {
        if (!is_valid_stream_id(stream_id)) {
            return false;
        }
        return msix_pool_.update_pending(stream_id, vector);
    }

    uint16_t PcieSriovVfPool::next_seq(uint16_t stream_id) noexcept {
        if (!is_valid_stream_id(stream_id)) {
            return 0;
        }
        const uint16_t s = tlp_seq_[stream_id];
        tlp_seq_[stream_id] = (s + 1u) & kSeqMask;
        return s;
    }

    uint16_t PcieSriovVfPool::seq_of(uint16_t stream_id) const noexcept {
        if (!is_valid_stream_id(stream_id)) {
            return 0;
        }
        return tlp_seq_[stream_id];
    }

    void PcieSriovVfPool::flr_pf() noexcept {
        config_pool_.init_all();
        msix_pool_.init_all();
        completions_.flr_pf();
        tlp_seq_.fill(0);
        ari_router_.set_ari_enabled(false); // ARI Forwarding Enable 是 PF 属性, FLR 后回默认
        for (uint16_t sid = 0; sid < NUM_PORTS; ++sid) {
            fc_engine_.install_bucket(sid, FcTokenBucket());
        }
    }

    void PcieSriovVfPool::flr_vf(uint16_t vf_id) noexcept {
        // vf_id=0 是 PF, 拒绝; 合法 VF 范围 1..16
        if (vf_id < 1 || vf_id >= NUM_PORTS) {
            return;
        }
        config_pool_.config_of(vf_id).init();
        msix_pool_.table_of(vf_id).init();
        completions_.flr_vf(vf_id);
        tlp_seq_[vf_id] = 0;
        fc_engine_.install_bucket(vf_id, FcTokenBucket());
    }

} // namespace tlm::pcie
