// src/tlm/gpu/streaming_multiprocessor_tlm.cc
// StreamingMultiprocessorTLM 实现 (Task 4 stub; Task 18 完整实现 15 方法)
//
// 作者 CppTLM Team / 日期 2027-02-09
// 参考: docs/soc_arch/architecture/15-sm-microarchitecture-design.md §15.2 + §15.5
#include "tlm/gpu/streaming_multiprocessor_tlm.hh"

namespace tlm {

StreamingMultiprocessorTLM::StreamingMultiprocessorTLM(const std::string& name, EventQueue* eq)
    : ChStreamModuleBase(name, eq) {
    // Task 1.3 P1-3: 初始化 ScalarALU 真值 (独立 cpptlm::gpu::ScalarALU)
    // parent_ = this, ScalarALU 通过 parent_->get_scalar_reg / set_scalar_reg
    // 访问 SM 顶层 scalar_regs_ 真值源 (Task 1.1 interim, Task 2.11 迁移到 RegFileUnit)
    scalar_alu_ = std::make_unique<cpptlm::gpu::ScalarALU>(this);
    // Task 2.2 P1-2: 构造 12 子模块 (per Oracle 预审 Task 2.2 F-2 P0 修复)
    // 之前 12 子模块 unique_ptr 从未构造 (P0 blocker), 现在 make_unique 全集
    fu_ = std::make_unique<sm::FetchUnitTLM>(name + ".fu", eq);
    fu_->set_parent(this);  // Task 2.2 P1-2: 注入 parent (per Oracle F-2 P0)
    du_ = std::make_unique<sm::DecodeUnitTLM>(name + ".du", eq);
    du_->set_parent(this);  // Task 2.3 P1-3: 注入 parent (per Oracle F-2 P0 修复, Task 2.2 漏了)
    iu_ = std::make_unique<sm::IssueUnitTLM>(name + ".iu", eq);
    iu_->set_parent(this);  // Task 2.4 P1-4: 注入 parent (per Oracle F-2 P0 修复)
    sa_ = std::make_unique<sm::ScalarALU>(name + ".sa", eq);
    sa_->set_parent(this);  // Task 2.5 P1-5: 注入 parent (per Oracle F-2 P0 修复, 镜像 Task 2.3 du_ 模式)
    // Task 2.6 P1-6: VectorALU 真值类 (镜像 cpptlm::gpu::ScalarALU, 持 SM 顶层 parent)
    vector_alu_ = std::make_unique<cpptlm::gpu::VectorALU>(this);
    va_ = std::make_unique<sm::VectorALU>(name + ".va", eq);
    va_->set_parent(this);  // Task 2.6 P1-6: 注入 parent (per Oracle F-2 P0 修复, 镜像 sa_ 模式)
    // Task 2.7 P1-7: MatrixCore stub 真值类 (镜像 cpptlm::gpu::ScalarALU/VectorALU, 真值推迟 Task 4.6)
    matrix_alu_ = std::make_unique<cpptlm::gpu::MatrixCore>(this);
    mc_ = std::make_unique<sm::MatrixCore>(name + ".mc", eq);
    mc_->set_parent(this);  // Task 2.7 P1-7: 注入 parent (per Oracle F-2 P0 修复, 镜像 sa_/va_ 模式)
    // Task 2.8 P1-8: SIMTLane 真值类 (镜像 cpptlm::gpu::ScalarALU/VectorALU/MatrixCore, EXEC mask + 分歧检测)
    simt_lane_ = std::make_unique<cpptlm::gpu::SIMTLane>(this);
    // Task 2.8 P1-8: SIMTLane 真值类 (镜像 cpptlm::gpu::ScalarALU/VectorALU/MatrixCore, EXEC mask + 分歧检测)
    simt_lane_ = std::make_unique<cpptlm::gpu::SIMTLane>(this);
    sl_ = std::make_unique<sm::SIMTLane>(name + ".sl", eq);
    sl_->set_parent(this);  // Task 2.8 P1-8: 注入 parent (per Oracle F-2 P0 修复, 镜像 sa_/va_/mc_ 模式)
    sl_->set_parent(this);  // Task 2.8 P1-8: 注入 parent (per Oracle F-2 P0 修复, 镜像 sa_/va_/mc_ 模式)
    lg_ = std::make_unique<sm::LsuGlobal>(name + ".lg", eq);
    ll_ = std::make_unique<sm::LsuLDS>(name + ".ll", eq);
    rf_ = std::make_unique<sm::RegFileUnit>(name + ".rf", eq);
    wb_ = std::make_unique<sm::WritebackUnit>(name + ".wb", eq);
    ht_ = std::make_unique<sm::HazardTracker>(name + ".ht", eq);
}

void StreamingMultiprocessorTLM::set_stream_adapter(cpptlm::StreamAdapterBase* adapter) {
    adapter_ = adapter;
}

void StreamingMultiprocessorTLM::tick() {
    // Stub: Task 18 将协调 12 子模块:
    //   fu_->tick(); du_->tick(); iu_->tick(); ... 等
    // 当前为空 (Task 4 占位, 编译通过即可)
}

}  // namespace tlm