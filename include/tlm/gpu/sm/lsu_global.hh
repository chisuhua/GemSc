// include/tlm/gpu/sm/lsu_global.hh
// LsuGlobal: 全局内存 LSU 真值类 (per SM 微架构 §15.3.3.5) - Task 2.9 异步内存回调骨架
//
// 功能 (per plan line 790 '异步内存回调骨架' + Oracle Q1 C 推荐):
//   - execute(InstrDescriptor&): 判 pipe==kLsuGlobal + is_memory + num_dst>=1 → enqueue PendingRequest
//   - tick(): 推进 pending_ 队列 counter, 归零时回调 (set_scalar_reg + mark_completed)
//   - 异步语义: execute 立即返回 (1 cycle enqueue), 完成在 N cycle 后 (latency_cycles_=10)
//   - 快照语义 (per Oracle Q4 硬性要求): enqueue 时快照 instr_id/dst_reg/data, 回调阶段不再读 fetched()
//   - 镜像 cpptlm::gpu::ScalarALU/VectorALU/MatrixCore/SIMTLane 模式
//
// 架构定位:
//   - cpptlm::gpu::LsuGlobal 真值类 (Task 2.9 异步骨架, 完整内存模型推迟 Task 4.6)
//   - 构造 (::tlm::StreamingMultiprocessorTLM* parent) - 持 SM 顶层指针 (leading ::)
//   - tlm::sm::LsuGlobal stub (12 子模块之一) 内部 tick() dispatch
//   - SM.exe_once() 关键 P0 修正: lsu_global_->tick() 无条件在 head (异步不依赖 ring)
//
// 数据流 (pipeline 第 8 步, 镜像 sa_/va_/mc_/sl_ 模式):
//   ring buffer → Fetch → Decode → Issue → ...ALU... → LSU Global (enqueue) → [异步 N cycle] → 回写 RegFile
//
// latency 约定 (per Oracle Q3 B):
//   - 固定 latency_cycles_ = 10 (单一可调常量, per-instruction latency 推迟 Task 4.6)
//   - enqueue 当 cycle: pending_.push_back(remaining=10); 每次后续 tick() 递减
//   - 归零 (remaining==0): set_scalar_reg(dst_reg, data) + mark_completed(instr_id) + pop
//
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18b P1-9 真值, per Oracle 预审 Task 2.9)
#ifndef TLM_GPU_SM_LSU_GLOBAL_HH
#define TLM_GPU_SM_LSU_GLOBAL_HH

#include "tlm/gpu/instruction_descriptor.hh"
#include <cstdint>
#include <deque>

namespace tlm {

class StreamingMultiprocessorTLM;  // 前向声明 (避免循环 include)

}

namespace cpptlm {
namespace gpu {

class LsuGlobal {
public:
    // leading :: 强制全局 tlm:: namespace, 避免 cpptlm::tlm:: 隐式嵌套冲突
    explicit LsuGlobal(::tlm::StreamingMultiprocessorTLM* parent) : parent_(parent) {}

    // execute: 异步 enqueue 入口, 返回 latency cycles (镜像 ScalarALU/SIMTLane execute 模式)
    //   - kLsuGlobal + is_memory + num_dst>=1: enqueue PendingRequest
    //   - 其他: return 0 (不支持)
    uint32_t execute(InstrDescriptor& desc);

    // tick: 异步推进 pending 队列 (per Oracle Q1 C: 归零回调)
    //   - 每次调用递减 front().cycles_remaining
    //   - 归零时: parent_->set_scalar_reg(dst_reg, data) + parent_->mark_completed(instr_id) + pop_front
    //   - SM.exe_once() head 无条件调 (per Oracle Q5 P0 修正, 异步不依赖 ring)
    void tick();

    // pending 队列大小 (per Oracle Q8 A3 pipe 互斥测试用)
    size_t pending_count() const { return pending_.size(); }

    // latency 配置 (默认 10, 测试可调)
    void set_latency_cycles(uint32_t n) { latency_cycles_ = n; }
    uint32_t latency_cycles() const { return latency_cycles_; }

private:
    // PendingRequest 快照结构 (per Oracle Q4 硬性要求: enqueue 时快照, 回调不读 fetched())
    struct PendingRequest {
        uint64_t instr_id = 0;
        uint32_t dst_reg = 0;
        uint64_t data = 0;
        uint64_t target_vaddr = 0;  // Task 2.9 stub 暂不寻址, Task 4.6 完整内存模型用
        uint32_t cycles_remaining = 0;
    };

    std::deque<PendingRequest> pending_;
    ::tlm::StreamingMultiprocessorTLM* parent_ = nullptr;
    uint32_t latency_cycles_ = 10;  // per Oracle Q3 B 固定 (可测试调整)
};

} // namespace gpu
} // namespace cpptlm

#endif // TLM_GPU_SM_LSU_GLOBAL_HH
