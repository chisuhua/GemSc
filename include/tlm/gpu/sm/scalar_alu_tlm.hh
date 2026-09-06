// include/tlm/gpu/sm/scalar_alu_tlm.hh
// ScalarALU: 标量 ALU 子模块 (per SM 微架构 §15.3.3) - Task 2.5 端口接线
//
// 功能:
//   - tick() 从 FetchUnitTLM.fetched() 读 InstrDescriptor (per Oracle Q12 A 策略)
//   - 判 pipe == kScalarALU → 调 parent_->scalar_alu()->execute() (cpptlm::gpu::ScalarALU 真值)
//   - 完成后 parent_->mark_completed() 标 instr_id 已完成 (G8 配套)
//   - pipe 判断只驻留 sa_ tick() (exe_once 不保留, 避免双 dispatch, Task 2.2 F-1 教训)
//
// 架构定位:
//   - tlm::sm::ScalarALU stub (12 子模块之一, Task 2.1 拆分后) - 端口接线节点
//   - cpptlm::gpu::ScalarALU 真值类 (Task 1.3 commit 2676049) - ADD/IMAD 真值
//   - 关系: sa_ tick() 内部 dispatch 到 scalar_alu_ (真值), 类似调度模式 (Fetch→Decode→Issue)
//
// 跨仓契约 (per HSK-9 §3):
//   - SM.exe_once() 调 fu_->tick() → du_->tick() → iu_->tick() → sa_->tick() → return 1
//   - sa_->tick() 内部 pipe 判断 + dispatch (exec 源保持 fu_->fetched().instr_desc 一致)
//
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18b P1-5 端口接线, per Oracle 预审 Task 2.5)
#ifndef TLM_GPU_SM_SCALAR_ALU_TLM_HH
#define TLM_GPU_SM_SCALAR_ALU_TLM_HH

#include "core/chstream_module.hh"
#include "framework/stream_adapter.hh"
#include <string>

namespace tlm {

class StreamingMultiprocessorTLM;  // 前向声明 (避免循环 include)

namespace sm {

class ScalarALU : public ChStreamModuleBase {
public:
    // 构造 2 参数 (per module_factory 2 参数 lambda, chstream_register.hh:73)
    ScalarALU(const std::string& n, EventQueue* eq);
    std::string get_module_type() const override { return "ScalarALU"; }
    void set_stream_adapter(cpptlm::StreamAdapterBase* a) override { (void)a; }
    void tick() override;  // Task 2.5 真值: dispatch 到 cpptlm::gpu::ScalarALU

    // 设置 parent 指针 (per Oracle 预审 Task 2.5 F-2 P0 修复,
    // 镜像 FetchUnitTLM/DecodeUnitTLM/IssueUnitTLM set_parent 模式)
    void set_parent(StreamingMultiprocessorTLM* parent) { parent_ = parent; }

private:
    StreamingMultiprocessorTLM* parent_ = nullptr;
};

} // namespace sm
} // namespace tlm

#endif // TLM_GPU_SM_SCALAR_ALU_TLM_HH
