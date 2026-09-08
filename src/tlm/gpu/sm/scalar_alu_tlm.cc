// src/tlm/gpu/sm/scalar_alu_tlm.cc
// ScalarALU: tick() 真值实现 (Task 2.5 P1-5 端口接线, per Oracle 预审 Task 2.5)
//
// 真值路径 (per Oracle Q12):
//   - 从 parent_->fu()->fetched().instr_desc 读 (源与旧 exe_once 完全一致, 零行为变化)
//   - 判 pipe == kScalarALU → parent_->scalar_alu()->execute() + parent_->mark_completed()
//   - pipe 判断只驻留 sa_ tick() (避免双 dispatch, Task 2.2 F-1 教训)
//
// 数据流:
//   ring buffer → Fetch → Decode → Issue → **ScalarALU (sa_)** → ScalarALU 真值类 (scalar_alu_)
//
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18b P1-5 端口接线)
#include "tlm/gpu/sm/scalar_alu_tlm.hh"
#include "tlm/gpu/streaming_multiprocessor_tlm.hh"

namespace tlm::sm {

    ScalarALU::ScalarALU(const std::string& n, EventQueue* eq)
        : ChStreamModuleBase(n, eq), parent_(nullptr) {
    }
    // parent_ 由 SM 构造函数 make_unique 后调 set_parent(this) 注入

    void ScalarALU::tick() {
        // 安全检查: parent + fu + fu->has_fetched (镜像 Fetch/Decode/Issue 模式)
        if (!parent_ || !parent_->fu() || !parent_->fu()->has_fetched()) {
            return;
        }
        // 指令源: 与旧 exe_once 完全一致 (parent_->fu()->fetched().instr_desc, 零行为变化)
        auto d = parent_->fu()->fetched().instr_desc;
        // pipe 判断只驻留 sa_ tick() (Oracle Q7 禁令: exe_once 不得保留)
        if (d.pipe == cpptlm::gpu::PipeClass::kScalarALU) {
            // 调 cpptlm::gpu::ScalarALU 真值类 (Task 1.3 真值: ADD/IMAD)
            parent_->scalar_alu()->execute(d);
            // G8 配套: 完成后标记 instr_id 已完成 (per Oracle Q12)
            parent_->mark_completed(d.instr_id);
        }
    }

} // namespace tlm::sm
