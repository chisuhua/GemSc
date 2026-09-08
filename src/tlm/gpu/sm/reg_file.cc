// src/tlm/gpu/sm/reg_file.cc
// RegFileUnit 真值类实现 (per SM 微架构 §15.5.6)
//
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18b P1-11 真值, per Oracle 预审 Task 2.11)
#include "tlm/gpu/sm/reg_file.hh"

namespace cpptlm {
    namespace gpu {

        void RegFileUnit::write(uint32_t warp_id, uint32_t reg_id, uint64_t value) {
            regs_[make_key(warp_id, reg_id)] = value;
        }

        bool RegFileUnit::read(uint32_t warp_id, uint32_t reg_id, uint64_t* out_value) const {
            if (!out_value)
                return false;
            auto it = regs_.find(make_key(warp_id, reg_id));
            if (it == regs_.end())
                return false;
            *out_value = it->second;
            return true;
        }

    } // namespace gpu
} // namespace cpptlm
