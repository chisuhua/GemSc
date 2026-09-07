// include/tlm/gpu/sm/lsu_lds.hh
// LsuLDS: 共享内存 LSU 真值类 (per SM 微架构 §15.3.3.6) - Task 2.10 bank conflict stub
//
// 功能 (per plan line 791 '共享内存 bank conflict 检测 stub' + Oracle Q1 C 推荐):
//   - execute(InstrDescriptor&): 判 pipe==kLsuLDS + is_memory + num_dst>=1 → 同步回写 + mark_completed
//   - 同步语义 (per Oracle Q2 A): 立即完成, 无 pending queue (对照 LsuGlobal 异步 10 cycle)
//   - bank_conflict_count(): 恒 0 (Task 2.10 stub, Task 4.6 填充)
//   - 镜像 cpptlm::gpu::LsuGlobal 模式 (parent 指针 + leading ::)
//
// 架构定位:
//   - cpptlm::gpu::LsuLDS 真值类 (Task 2.10 stub, bank conflict 真值推迟 Task 4.6)
//   - tlm::sm::LsuLDS stub (12 子模块之一) 内部 tick() dispatch
//   - LDS 同步 vs LsuGlobal 异步对照 (两种 LSU 模式)
//
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18b P1-10 stub, per Oracle 预审 Task 2.10)
#ifndef TLM_GPU_SM_LSU_LDS_HH
#define TLM_GPU_SM_LSU_LDS_HH

#include "tlm/gpu/instruction_descriptor.hh"
#include <cstdint>

namespace tlm {

class StreamingMultiprocessorTLM;  // 前向声明 (避免循环 include)

}

namespace cpptlm {
namespace gpu {

class LsuLDS {
public:
    explicit LsuLDS(::tlm::StreamingMultiprocessorTLM* parent) : parent_(parent) {}

    // execute: 同步完成入口, 返回 cycles (stub 固定 1)
    //   - kLsuLDS + is_memory + num_dst>=1: set_scalar_reg + mark_completed
    //   - 其他: return 0 (不支持)
    uint32_t execute(InstrDescriptor& desc);

    // bank conflict stub (Task 4.6 填充真实检测)
    uint32_t bank_conflict_count() const { return 0; }

private:
    ::tlm::StreamingMultiprocessorTLM* parent_ = nullptr;
};

} // namespace gpu
} // namespace cpptlm

#endif // TLM_GPU_SM_LSU_LDS_HH
