// src/tlm/gpu/sm/lsu_lds_tlm.cc
// LsuLDS: tick() 接线 (Task 2.10 P1-10, per Oracle 预审 Task 2.10)
//
// stub tick() 行为:
//   - 模拟 sa_/va_/mc_/sl_/lg_ 安全检查
//   - 判 pipe == kLsuLDS + is_memory → parent_->lsu_lds()->execute() (同步回写)
//
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18b P1-10 端口接线)
#include "tlm/gpu/sm/lsu_lds_tlm.hh"
#include "tlm/gpu/streaming_multiprocessor_tlm.hh"

namespace tlm::sm {

    LsuLDS::LsuLDS(const std::string& n, EventQueue* eq)
        : ChStreamModuleBase(n, eq), parent_(nullptr) {
    }

    void LsuLDS::tick() {
        if (!parent_ || !parent_->fu() || !parent_->fu()->has_fetched()) {
            return;
        }
        auto d = parent_->fu()->fetched().instr_desc;
        if (d.pipe == cpptlm::gpu::PipeClass::kLsuLDS && d.is_memory) {
            parent_->lsu_lds()->execute(d);
        }
    }

} // namespace tlm::sm
