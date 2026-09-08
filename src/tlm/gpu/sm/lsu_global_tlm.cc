// src/tlm/gpu/sm/lsu_global_tlm.cc
// LsuGlobal: tick() 接线 (Task 2.9 P1-9, per Oracle 预审 Task 2.9)
//
// stub tick() 行为 (per Oracle Q5 末尾 dispatch):
//   - 模拟 sa_/va_/mc_/sl_ 安全检查 (parent + fu + fu->has_fetched)
//   - 判 pipe == kLsuGlobal + is_memory → parent_->lsu_global()->execute() (enqueue)
//   - **不调 mark_completed** (异步完成在 lsu_global_ tick() 归零回调)
//
// 数据流 (pipeline 第 8 步, 镜像 sa_/va_/mc_/sl_ 模式):
//   ring buffer → Fetch → Decode → Issue → ...ALU... → LsuGlobal (dispatch enqueue)
//   → [异步 N cycle, lsu_global_ head tick 推进] → 回写 scalar_regs_ + mark_completed
//
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18b P1-9 端口接线)
#include "tlm/gpu/sm/lsu_global_tlm.hh"
#include "tlm/gpu/streaming_multiprocessor_tlm.hh"

namespace tlm::sm {

    LsuGlobal::LsuGlobal(const std::string& n, EventQueue* eq)
        : ChStreamModuleBase(n, eq), parent_(nullptr) {
    }
    // parent_ 由 SM 构造函数 make_unique 后调 set_parent(this) 注入

    void LsuGlobal::tick() {
        // 安全检查: parent + fu + fu->has_fetched (镜像 Fetch/Decode/Issue/... 模式)
        if (!parent_ || !parent_->fu() || !parent_->fu()->has_fetched()) {
            return;
        }
        // 指令源: 与 sa_/va_/mc_/sl_ tick 完全一致 (parent_->fu()->fetched().instr_desc,
        // 零行为变化)
        auto d = parent_->fu()->fetched().instr_desc;
        // pipe 判断只驻留 lg_ tick() (Oracle Q3 禁令: exe_once 不得保留避免双 dispatch)
        if (d.pipe == cpptlm::gpu::PipeClass::kLsuGlobal && d.is_memory) {
            // 调 cpptlm::gpu::LsuGlobal 真值类 (Task 2.9 异步骨架: enqueue, 异步完成在 tick 归零)
            parent_->lsu_global()->execute(d);
            // **不调 mark_completed** (异步完成在 lsu_global_ tick() 归零回调)
        }
    }

} // namespace tlm::sm
