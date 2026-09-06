// src/tlm/gpu/sm/vector_alu.cc
// VectorALU: VIADD.U8x4 真值实现 (Task 2.6 P1-6, per Oracle 预审 Task 2.6)
//
// 真值路径 (per Oracle Q12 A'):
//   - 读 src_regs[0]+[1] 从 scalar_regs_ (镜像 ScalarALU, 非 src_values)
//   - 展开 4 个 u8 lane, per-lane ADD (u8 wrap)
//   - 打包写 dst_regs[0]
//
// Lane packing (per Oracle Q12 A1):
//   lane0 = bits 0-7, lane1 = bits 8-15, lane2 = bits 16-23, lane3 = bits 24-31
//
// 作者 CppTLM Team / 日期 2027-02-10 (Task 18b P1-6 真值)
#include "tlm/gpu/sm/vector_alu.hh"
#include "tlm/gpu/streaming_multiprocessor_tlm.hh"

namespace cpptlm {
namespace gpu {

uint32_t VectorALU::execute(InstrDescriptor& desc) {
    if (!parent_) return 0;

    // kVectorALU + 2 src + 1 dst (per Oracle Q12 校验)
    if (desc.pipe != PipeClass::kVectorALU) return 0;
    if (desc.num_src < 2 || desc.num_dst < 1) return 0;

    // 读 src0 + src1 寄存器堆 (per Oracle Q1 修正 A')
    uint64_t src0 = parent_->get_scalar_reg(desc.src_regs[0]);
    uint64_t src1 = parent_->get_scalar_reg(desc.src_regs[1]);

    // 展开 4-lane u8 (per Oracle Q12 lane packing)
    uint8_t lane[4] = {0};
    uint64_t dst_packed = 0;
    for (int i = 0; i < 4; ++i) {
        uint8_t s0 = static_cast<uint8_t>((src0 >> (i * 8)) & 0xff);
        uint8_t s1 = static_cast<uint8_t>((src1 >> (i * 8)) & 0xff);
        // u8 wrap ADD (per PTX VIADD.U8x4)
        uint8_t r = static_cast<uint8_t>(s0 + s1);
        lane[i] = r;
        dst_packed |= (static_cast<uint64_t>(r) << (i * 8));
    }

    // 写回 dst_regs[0] (per Oracle Q12, pipeline 第一 vector 指令只写 1 dst)
    parent_->set_scalar_reg(desc.dst_regs[0], dst_packed);
    desc.result_value[0] = dst_packed;
    return 1;  // 1 cycle (per Oracle Q12 note: latency 未消费)
}

} // namespace gpu
} // namespace cpptlm
