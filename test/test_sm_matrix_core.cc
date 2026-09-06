// test/test_sm_matrix_core.cc
// Test: MatrixCore MFMA stub 接线 + 行为测试 (per plan Task 2.7, 真值推迟 Task 4.6)
//
// 验证 MatrixCore 子模块 stub + 接线 (真值推迟 Task 4.6 CDNA ISA 阶段):
//   A1: 模块身份 (mc()/matrix_alu() accessor + 类型正确)
//   A2: kMatrixCore 注入安全 (dst reg 不被改 + ring_count 推进, 真值推迟)
//   A3: pipe 互斥 (非 kMatrixCore 指令 mc_ tick 静默不误改 dst)
//   A4: stub 不标 completed (instr_id 仍 pending, 真值 Task 4.6)
//
// 关键路径 (per Oracle 预审 Task 2.7 APPROVE-WITH-FIXES):
//   - mc_ 必须 make_unique (已, Task 2.1 SM.cc) + set_parent(this) (P0 修复)
//   - matrix_alu_ 必须 make_unique (新, cpptlm::gpu::MatrixCore 真值类, 镜像 VectorALU)
//   - matrix_alu_tlm.stub 需补 parent_ + set_parent 声明 (P1 修复, 镜像 VectorALU stub)
//   - SM.exe_once() 加 mc_->tick() 在 va_->tick() 之后 (P3 C 推荐)
//   - stub 行为: 不写 dst, 不标 completed, 不崩 (真值推迟 Task 4.6)
//
// 作者 CppTLM Team / 2027-02-10 (Task 18b P1-7 实施, per Oracle 预审 Task 2.7)
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "tlm/gpu/streaming_multiprocessor_tlm.hh"

using namespace tlm;
using namespace cpptlm::gpu;

TEST_CASE("MatrixCore MFMA stub: 模块身份 + 真值推迟 Task 4.6",
          "[sm][matrix][sm-microarch][task18]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    DeviceConfig cfg{};
    REQUIRE(sm.initialize(cfg));

    // A1: 模块身份 (mc_ + matrix_alu_ accessor + 类型正确, per Oracle Q7 A1)
    REQUIRE(sm.mc() != nullptr);
    REQUIRE(sm.mc()->get_module_type() == "MatrixCore");
    REQUIRE(sm.matrix_alu() != nullptr);  // cpptlm::gpu::MatrixCore 真值类 (stub)
}

TEST_CASE("MatrixCore MFMA stub: kMatrixCore 注入安全 + stub 不标 completed",
          "[sm][matrix][sm-microarch][task18]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    DeviceConfig cfg{};
    REQUIRE(sm.initialize(cfg));

    // 预置 dst reg 30 sentinel (per Oracle Q2 A: stub 不写 dst)
    sm.set_scalar_reg(30, 0xDEADBEEFULL);

    // 注入 kMatrixCore desc (per PipeClass::kMatrixCore = 2, instr_descriptor.hh:31)
    InstrDescriptor desc{};
    desc.instr_id = 300;
    desc.pipe = PipeClass::kMatrixCore;
    desc.latency_class = LatencyClass::kFixed32Cycle;  // MFMA 典型 32 cycle
    desc.dst_regs[0] = 30;
    desc.src_regs[0] = 1;
    desc.src_regs[1] = 2;
    desc.num_src = 2;
    desc.num_dst = 1;
    sm.set_instr_descriptor_buf(&desc, 1);
    REQUIRE(sm.ring_count() == 1);

    // 推进 4 cycle (fu_ tick consume + 其他 ALU tick 静默 + mc_ tick 静默)
    for (int i = 0; i < 4; ++i) {
        sm.exe_once();
    }

    // A2: dst reg 30 不变 (stub 不写 dst, 真值推迟 Task 4.6)
    uint64_t val = 0;
    REQUIRE(sm.get_register_value(0, 0, 30, &val));
    REQUIRE(val == 0xDEADBEEFULL);

    // A4: stub 不标 completed (instr_id 300 仍 pending, 避免假完成)
    // per Oracle Q2 A 推荐: 标 completed = 假完成, mark_completed 随真值同批 (Task 4.6)
    REQUIRE_FALSE(sm.is_instruction_completed(300));
}

TEST_CASE("MatrixCore MFMA stub: pipe 互斥 (非 kMatrixCore 不误改 dst)",
          "[sm][matrix][sm-microarch][task18]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    DeviceConfig cfg{};
    REQUIRE(sm.initialize(cfg));

    sm.set_scalar_reg(30, 0xDEADBEEFULL);

    // 注入 kScalarALU desc (非 kMatrixCore, 应走 sa_ tick, mc_ tick 静默)
    InstrDescriptor desc{};
    desc.instr_id = 400;
    desc.pipe = PipeClass::kScalarALU;
    desc.latency_class = LatencyClass::kFixed1Cycle;
    desc.dst_regs[0] = 5;
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

    // sa_ tick 应写 reg 5 = 300 (ADD 真值, Task 1.3)
    uint64_t val5 = 0;
    REQUIRE(sm.get_register_value(0, 0, 5, &val5));
    REQUIRE(val5 == 300);

    // A3: mc_ 不应误改 reg 30 (pipe 互斥, kScalarALU 不触发 MatrixCore)
    uint64_t val30 = 0;
    REQUIRE(sm.get_register_value(0, 0, 30, &val30));
    REQUIRE(val30 == 0xDEADBEEFULL);
}
