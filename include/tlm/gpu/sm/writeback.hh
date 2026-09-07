// include/tlm/gpu/sm/writeback.hh
// WritebackUnit: 写回单元真值类 (per SM 微架构 §15.3.3.10)
//
// 功能 (per plan line 793 + Oracle F-2 P0 修复, re-scoped to rf_-only):
//   - enqueue(InstrDescriptor&): 入队 in-flight 队列, 记录 result_value + dst_regs + warpid
//   - tick(current_cycle): 遍历队列, finish_cycle ≤ current_cycle → 写回 RegFileUnit
//   - pending_count(): 返回 in-flight 队列大小
//   - 回写循环用 result_num (per Oracle F-1 P1, 不是固定 4)
//   - warp 来源是 desc.warpid (per Oracle F-2 P1, per-warp 写回)
//   - **无 HT-release** (per Oracle F-2 re-scope, 推迟 Task 2.13)
//
// 架构定位:
//   - cpptlm::gpu::WritebackUnit 真值类 (Task 2.12)
//   - tlm::sm::WritebackUnit stub (12 子模块之一) 内部 tick() dispatch
//
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18b P1-12 真值, per Oracle 预审 Task 2.12)
#ifndef TLM_GPU_SM_WRITEBACK_HH
#define TLM_GPU_SM_WRITEBACK_HH

#include "tlm/gpu/instruction_descriptor.hh"
#include <cstdint>
#include <deque>
#include <string>

namespace tlm {

class StreamingMultiprocessorTLM;  // 前向声明

}

namespace cpptlm {
namespace gpu {

class WritebackUnit {
public:
    explicit WritebackUnit(::tlm::StreamingMultiprocessorTLM* parent) : parent_(parent) {}

    // enqueue: 入队 in-flight (保留 desc 浅拷贝)
    void enqueue(const InstrDescriptor& desc, uint64_t finish_cycle);

    // tick: 推进队列, finish_cycle ≤ current_cycle 的条目写回 RegFileUnit 并出队
    void tick(uint64_t current_cycle);

    // pending_count: in-flight 队列大小 (测试用)
    size_t pending_count() const { return queue_.size(); }

    // clear: 清空队列 (per initialize())
    void clear() { queue_.clear(); }

private:
    // in-flight entry: desc + finish_cycle
    struct Entry {
        InstrDescriptor desc;
        uint64_t finish_cycle;
    };

    ::tlm::StreamingMultiprocessorTLM* parent_ = nullptr;
    std::deque<Entry> queue_;
};

} // namespace gpu
} // namespace cpptlm

#endif // TLM_GPU_SM_WRITEBACK_HH
