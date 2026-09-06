// include/tlm/gpu/sm/matrix_alu.hh
// MatrixCore: 矩阵核心真值类 (per SM 微架构 §15.3.3.4) - Task 2.7 stub
//
// 功能 (Task 2.7 真值推迟):
//   - execute(InstrDescriptor&): kMatrixCore stub (guard + return 0)
//   - 真值实现推迟到 Task 4.6 (CDNA MFMA 4x4/16x16/32x32 真值)
//   - 镜像 cpptlm::gpu::ScalarALU / VectorALU 模式
//
// 架构定位:
//   - cpptlm::gpu::MatrixCore 真值类 (Task 2.7 stub, Task 4.6 填充)
//   - 构造 (::tlm::StreamingMultiprocessorTLM* parent) - 持 SM 顶层指针 (leading ::)
//   - tlm::sm::MatrixCore stub (12 子模块之一) 内部 tick() dispatch
//
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18b P1-7 stub, per Oracle 预审 Task 2.7)
#ifndef TLM_GPU_SM_MATRIX_ALU_HH
#define TLM_GPU_SM_MATRIX_ALU_HH

#include "tlm/gpu/instruction_descriptor.hh"
#include <cstdint>

namespace tlm {

class StreamingMultiprocessorTLM;  // 前向声明 (避免循环 include)

}

namespace cpptlm {
namespace gpu {

class MatrixCore {
public:
    // leading :: 强制全局 tlm:: namespace, 避免 cpptlm::tlm:: 隐式嵌套冲突
    explicit MatrixCore(::tlm::StreamingMultiprocessorTLM* parent) : parent_(parent) {}

    // execute: Task 2.7 stub (guard + return 0, 真值推迟 Task 4.6 MFMA)
    uint32_t execute(InstrDescriptor& desc);

private:
    ::tlm::StreamingMultiprocessorTLM* parent_ = nullptr;
};

} // namespace gpu
} // namespace cpptlm

#endif // TLM_GPU_SM_MATRIX_ALU_HH
