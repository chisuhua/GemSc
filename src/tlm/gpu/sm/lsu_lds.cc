// src/tlm/gpu/sm/lsu_lds.cc
// LsuLDS: execute() 同步实现 (Task 2.10 P1-10 stub, per Oracle 预审 Task 2.10)
//
// 同步语义 (per Oracle Q2 A, 对照 LsuGlobal 异步):
//   - 立即 set_scalar_reg(dst_reg, memory_data) + mark_completed(instr_id)
//   - return 1 (固定 1 cycle)
//   - bank conflict 检测推迟 Task 4.6
//
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18b P1-10 stub)
#include "tlm/gpu/sm/lsu_lds.hh"
#include "tlm/gpu/streaming_multiprocessor_tlm.hh"

namespace cpptlm {
namespace gpu {

uint32_t LsuLDS::execute(InstrDescriptor& desc) {
    if (!parent_) return 0;
    if (desc.pipe != PipeClass::kLsuLDS) return 0;
    if (!desc.is_memory) return 0;
    if (desc.num_dst < 1) return 0;

    // 同步完成: 立即回写 + mark_completed (对照 LsuGlobal 异步 10 cycle)
    parent_->set_scalar_reg(desc.dst_regs[0], desc.memory_data);
    parent_->mark_completed(desc.instr_id);
    return 1;
}

} // namespace gpu
} // namespace cpptlm
