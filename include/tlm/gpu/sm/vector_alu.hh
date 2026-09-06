// include/tlm/gpu/sm/vector_alu.hh
// VectorALU: 向量 ALU 真值类 (per SM 微架构 §15.3.3.4) - Task 2.6 真值
//
// 功能:
//   - execute(InstrDescriptor&): 4-lane packed u8 ADD (VIADD.U8x4)
//   - 读 src_regs[0]+[1] 从 parent_->get_scalar_reg (per Oracle Q1 修正 A', 寄存器堆读源)
//   - 展开 4 个 u8 lane + per-lane ADD (u8 wrap) + 打包写 dst_regs[0]
//   - 镜像 cpptlm::gpu::ScalarALU (Task 1.3 真值类, ADD/IMAD 真值)
//
// 架构定位:
//   - cpptlm::gpu::VectorALU 真值类 (Task 2.6 真值, 镜像 cpptlm::gpu::ScalarALU)
//   - 构造 (::tlm::StreamingMultiprocessorTLM* parent) - 持 SM 顶层指针 (leading ::)
//   - tlm::sm::VectorALU stub (12 子模块之一) 内部 tick() dispatch
//
// Lane packing 约定 (per Oracle Q12 A1):
//   lane0 = bits 0-7    (lowest byte)
//   lane1 = bits 8-15, lane2 = bits 16-23, lane3 = bits 24-31
//   例: [10,20,30,40] → 0x281e140a (lane3=40=0x28, lane2=30=0x1e, lane1=20=0x14, lane0=10=0x0a)
//
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18b P1-6 真值, per Oracle 预审 Task 2.6)
#ifndef TLM_GPU_SM_VECTOR_ALU_HH
#define TLM_GPU_SM_VECTOR_ALU_HH

#include "tlm/gpu/instruction_descriptor.hh"
#include <cstdint>

namespace tlm {

class StreamingMultiprocessorTLM;  // 前向声明 (避免循环 include)

}

namespace cpptlm {
namespace gpu {

class VectorALU {
public:
    // leading :: 强制全局 tlm:: namespace, 避免 cpptlm::tlm:: 隐式嵌套冲突
    explicit VectorALU(::tlm::StreamingMultiprocessorTLM* parent) : parent_(parent) {}

    // execute: 真值入口, 返回 cycles (镜像 ScalarALU::execute)
    //   - kVectorALU + num_src>=2 + num_dst>=1: VIADD.U8x4 真值
    //   - 其他: return 0 (不支持)
    uint32_t execute(InstrDescriptor& desc);

private:
    ::tlm::StreamingMultiprocessorTLM* parent_ = nullptr;
};

} // namespace gpu
} // namespace cpptlm

#endif // TLM_GPU_SM_VECTOR_ALU_HH
