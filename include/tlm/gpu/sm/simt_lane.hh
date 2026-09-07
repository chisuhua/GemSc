// include/tlm/gpu/sm/simt_lane.hh
// SIMTLane: SIMT 线程控制真值类 (per SM 微架构 §15.3.3.4) - Task 2.8 真值
//
// 功能 (per plan line 789 'EXEC mask 64-bit + 分歧检测'):
//   - execute(InstrDescriptor&): 判 pipe == kSIMTLane → 更新 exec_mask_ 从 desc.exec_mask
//   - set_active_mask(uint64_t mask): 显式接口 (Task 2.8 测试用)
//   - get_active_mask() const: 返回 exec_mask_
//   - is_divergent() const: 分歧检测 (mask != 0 && mask != all-ones)
//   - 镜像 cpptlm::gpu::ScalarALU/VectorALU/MatrixCore 模式
//
// 架构定位:
//   - cpptlm::gpu::SIMTLane 真值类 (Task 2.8 真值, 独立于 stub)
//   - 构造 (::tlm::StreamingMultiprocessorTLM* parent) - 持 SM 顶层指针 (leading ::)
//   - tlm::sm::SIMTLane stub (12 子模块之一) 内部 tick() dispatch
//
// EXEC mask 初始值 (per Oracle Q5 P1 修正):
//   镜像硬件 reset 状态 + InstrDescriptor.exec_mask 默认值 (instruction_descriptor.hh:88)
//   = 0xFFFFFFFFFFFFFFFFull (all-ones, 全 thread active)
//
// Lane mask vs EXEC mask 二义 (per Oracle Q10 P1):
//   - lane_mask (line 111): per-lane mask (Task 4.6 关注)
//   - exec_mask (line 88): warp-level active mask (Task 2.8 关注)
//   - 本类仅用 desc.exec_mask, lane_mask 留 Task 4.6
//
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18b P1-8 真值, per Oracle 预审 Task 2.8)
#ifndef TLM_GPU_SM_SIMT_LANE_HH
#define TLM_GPU_SM_SIMT_LANE_HH

#include "tlm/gpu/instruction_descriptor.hh"
#include <cstdint>

namespace tlm {

class StreamingMultiprocessorTLM;  // 前向声明 (避免循环 include)

}

namespace cpptlm {
namespace gpu {

class SIMTLane {
public:
    // leading :: 强制全局 tlm:: namespace, 避免 cpptlm::tlm:: 隐式嵌套冲突
    explicit SIMTLane(::tlm::StreamingMultiprocessorTLM* parent) : parent_(parent) {}

    // execute: 真值入口, 返回 cycles (镜像 ScalarALU/VectorALU/MatrixCore)
    //   - kSIMTLane + num_src==0 + num_dst==0: EXEC mask 真值更新
    //   - 其他: return 0 (不支持)
    uint32_t execute(InstrDescriptor& desc);

    // EXEC mask 显式接口 (Task 2.8 测试用)
    void set_active_mask(uint64_t mask) { exec_mask_ = mask; }
    uint64_t get_active_mask() const { return exec_mask_; }

    // 分歧检测 (per Oracle Q3 A+C, split 推迟 Task 4.6)
    // 双极端 uniform (全 0 / 全 all-ones) 都判非分歧
    bool is_divergent() const {
        return exec_mask_ != 0 && exec_mask_ != 0xFFFFFFFFFFFFFFFFull;
    }

private:
    ::tlm::StreamingMultiprocessorTLM* parent_ = nullptr;
    uint64_t exec_mask_ = 0xFFFFFFFFFFFFFFFFull;  // P1 修正: all-ones 初始化 (镜像硬件 reset)
};

} // namespace gpu
} // namespace cpptlm

#endif // TLM_GPU_SM_SIMT_LANE_HH
