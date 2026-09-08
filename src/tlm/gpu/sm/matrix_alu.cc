// src/tlm/gpu/sm/matrix_alu.cc
// MatrixCore: execute() stub 实现 (Task 2.7 P1-7 stub, per Oracle 预审 Task 2.7)
//
// Task 2.7 stub 行为 (per Oracle Q2 A):
//   - 判 pipe == kMatrixCore → return 0 (纯静默)
//   - 不写 dst (真值推迟 Task 4.6)
//   - tick() 不调 mark_completed (避免假完成, mark_completed 随真值同批)
//   - Task 4.6 填充 MFMA 4x4/16x16/32x32 真值
//
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18b P1-7 stub)
#include "tlm/gpu/sm/matrix_alu.hh"
#include "tlm/gpu/streaming_multiprocessor_tlm.hh"

namespace cpptlm {
    namespace gpu {

        uint32_t MatrixCore::execute(InstrDescriptor& desc) {
            // Task 2.7 stub: 判 pipe == kMatrixCore → return 0 (Task 4.6 MFMA 真值替换)
            if (!parent_)
                return 0;
            if (desc.pipe != PipeClass::kMatrixCore)
                return 0;
            return 0;
        }

    } // namespace gpu
} // namespace cpptlm
