// src/tlm/pcie/pcie_flow_control_token_bucket.cc
// FcTokenBucket + FcEngine 实现：PCIe Flow Control Token Bucket 引擎
// 作者 CppTLM Team / 日期 2026-09-08
// 参考: openspec/changes/2026-09-01-cpptlm-dgpu-pcie-ip-microarch/decisions.md §Q2

#include "tlm/pcie/pcie_flow_control_token_bucket.hh"

#include <algorithm>

namespace tlm::pcie {

    // ========== FcTokenBucket ==========

    bool FcTokenBucket::can_send(Type t) const noexcept {
        return tokens(t) >= weight(t);
    }

    bool FcTokenBucket::consume(Type t) noexcept {
        if (tokens(t) < weight(t)) {
            // over-consume: 返回 false，不扣减（spec Scenario "基础行为"）
            return false;
        }
        set_tokens(t, tokens(t) - weight(t));
        return true;
    }

    void FcTokenBucket::update(Type t, uint32_t credit) noexcept {
        // UpdateFC DLLP 到达时唯一补充路径；credit 单调非减，不超 capacity
        const uint32_t next = tokens(t) + credit;
        if (next > capacity_) {
            set_tokens(t, capacity_);
        } else {
            set_tokens(t, next);
        }
    }

    uint32_t FcTokenBucket::token_count(Type t) const noexcept {
        return tokens(t);
    }

    void FcTokenBucket::init_fc(uint32_t cap, uint32_t p, uint32_t np, uint32_t cpl) noexcept {
        capacity_ = std::max({cap, p, np, cpl});
        tokens_p_ = p;
        tokens_np_ = np;
        tokens_cpl_ = cpl;
    }

    uint32_t FcTokenBucket::weight(Type t) const noexcept {
        switch (t) {
        case Type::Posted:
            return weight_p_;
        case Type::NonPosted:
            return weight_np_;
        case Type::Completion:
            return weight_cpl_;
        }
        return 1;
    }

    // ========== FcEngine ==========

    FcTokenBucket& FcEngine::bucket(uint32_t vf_id) {
        return per_vf_buckets_[vf_id];
    }

    void FcEngine::install_bucket(uint32_t vf_id, FcTokenBucket bucket) {
        bucket.set_event_queue(eq_);
        per_vf_buckets_[vf_id] = std::move(bucket);
    }

    bool FcEngine::can_send(uint32_t vf_id, FcTokenBucket::Type t) {
        return bucket(vf_id).can_send(t);
    }

    bool FcEngine::consume(uint32_t vf_id, FcTokenBucket::Type t) {
        return bucket(vf_id).consume(t);
    }

    void FcEngine::update(uint32_t vf_id, FcTokenBucket::Type t, uint32_t credit) {
        bucket(vf_id).update(t, credit);
    }

} // namespace tlm::pcie