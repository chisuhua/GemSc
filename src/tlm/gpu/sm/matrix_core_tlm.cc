// src/tlm/gpu/sm/matrix_core_tlm.cc
// MatrixCore: tick() stub 接线 (Task 2.7 P1-7, per Oracle 预审 Task 2.7)
//
// stub tick() 行为 (per Oracle Q2 A):
//   - 模拟 sa_/va_ 安全检查 (parent + fu + fu->has_fetched)
//   - 判 pipe == kMatrixCore → parent_->matrix_alu()->execute() (stub)
//   - **不调 mark_completed** (stub 阶段避免假完成, mark_completed 推迟 Task 4.6)
//
// 数据流 (pipeline 第 6 步, 镜像 sa_/va_ 模式):
//   ring buffer → Fetch → Decode → Issue → ScalarALU → VectorALU → MatrixCore → RegFile
//
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18b P1-7 stub 接线)
#include "tlm/gpu/sm/matrix_core_tlm.hh"
#include "tlm/gpu/streaming_multiprocessor_tlm.hh"

namespace tlm::sm {

    MatrixCore::MatrixCore(const std::string& n, EventQueue* eq)
        : ChStreamModuleBase(n, eq), parent_(nullptr) {
    }
    // parent_ 由 SM 构造函数 make_unique 后调 set_parent(this) 注入

    void MatrixCore::tick() {
        // 安全检查: parent + fu + fu->has_fetched (镜像 Fetch/Decode/Issue/ScalarALU/VectorALU
        // 模式)
        if (!parent_ || !parent_->fu() || !parent_->fu()->has_fetched()) {
            return;
        }
        // 指令源: 与 sa_/va_ tick 完全一致 (parent_->fu()->fetched().instr_desc, 零行为变化)
        auto d = parent_->fu()->fetched().instr_desc;
        // pipe 判断只驻留 mc_ tick() (Oracle Q3 禁令: exe_once 不得保留避免双 dispatch)
        if (d.pipe == cpptlm::gpu::PipeClass::kMatrixCore) {
            // 调 cpptlm::gpu::MatrixCore stub (Task 4.6 MFMA 真值替换实现)
            parent_->matrix_alu()->execute(d);
            // **不调 mark_completed** (per Oracle Q2 A: stub 阶段避免假完成, mark_completed 推迟
            // Task 4.6)
        }
    }

} // namespace tlm::sm
