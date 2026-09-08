// src/tlm/pcie/pcie_completion_tracker_tlm.cc
// CompletionTracker 实现 (T-P4-6, per Q12)
// 作者 CppTLM Team / 日期 2026-10-13
#include "tlm/pcie/pcie_completion_tracker_tlm.hh"

namespace tlm::pcie {

    namespace {
        constexpr uint16_t kNumSlots = 17; // 0=PF, 1..16=VF0..VF15
    }

    void CompletionTracker::init() {
        for (auto& st : per_vf_) {
            st.outstanding.clear();
            st.completed = 0;
        }
    }

    bool CompletionTracker::register_np(uint16_t vf_id, uint32_t trans_id) {
        if (vf_id >= kNumSlots) {
            return false;
        }
        auto& st = per_vf_[vf_id];
        if (st.outstanding.size() >= DEFAULT_CAPACITY) {
            return false; // 溢出: N+1 拒绝新发出
        }
        st.outstanding.emplace(trans_id, CplData{});
        return true;
    }

    bool CompletionTracker::complete(uint16_t vf_id, uint32_t trans_id, const CplData& cpl) {
        if (vf_id >= kNumSlots) {
            return false;
        }
        auto& st = per_vf_[vf_id];
        auto it = st.outstanding.find(trans_id);
        if (it == st.outstanding.end()) {
            return false; // 未匹配
        }
        it->second = cpl; // 记录 CplD 载荷
        st.outstanding.erase(it);
        ++st.completed;
        return true;
    }

    std::size_t CompletionTracker::outstanding_count(uint16_t vf_id) const {
        if (vf_id >= kNumSlots) {
            return 0;
        }
        return per_vf_[vf_id].outstanding.size();
    }

    std::size_t CompletionTracker::completed_count(uint16_t vf_id) const {
        if (vf_id >= kNumSlots) {
            return 0;
        }
        return per_vf_[vf_id].completed;
    }

    void CompletionTracker::flr_vf(uint16_t vf_id) {
        if (vf_id >= kNumSlots) {
            return;
        }
        per_vf_[vf_id].outstanding.clear();
    }

    void CompletionTracker::flr_pf() {
        for (auto& st : per_vf_) {
            st.outstanding.clear();
        }
    }

} // namespace tlm::pcie
