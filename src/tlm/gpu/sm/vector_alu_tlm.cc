// src/tlm/gpu/sm/vector_alu_tlm.cc
// VectorALU: tick() 真值实现 (Task 2.6 P1-6 端口接线, per Oracle 预审 Task 2.6)
//
// 真值路径 (per Oracle Q12):
//   - 从 parent_->fu()->fetched().instr_desc 读 (源与 sa_ tick 完全一致, 零行为变化)
//   - 判 pipe == kVectorALU → parent_->vector_alu()->execute() + parent_->mark_completed()
//   - pipe 判断只驻留 va_ tick() (避免双 dispatch, Task 2.2 F-1 教训)
//
// 数据流:
//   ring buffer → Fetch → Decode → Issue → ScalarALU (sa_) | VectorALU (va_) → RegFile
//
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18b P1-6 端口接线)
#include "tlm/gpu/sm/vector_alu_tlm.hh"
#include "tlm/gpu/streaming_multiprocessor_tlm.hh"

namespace tlm::sm {

    VectorALU::VectorALU(const std::string& n, EventQueue* eq)
        : ChStreamModuleBase(n, eq), parent_(nullptr) {
    }
    // parent_ 由 SM 构造函数 make_unique 后调 set_parent(this) 注入

    void VectorALU::tick() {
        // 安全检查: parent + fu + fu->has_fetched (镜像 Fetch/Decode/Issue/ScalarALU 模式)
        if (!parent_ || !parent_->fu() || !parent_->fu()->has_fetched()) {
            return;
        }
        // 指令源: 与 sa_ tick 完全一致 (parent_->fu()->fetched().instr_desc, 零行为变化)
        auto d = parent_->fu()->fetched().instr_desc;
        // pipe 判断只驻留 va_ tick() (Oracle Q7 禁令: exe_once 不得保留避免双 dispatch)
        if (d.pipe == cpptlm::gpu::PipeClass::kVectorALU) {
            // 调 cpptlm::gpu::VectorALU 真值类 (Task 2.6 真值: VIADD.U8x4)
            parent_->vector_alu()->execute(d);
            // G8 配套: 完成后标记 instr_id 已完成 (per Oracle Q12)
            parent_->mark_completed(d.instr_id);
        }
    }

} // namespace tlm::sm
