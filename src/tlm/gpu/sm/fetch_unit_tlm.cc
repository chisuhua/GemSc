// src/tlm/gpu/sm/fetch_unit_tlm.cc
// FetchUnitTLM: tick() 真值实现 (Task 2.2 P1-2, per Oracle 预审 Task 2.2)
//
// 真值路径:
//   - 从 SM 顶层 ring buffer 取下一条 (parent_->fetch_next_instr() 封装)
//   - 写入 FetchToIssueBundle (instr_desc + warp_id + pc)
//   - 设置 fetched_valid_ = true (空 ring 时 false)
//
// 数据流:
//   ring buffer → InstrDescriptor → FetchToIssueBundle → (next pipeline stage)
//
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18b P1-2 真值)
#include "tlm/gpu/sm/fetch_unit_tlm.hh"
#include "tlm/gpu/streaming_multiprocessor_tlm.hh"

namespace tlm::sm {

    FetchUnitTLM::FetchUnitTLM(const std::string& n, EventQueue* eq)
        : ChStreamModuleBase(n, eq), parent_(nullptr) {
    }
    // parent_ 由 SM 构造函数 make_unique 后调 set_parent(this) 注入
    // (per Oracle 预审 Task 2.2 F-2 P0 + module_factory 2 参数 lambda 兼容)

    void FetchUnitTLM::tick() {
        if (!parent_) {
            fetched_valid_ = false;
            return;
        }
        // 从 SM 顶层 ring buffer 取下一条 (封装 access, instr_ring_ 保持 private)
        cpptlm::gpu::InstrDescriptor d{};
        fetched_valid_ = parent_->fetch_next_instr(d);
        if (fetched_valid_) {
            fetched_.instr_desc = d;
            fetched_.warp_id =
                d.warpid; // InstrDescriptor.warpid (u8) → FetchToIssueBundle.warp_id (u32)
            fetched_.pc = d.pc;
        }
    }

} // namespace tlm::sm
