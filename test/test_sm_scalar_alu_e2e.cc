// test/test_sm_scalar_alu_e2e.cc
// Test: ScalarALU ADD 真值端到端 (per plan Task 1.3)
//
// 验证 StreamingMultiprocessorTLM 经 IComputeDevice 接口 (set_instr_descriptor_buf
// + exe_once + get_register_value) 真实计算 ADD 指令: reg 1 + reg 2 → reg 5 = 300
//
// 关键路径:
//   - sm.initialize(cfg)
//   - sm.set_scalar_reg(1, 100), sm.set_scalar_reg(2, 200)  (Task 1.1 interim 真值源)
//   - InstrDescriptor{kScalarALU, kFixed1Cycle, dst=[5], src=[1,2], num_src=2, num_dst=1}
//   - sm.set_instr_descriptor_buf(&desc, 1)  (浅拷贝 internal_buf_)
//   - 推进 4 cycle: sm.exe_once() × 4
//   - sm.get_register_value(0, 0, 5, &val) == 300  (验证寄存器真值)
//
// 作者 CppTLM Team / 2027-02-10 (Task 18 P1-3 实施)
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "tlm/gpu/streaming_multiprocessor_tlm.hh"

using namespace tlm;
using namespace cpptlm::gpu;

TEST_CASE("ScalarALU ADD: reg 1 + reg 2 → reg 5 (round-trip via IComputeDevice)",
          "[sm-alu][sm-microarch][task18]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    DeviceConfig cfg{};
    REQUIRE(sm.initialize(cfg)); // Task 1.3 真值
    sm.set_scalar_reg(1, 100);
    sm.set_scalar_reg(2, 200);

    InstrDescriptor desc{};
    desc.instr_id = 1;
    desc.pipe = PipeClass::kScalarALU;
    desc.latency_class = LatencyClass::kFixed1Cycle;
    desc.dst_regs[0] = 5;
    desc.src_regs[0] = 1; // 寄存器号 (语义统一 per Oracle P0-4)
    desc.src_regs[1] = 2;
    desc.num_src = 2;
    desc.num_dst = 1;
    sm.set_instr_descriptor_buf(&desc, 1); // 浅拷贝 internal_buf_

    // 推进 4 cycle (ADD 1 cycle, 留 buffer 写入 + Result 收集)
    for (int i = 0; i < 4; ++i) {
        sm.exe_once();
    }

    uint64_t val = 0;
    REQUIRE(sm.get_register_value(0, 0, 5, &val));
    REQUIRE(val == 300); // 100 + 200
}

// Per Oracle 复审子波 1 (session ses_f8753c360ffepoeFV044s4tkSs P1-1):
// 补 IMAD 真值 round-trip 测试 (G7 PARTIAL 修复, 验证 Gate G7 名称承诺).
// ScalarALU::execute IMAD (kFixed4Cycle): dst = src[0] * src[1]
//   (per src/tlm/gpu/sm/scalar_alu.cc Task 1.3 简化实现, 无第三操作数 + 0)
TEST_CASE("ScalarALU IMAD: reg 7 × reg 11 → reg 20 (round-trip via IComputeDevice)",
          "[sm-alu][sm-microarch][task18]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    DeviceConfig cfg{};
    REQUIRE(sm.initialize(cfg));
    sm.set_scalar_reg(7, 6);
    sm.set_scalar_reg(11, 7);

    InstrDescriptor desc{};
    desc.instr_id = 2;
    desc.pipe = PipeClass::kScalarALU;
    desc.latency_class = LatencyClass::kFixed4Cycle;
    desc.dst_regs[0] = 20;
    desc.src_regs[0] = 7; // 寄存器号 (语义统一 per Oracle P0-4)
    desc.src_regs[1] = 11;
    desc.num_src = 2;
    desc.num_dst = 1;
    sm.set_instr_descriptor_buf(&desc, 1);

    // 推进 4 cycle (ScalarALU.execute 返回 4 cycles 但 SM.exe_once() 每次只 consume 1 cycle)
    for (int i = 0; i < 4; ++i) {
        sm.exe_once();
    }

    uint64_t val = 0;
    REQUIRE(sm.get_register_value(0, 0, 20, &val));
    REQUIRE(val == 42);

    // G8 配套: ScalarALU.execute() 后 completed_instr_ids_.insert(desc.instr_id)
    REQUIRE(sm.is_instruction_completed(2));
}

// Per Oracle 预审 Task 2.5 (session ses_f8713ec67ffe1swhein66TW2jJ APPROVE-WITH-FIXES):
// 验证 sa_ 作为流水线节点, tick() 内部 dispatch 到 cpptlm::gpu::ScalarALU 真值.
// 端口接线 (per plan line 786): sa_ 接线 SM.exe_once() 流水线 (fu→du→iu→sa).
// 关键路径 (per Oracle Q12):
//   - sm.sa() != nullptr (A1a 模块身份)
//   - sm.sa()->get_module_type() == "ScalarALU" (A1b 模块身份)
//   - SM.exe_once() → sa_->tick() 即时 dispatch (单次 ADD 即时可见, A2 接线真实生效)
//   - 测试不修改 test_sm_scalar_alu_e2e.cc 既有 TEST_CASE (e2e 回归)
TEST_CASE("Task 2.5: sa_ 端口接线 (pipeline node dispatch)", "[sm-alu][sm-microarch][task18]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);

    // A1: 模块身份 (sa_ 已构造 + 类型正确, per Oracle Q12)
    REQUIRE(sm.sa() != nullptr);
    REQUIRE(sm.sa()->get_module_type() == "ScalarALU");

    DeviceConfig cfg{};
    REQUIRE(sm.initialize(cfg));
    sm.set_scalar_reg(1, 100);
    sm.set_scalar_reg(2, 200);

    InstrDescriptor d{};
    d.instr_id = 7;
    d.pipe = PipeClass::kScalarALU;
    d.latency_class = LatencyClass::kFixed1Cycle;
    d.dst_regs[0] = 5;
    d.src_regs[0] = 1;
    d.src_regs[1] = 2;
    d.num_src = 2;
    d.num_dst = 1;
    sm.set_instr_descriptor_buf(&d, 1);

    // A2: 单次 sm.exe_once() → sa_->tick() 即时 dispatch (sa_ 接线真实生效,
    // 非旧 scalar_alu_->execute() direct 路径残留)
    sm.exe_once();

    uint64_t val = 0;
    REQUIRE(sm.get_register_value(0, 0, 5, &val));
    REQUIRE(val == 300); // 100 + 200 (sa_ 内部 dispatch 到 cpptlm::gpu::ScalarALU 真值)
    REQUIRE(sm.is_instruction_completed(7));
}
