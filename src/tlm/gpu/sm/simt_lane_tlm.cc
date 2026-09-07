// src/tlm/gpu/sm/simt_lane_tlm.cc
// SIMTLane: tick() 接线 (Task 2.8 P1-8, per Oracle 预审 Task 2.8)
//
// stub tick() 行为 (per Oracle Q4):
//   - 模拟 sa_/va_/mc_ 安全检查 (parent + fu + fu->has_fetched)
//   - 判 pipe == kSIMTLane → parent_->simt_lane()->execute() (EXEC mask 更新)
//   - **不调 mark_completed** (mask 更新非指令完成, 推迟 Task 4.6)
//
// 数据流 (pipeline 第 7 步, 镜像 sa_/va_/mc_ 模式):
//   ring buffer → Fetch → Decode → Issue → ScalarALU → VectorALU → MatrixCore → SIMTLane → RegFile
//
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18b P1-8 端口接线)
#include "tlm/gpu/sm/simt_lane_tlm.hh"
#include "tlm/gpu/streaming_multiprocessor_tlm.hh"

namespace tlm::sm {

// 构造 body (per Oracle P0 P1 修复: stub 之前无 parent_/set_parent, 显式 ctor 加 parent_(nullptr))
SIMTLane::SIMTLane(const std::string& n, EventQueue* eq)
    : ChStreamModuleBase(n, eq), parent_(nullptr) {}
    // parent_ 由 SM 构造函数 make_unique 后调 set_parent(this) 注入

void SIMTLane::tick() {
    // 安全检查: parent + fu + fu->has_fetched (镜像 Fetch/Decode/Issue/ScalarALU/VectorALU/MatrixCore 模式)
    if (!parent_ || !parent_->fu() || !parent_->fu()->has_fetched()) {
        return;
    }
    // 指令源: 与 sa_/va_/mc_ tick 完全一致 (parent_->fu()->fetched().instr_desc, 零行为变化)
    auto d = parent_->fu()->fetched().instr_desc;
    // pipe 判断只驻留 sl_ tick() (Oracle Q3 禁令: exe_once 不得保留避免双 dispatch)
    if (d.pipe == cpptlm::gpu::PipeClass::kSIMTLane) {
        // 调 cpptlm::gpu::SIMTLane 真值类 (Task 2.8 真值: EXEC mask 更新 + 分歧检测)
        parent_->simt_lane()->execute(d);
        // **不调 mark_completed** (mask 更新非指令完成, 推迟 Task 4.6 与真值同批)
    }
}

} // namespace tlm::sm
