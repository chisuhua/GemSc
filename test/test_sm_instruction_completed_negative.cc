// test/test_sm_instruction_completed_negative.cc
// Test: is_instruction_completed 负测试 (P2-2 跟踪项)
//
// 验证 StreamingMultiprocessorTLM::is_instruction_completed 在边界条件下:
//   A1: 未注入任何 instr, 调 is_instruction_completed(任意 id) → false
//   A2: 注入并完成 desc 后, 调未注册的 id → false (隔离)
//   A3: 注入并完成 desc 后, 调已完成的 id → true (确认基线)
//   A4: 多个 instr_id 注入, 部分完成, 查询分别返回正确状态
//   A5: is_instruction_completed(0) (默认 id) 应返回 false
//
// 作者 CppTLM Team / 2026-09-07 (Subwave 3 P2-2 boundary test, per Oracle final carryover)
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "tlm/gpu/streaming_multiprocessor_tlm.hh"

using namespace tlm;
using namespace cpptlm::gpu;

TEST_CASE("is_instruction_completed: 空 ring 时返回 false (A1)",
          "[sm-completed][sm-microarch][task18-p2-2]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    DeviceConfig cfg{};
    REQUIRE(sm.initialize(cfg));

    REQUIRE_FALSE(sm.is_instruction_completed(0));
    REQUIRE_FALSE(sm.is_instruction_completed(1));
    REQUIRE_FALSE(sm.is_instruction_completed(42));
    REQUIRE_FALSE(sm.is_instruction_completed(99));
    REQUIRE_FALSE(sm.is_instruction_completed(UINT64_MAX));
}

TEST_CASE("is_instruction_completed: 已完成 id 与未注册 id 隔离 (A2-A3)",
          "[sm-completed][sm-microarch][task18-p2-2]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    DeviceConfig cfg{};
    REQUIRE(sm.initialize(cfg));

    InstrDescriptor desc{};
    desc.instr_id = 100;
    desc.pipe = PipeClass::kScalarALU;
    desc.latency_class = LatencyClass::kFixed1Cycle;
    desc.src_regs[0] = 1;
    desc.src_regs[1] = 2;
    desc.dst_regs[0] = 5;
    desc.num_src = 2;
    desc.num_dst = 1;
    sm.set_scalar_reg(1, 10);
    sm.set_scalar_reg(2, 20);
    sm.set_instr_descriptor_buf(&desc, 1);
    sm.exe_once();

    REQUIRE(sm.is_instruction_completed(100));
    REQUIRE_FALSE(sm.is_instruction_completed(101));
    REQUIRE_FALSE(sm.is_instruction_completed(99));
}

TEST_CASE("is_instruction_completed: 多 id 注入, 部分完成 (A4)",
          "[sm-completed][sm-microarch][task18-p2-2]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    DeviceConfig cfg{};
    REQUIRE(sm.initialize(cfg));

    InstrDescriptor d0{};
    d0.instr_id = 200;
    d0.pipe = PipeClass::kScalarALU;
    d0.latency_class = LatencyClass::kFixed1Cycle;
    d0.src_regs[0] = 1;
    d0.src_regs[1] = 2;
    d0.dst_regs[0] = 5;
    d0.num_src = 2;
    d0.num_dst = 1;

    InstrDescriptor d1{};
    d1.instr_id = 201;
    d1.pipe = PipeClass::kScalarALU;
    d1.latency_class = LatencyClass::kFixed1Cycle;
    d1.src_regs[0] = 3;
    d1.src_regs[1] = 4;
    d1.dst_regs[0] = 6;
    d1.num_src = 2;
    d1.num_dst = 1;

    InstrDescriptor batch[2] = {d0, d1};
    sm.set_scalar_reg(1, 1);
    sm.set_scalar_reg(2, 2);
    sm.set_scalar_reg(3, 3);
    sm.set_scalar_reg(4, 4);
    sm.set_instr_descriptor_buf(batch, 2);

    // 推进 1 cycle: 第一条 ScalarALU execute 完成
    sm.exe_once();
    REQUIRE(sm.is_instruction_completed(200));
    REQUIRE_FALSE(sm.is_instruction_completed(201));

    // 推进 1 cycle: 第二条完成
    sm.exe_once();
    REQUIRE(sm.is_instruction_completed(200));
    REQUIRE(sm.is_instruction_completed(201));

    // 未注册的 id 仍 false
    REQUIRE_FALSE(sm.is_instruction_completed(202));
    REQUIRE_FALSE(sm.is_instruction_completed(199));
}

TEST_CASE("is_instruction_completed: 重新 initialize 清空 completed (A5)",
          "[sm-completed][sm-microarch][task18-p2-2]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    DeviceConfig cfg{};
    REQUIRE(sm.initialize(cfg));

    InstrDescriptor desc{};
    desc.instr_id = 300;
    desc.pipe = PipeClass::kScalarALU;
    desc.latency_class = LatencyClass::kFixed1Cycle;
    desc.src_regs[0] = 1;
    desc.src_regs[1] = 2;
    desc.dst_regs[0] = 5;
    desc.num_src = 2;
    desc.num_dst = 1;
    sm.set_scalar_reg(1, 10);
    sm.set_scalar_reg(2, 20);
    sm.set_instr_descriptor_buf(&desc, 1);
    sm.exe_once();
    REQUIRE(sm.is_instruction_completed(300));

    // 重新 initialize 应清空 completed_instr_ids_ (per SM.hh:143-148)
    REQUIRE(sm.initialize(cfg));
    REQUIRE_FALSE(sm.is_instruction_completed(300));
    REQUIRE_FALSE(sm.is_instruction_completed(0));
}