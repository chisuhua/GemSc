// src/tlm/gpu/sm/hazard_tracker.cc
// HazardTracker 真值类实现 (per SM 微架构 §15.5.6)
//
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18 L5 真值, per Oracle 预审 Task 2.13)
#include "tlm/gpu/sm/hazard_tracker.hh"

namespace cpptlm {
    namespace gpu {

        // === kHardwareCounter ===
        void HazardTracker::increment_vmcnt(uint32_t warp, uint32_t vgpr) {
            vmcnts_[make_key(warp, vgpr)]++;
        }

        void HazardTracker::decrement_vmcnt(uint32_t warp, uint32_t vgpr) {
            auto k = make_key(warp, vgpr);
            auto it = vmcnts_.find(k);
            if (it != vmcnts_.end() && it->second > 0) {
                --it->second;
                if (it->second == 0) {
                    vmcnts_.erase(it);
                }
            }
            // 未 increment 过的 key decrement 是 no-op (per plan v3 测试语义: 初始 vmcnt=0)
        }

        uint32_t HazardTracker::vmcnt(uint32_t warp, uint32_t vgpr) const {
            auto it = vmcnts_.find(make_key(warp, vgpr));
            return it != vmcnts_.end() ? it->second : 0;
        }

        bool HazardTracker::is_stalled_vmcnt(uint32_t warp, uint32_t vgpr, uint32_t N) const {
            return vmcnt(warp, vgpr) > N;
        }

        // === kVirtualReg RAW hazard ===
        bool HazardTracker::can_allocate(uint32_t warp, uint32_t vgpr) const {
            return allocated_vregs_.find(make_key(warp, vgpr)) == allocated_vregs_.end();
        }

        void HazardTracker::allocate(uint32_t warp, uint32_t vgpr) {
            allocated_vregs_.insert(make_key(warp, vgpr));
        }

        void HazardTracker::release(uint32_t warp, uint32_t vgpr) {
            allocated_vregs_.erase(make_key(warp, vgpr));
        }

    } // namespace gpu
} // namespace cpptlm
