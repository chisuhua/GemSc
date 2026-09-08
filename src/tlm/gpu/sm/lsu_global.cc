// src/tlm/gpu/sm/lsu_global.cc
// LsuGlobal: execute() + tick() 真值实现 (Task 2.9 P1-9, per Oracle 预审 Task 2.9)
//
// execute() (异步 enqueue):
//   - guard: !parent_ / pipe != kLsuGlobal / !is_memory / num_dst < 1 → return 0
//   - enqueue PendingRequest (快照 instr_id/dst_reg/data/vaddr, cycles_remaining=latency)
//   - return latency_cycles_ (立即返回, 完成在 N cycle 后)
//
// tick() (异步推进, per Oracle Q5 P0 修正: exe_once head 无条件调):
//   - pending_ 空 → return (无 pending)
//   - front().cycles_remaining 递减
//   - 归零: set_scalar_reg(dst_reg, data) + mark_completed(instr_id) + pop_front
//
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18b P1-9 真值)
#include "tlm/gpu/sm/lsu_global.hh"
#include "tlm/gpu/streaming_multiprocessor_tlm.hh"

namespace cpptlm {
    namespace gpu {

        uint32_t LsuGlobal::execute(InstrDescriptor& desc) {
            if (!parent_)
                return 0;
            if (desc.pipe != PipeClass::kLsuGlobal)
                return 0;
            if (!desc.is_memory)
                return 0;
            if (desc.num_dst < 1)
                return 0;

            // enqueue 快照 (per Oracle Q4 硬性要求: 回调阶段不再读 fu()->fetched())
            PendingRequest req;
            req.instr_id = desc.instr_id;
            req.dst_reg = desc.dst_regs[0];
            req.data = desc.memory_data;
            req.target_vaddr = desc.target_vaddr;
            req.cycles_remaining = latency_cycles_;
            pending_.push_back(req);
            return latency_cycles_;
        }

        void LsuGlobal::tick() {
            if (pending_.empty())
                return;
            auto& front = pending_.front();
            if (--front.cycles_remaining == 0) {
                // 归零回调: 写 scalar_regs_ + mark_completed (per Oracle Q4)
                parent_->set_scalar_reg(front.dst_reg, front.data);
                parent_->mark_completed(front.instr_id);
                pending_.pop_front();
            }
        }

    } // namespace gpu
} // namespace cpptlm
