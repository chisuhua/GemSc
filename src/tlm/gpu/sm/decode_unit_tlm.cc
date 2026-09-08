// src/tlm/gpu/sm/decode_unit_tlm.cc
// DecodeUnitTLM: tick() 真值实现 (Task 2.3 P1-3, per Oracle 预审 Task 2.3)
//
// 真值路径 (per Oracle Q11):
//   - 检查 parent_ + parent_->fu() + parent_->fu()->has_fetched()
//   - 失败 → decoded_valid_ = false, 安全返回
//   - 成功 → 从 fetched_.instr_desc 提取 pipe + latency_class, 写 decoded_
//
// 数据流:
//   FetchUnitTLM.fetched() → DecodeUnitTLM → DecodeToIssueBundle → IssueUnitTLM
//
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18b P1-3 真值)
#include "tlm/gpu/sm/decode_unit_tlm.hh"
#include "tlm/gpu/streaming_multiprocessor_tlm.hh"

namespace tlm::sm {

    DecodeUnitTLM::DecodeUnitTLM(const std::string& n, EventQueue* eq)
        : ChStreamModuleBase(n, eq), parent_(nullptr) {
    }
    // parent_ 由 SM 构造函数 make_unique 后调 set_parent(this) 注入
    // (per Oracle 预审 Task 2.3 F-2 P0)

    void DecodeUnitTLM::tick() {
        // 安全检查: parent + fu + fu->has_fetched 三段
        if (!parent_ || !parent_->fu() || !parent_->fu()->has_fetched()) {
            decoded_valid_ = false;
            return;
        }
        // 从 FetchUnitTLM.fetched() 读 (已 consume, 不再消费 ring buffer)
        const auto& f = parent_->fu()->fetched();
        // 继承字段透传 (无 slice)
        decoded_.instr_desc = f.instr_desc;
        decoded_.warp_id = f.warp_id;
        decoded_.pc = f.pc;
        // 字段提取 (DecodeUnitTLM 真值核心: 从 InstrDescriptor 提取 pipe + latency_class)
        decoded_.pipe = f.instr_desc.pipe;
        decoded_.latency_class = f.instr_desc.latency_class;
        decoded_valid_ = true;
    }

} // namespace tlm::sm
