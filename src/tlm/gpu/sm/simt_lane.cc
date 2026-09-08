// src/tlm/gpu/sm/simt_lane.cc
// SIMTLane: execute() 真值实现 (Task 2.8 P1-8, per Oracle 预审 Task 2.8)
//
// 真值路径:
//   - 判 pipe == kSIMTLane → 更新 exec_mask_ 从 desc.exec_mask
//   - num_src==0 + num_dst==0 (SIMTLane 不读 src, 不写 dst, 仅维护 warp-level mask 状态)
//   - 返回 1 cycle (镜像 ScalarALU 真值)
//   - 不调 mark_completed (mask 更新非指令完成, 推迟 Task 4.6 与真值同批)
//
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18b P1-8 真值)
#include "tlm/gpu/sm/simt_lane.hh"

namespace cpptlm {
    namespace gpu {

        uint32_t SIMTLane::execute(InstrDescriptor& desc) {
            // SIMTLane guard: pipe 匹配 + num_src/dst == 0 (无寄存器读写, 仅 mask 状态)
            if (desc.pipe != PipeClass::kSIMTLane)
                return 0;
            if (desc.num_src != 0 || desc.num_dst != 0)
                return 0;

            // EXEC mask 真值: 从 desc.exec_mask 写入内部 state
            exec_mask_ = desc.exec_mask;
            return 1;
        }

    } // namespace gpu
} // namespace cpptlm
