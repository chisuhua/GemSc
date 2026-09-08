// src/tlm/gpu/sm/writeback.cc
// WritebackUnit 真值类实现 (per SM 微架构 §15.3.3.10, per Oracle F-2 re-scope)
//
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18b P1-12 真值)
#include "tlm/gpu/sm/writeback.hh"
#include "tlm/gpu/streaming_multiprocessor_tlm.hh"

namespace cpptlm {
    namespace gpu {

        void WritebackUnit::enqueue(const InstrDescriptor& desc, uint64_t finish_cycle) {
            queue_.push_back({desc, finish_cycle});
        }

        void WritebackUnit::tick(uint64_t current_cycle) {
            if (!parent_)
                return;
            auto* rf = parent_->reg_file();
            if (!rf)
                return;

            // 遍历 in-flight, 找出 finish_cycle ≤ current_cycle 的条目写回 RF 并出队
            while (!queue_.empty()) {
                auto& front = queue_.front();
                if (front.finish_cycle > current_cycle)
                    break; // 未到时间, 停止 drain

                // 写回: result_value[0..result_num) → dst_regs[0..result_num)
                // (per Oracle F-1 P1: 用 result_num 守卫, 不用固定 4)
                uint8_t n = front.desc.result_num;
                if (n > 4)
                    n = 4; // 防御: 截断到 max
                for (uint8_t i = 0; i < n; ++i) {
                    rf->write(front.desc.warpid, front.desc.dst_regs[i],
                              front.desc.result_value[i]);
                }
                queue_.pop_front();
            }
        }

    } // namespace gpu
} // namespace cpptlm
