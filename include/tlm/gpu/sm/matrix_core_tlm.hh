// include/tlm/gpu/sm/matrix_core_tlm.hh
// MatrixCore: 矩阵核心子模块 stub (per SM 微架构 §15.3.3.4) - Task 2.7 端口接线
//
// 功能:
//   - tick() 从 FetchUnitTLM.fetched() 读 InstrDescriptor (per Oracle Q3 C 策略)
//   - 判 pipe == kMatrixCore → 调 parent_->matrix_alu()->execute() (cpptlm::gpu::MatrixCore stub)
//   - **不调 mark_completed** (per Oracle Q2 A: stub 阶段避免假完成, mark_completed 随真值同批 Task 4.6)
//   - pipe 判断只驻留 mc_ tick() (Oracle Q3 禁令: exe_once 不得保留避免双 dispatch)
//
// 架构定位:
//   - tlm::sm::MatrixCore stub (12 子模块之一, Task 2.1 拆分后) - 端口接线节点
//   - cpptlm::gpu::MatrixCore 真值类 (Task 2.7 stub, Task 4.6 填充 MFMA) - dispatch 目标
//   - 关系: mc_ tick() 内部 dispatch 到 matrix_alu_ (真值类), 类似 sa_/va_ 模式
//
// 跨仓契约 (per HSK-9 §3):
//   - SM.exe_once() 调 fu_->tick() → du_->tick() → iu_->tick() → sa_->tick() → va_->tick() → mc_->tick()
//   - mc_->tick() 内部 pipe 判断 + dispatch (exec 源保持 fu_->fetched().instr_desc 一致)
//
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18b P1-7 端口接线 stub, per Oracle 预审 Task 2.7)
#ifndef TLM_GPU_SM_MATRIX_CORE_TLM_HH
#define TLM_GPU_SM_MATRIX_CORE_TLM_HH

#include "core/chstream_module.hh"
#include "framework/stream_adapter.hh"
#include <string>

namespace tlm {

class StreamingMultiprocessorTLM;  // 前向声明 (避免循环 include)

namespace sm {

class MatrixCore : public ChStreamModuleBase {
public:
    // 构造 2 参数 (per module_factory 2 参数 lambda, chstream_register.hh:75)
    MatrixCore(const std::string& n, EventQueue* eq);
    std::string get_module_type() const override { return "MatrixCore"; }
    void set_stream_adapter(cpptlm::StreamAdapterBase* a) override { (void)a; }
    void tick() override;  // Task 2.7 stub 接线: dispatch 到 cpptlm::gpu::MatrixCore stub

    // 设置 parent 指针 (per Oracle 预审 Task 2.7 F-2 P0 修复,
    // 镜像 Fetch/Decode/Issue/ScalarALU/VectorALU set_parent 模式)
    void set_parent(StreamingMultiprocessorTLM* parent) { parent_ = parent; }

private:
    StreamingMultiprocessorTLM* parent_ = nullptr;
};

} // namespace sm
} // namespace tlm

#endif // TLM_GPU_SM_MATRIX_CORE_TLM_HH
