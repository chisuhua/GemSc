// src/tlm/gpu/sm/issue_unit_tlm.cc
// IssueUnitTLM: tick() 真值实现 (Task 2.4 P1-4, per Oracle 预审 Task 2.4)
//
// 真值路径 (per Oracle Q13):
//   - 检查 parent_ + parent_->du() + parent_->du()->has_decoded()
//   - 失败 → issued_valid_ = false, 安全返回
//   - 成功 → 继承字段透传 (无 slice) + Round-robin 调度 warp_id
//
// 调度公式: warp_id = (last_issued_warp_id_ + 1) % num_warps_
//   首 tick: last=0 → warp 1 (约定)
//   4 cycle 序列: 1, 2, 3, 0 (wrap-around)
//
// 数据流:
//   DecodeToIssueBundle → IssueUnitTLM → IssueToExecBundle → Exec/ScalarALU
//
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18b P1-4 真值)
#include "tlm/gpu/sm/issue_unit_tlm.hh"
#include "tlm/gpu/streaming_multiprocessor_tlm.hh"

namespace tlm::sm {

IssueUnitTLM::IssueUnitTLM(const std::string& n, EventQueue* eq)
    : ChStreamModuleBase(n, eq), parent_(nullptr) {}
    // parent_ 由 SM 构造函数 make_unique 后调 set_parent(this) 注入
    // (per Oracle 预审 Task 2.4 F-2 P0)

void IssueUnitTLM::tick() {
    // 安全检查: parent + du + du->has_decoded 三段
    if (!parent_ || !parent_->du() || !parent_->du()->has_decoded()) {
        issued_valid_ = false;
        return;
    }
    // 从 DecodeUnitTLM.decoded() 读 (已 Decode, 不再 Decode, 不 consume ring)
    const auto& d = parent_->du()->decoded();
    // 继承字段透传 (逐字段, 镜像 DecodeUnitTLM 风格, 避免 slice)
    issued_.instr_desc = d.instr_desc;
    issued_.pc = d.pc;
    issued_.pipe = d.pipe;
    issued_.latency_class = d.latency_class;
    // Round-robin warp 调度 (per Oracle Q3/Q8)
    last_issued_warp_id_ = (last_issued_warp_id_ + 1) % num_warps_;
    issued_.warp_id = last_issued_warp_id_;
    issued_valid_ = true;
}

} // namespace tlm::sm
