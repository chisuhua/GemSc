// include/tlm/gpu/sm/vector_alu_tlm.hh
// VectorALU: 向量 ALU 子模块 stub (per SM 微架构 §15.3.3.4) - Task 2.6 端口接线
//
// 功能:
//   - tick() 从 FetchUnitTLM.fetched() 读 InstrDescriptor (per Oracle Q12 A 策略)
//   - 判 pipe == kVectorALU → 调 parent_->vector_alu()->execute() (cpptlm::gpu::VectorALU 真值)
//   - 完成后 parent_->mark_completed() 标 instr_id 已完成 (G8 配套)
//   - pipe 判断只驻留 va_ tick() (exe_once 不保留, 避免双 dispatch, Task 2.2 F-1 教训)
//
// 架构定位:
//   - tlm::sm::VectorALU stub (12 子模块之一, Task 2.1 拆分后) - 端口接线节点
//   - cpptlm::gpu::VectorALU 真值类 (Task 2.6, 镜像 cpptlm::gpu::ScalarALU) - VIADD.U8x4 真值
//   - 关系: va_ tick() 内部 dispatch 到 vector_alu_ (真值), 类似 sa_ tick() → scalar_alu_ 模式
//
// 跨仓契约 (per HSK-9 §3):
//   - SM.exe_once() 调 fu_->tick() → du_->tick() → iu_->tick() → sa_->tick() → va_->tick() → return 1
//   - va_->tick() 内部 pipe 判断 + dispatch (exec 源保持 fu_->fetched().instr_desc 一致)
//
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18b P1-6 端口接线, per Oracle 预审 Task 2.6)
#ifndef TLM_GPU_SM_VECTOR_ALU_TLM_HH
#define TLM_GPU_SM_VECTOR_ALU_TLM_HH

#include "core/chstream_module.hh"
#include "framework/stream_adapter.hh"
#include <string>

namespace tlm {

class StreamingMultiprocessorTLM;  // 前向声明 (避免循环 include)

namespace sm {

class VectorALU : public ChStreamModuleBase {
public:
    // 构造 2 参数 (per module_factory 2 参数 lambda, chstream_register.hh:74)
    VectorALU(const std::string& n, EventQueue* eq);
    std::string get_module_type() const override { return "VectorALU"; }
    void set_stream_adapter(cpptlm::StreamAdapterBase* a) override { (void)a; }
    void tick() override;  // Task 2.6 真值: dispatch 到 cpptlm::gpu::VectorALU

    // 设置 parent 指针 (per Oracle 预审 Task 2.6 F-2 P0 修复,
    // 镜像 FetchUnitTLM/DecodeUnitTLM/IssueUnitTLM/ScalarALU set_parent 模式)
    void set_parent(StreamingMultiprocessorTLM* parent) { parent_ = parent; }

private:
    StreamingMultiprocessorTLM* parent_ = nullptr;
};

} // namespace sm
} // namespace tlm

#endif // TLM_GPU_SM_VECTOR_ALU_TLM_HH
