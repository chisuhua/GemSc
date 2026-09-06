// test/test_sm_vector_alu_e2e.cc
// Test: VectorALU VIADD.U8x4 真值端到端 (per plan Task 2.6)
//
// 验证 StreamingMultiprocessorTLM 经 IComputeDevice 接口真实计算向量指令:
//   4-lane packed u8 ADD: dst[0..3] = src0[0..3] + src1[0..3] (u8 wrap)
//
// 关键路径 (per Oracle 预审 Task 2.6 P1 修正):
//   - VectorALU 读 scalar_regs_ (经 src_regs[i], 镜像 ScalarALU, 非 src_values)
//   - IssueToExecBundle.src_values[2] 在 SM 侧无 producer (IssueUnitTLM 不填),
//     故改读寄存器堆 (per Oracle Q1 修正 A')
//   - Lane packing: lane0=最低 8 bit, lane1=bit 8-15, lane2=bit 16-23, lane3=bit 24-31
//
// 断言 (≥2 真实行为, per v2 Metis Top 3):
//   A1: VIADD.U8x4 真值 round-trip (src0=[10,20,30,40] + src1=[1,2,3,4] → dst=[11,22,33,44])
//   A4': Pipe 互斥 (kScalarALU 指令经 exe_once 后 VectorALU 不误写 dst)
//
// 作者 CppTLM Team / 2027-02-10 (Task 18b P1-6 实施, per Oracle 预审 Task 2.6 APPROVE-WITH-FIXES)
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "tlm/gpu/streaming_multiprocessor_tlm.hh"

using namespace tlm;
using namespace cpptlm::gpu;

TEST_CASE("VectorALU VIADD.U8x4: 4-lane packed u8 ADD via IComputeDevice",
          "[sm-vp][sm-microarch][task18]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    DeviceConfig cfg{};
    REQUIRE(sm.initialize(cfg));

    // A1a + A1b: 模块身份 (va_ 已构造 + 类型正确, per Oracle 预审 Task 2.6)
    REQUIRE(sm.va() != nullptr);
    REQUIRE(sm.va()->get_module_type() == "VectorALU");

    // Lane packing: lane0=最低 8 bit, lane1=bit 8-15, lane2=bit 16-23, lane3=bit 24-31
    // src0 = [10,20,30,40]: lane0=0x0a, lane1=0x14, lane2=0x1e, lane3=0x28 → packed 0x281e140a
    // src1 = [1,2,3,4]:   lane0=0x01, lane1=0x02, lane2=0x03, lane3=0x04 → packed 0x04030201
    sm.set_scalar_reg(1, 0x281e140aULL);  // src_regs[0]
    sm.set_scalar_reg(2, 0x04030201ULL);  // src_regs[1]

    InstrDescriptor desc{};
    desc.instr_id = 100;
    desc.pipe = PipeClass::kVectorALU;
    desc.latency_class = LatencyClass::kFixed1Cycle;
    desc.dst_regs[0] = 20;    // dst reg 20
    desc.src_regs[0] = 1;     // src0 reg 1
    desc.src_regs[1] = 2;     // src1 reg 2
    desc.num_src = 2;
    desc.num_dst = 1;
    sm.set_instr_descriptor_buf(&desc, 1);

    // 推进 4 cycle (VectorALU 1 cycle + buffer)
    for (int i = 0; i < 4; ++i) {
        sm.exe_once();
    }

    // A1: 期望 dst=[11,22,33,44] → packed 0x2c21160b
    uint64_t val = 0;
    REQUIRE(sm.get_register_value(0, 0, 20, &val));
    REQUIRE(val == 0x2c21160bULL);
    REQUIRE(sm.is_instruction_completed(100));
}

// Per Oracle 预审 Task 2.6 A4': Pipe 互斥
// 验证 kScalarALU 指令经 exe_once 后 VectorALU 不误写 dst reg (pipe 互斥安全)
TEST_CASE("VectorALU pipe 互斥: kScalarALU 指令 VectorALU 不误写",
          "[sm-vp][sm-microarch][task18]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    DeviceConfig cfg{};
    REQUIRE(sm.initialize(cfg));

    // 预写 dst reg 30 一个特定值 (per-lane 区分)
    sm.set_scalar_reg(30, 0xDEADBEEFULL);

    // 注入 kScalarALU 指令 (走 sa_ dispatch, 不走 va_)
    InstrDescriptor desc{};
    desc.instr_id = 200;
    desc.pipe = PipeClass::kScalarALU;       // 非 kVectorALU
    desc.latency_class = LatencyClass::kFixed1Cycle;
    desc.dst_regs[0] = 5;                    // sa_ 写 reg 5
    desc.src_regs[0] = 1;
    desc.src_regs[1] = 2;
    desc.num_src = 2;
    desc.num_dst = 1;
    sm.set_scalar_reg(1, 100);
    sm.set_scalar_reg(2, 200);
    sm.set_instr_descriptor_buf(&desc, 1);

    for (int i = 0; i < 4; ++i) {
        sm.exe_once();
    }

    // sa_ 应写 reg 5 = 300 (ADD 真值)
    uint64_t val5 = 0;
    REQUIRE(sm.get_register_value(0, 0, 5, &val5));
    REQUIRE(val5 == 300);

    // A4': va_ 不应误写 reg 30 (pipe 互斥, kScalarALU 不触发 VectorALU)
    //     reg 30 保持预写值 0xDEADBEEFULL
    uint64_t val30 = 0;
    REQUIRE(sm.get_register_value(0, 0, 30, &val30));
    REQUIRE(val30 == 0xDEADBEEFULL);  // 不被 VectorALU 误改
}
