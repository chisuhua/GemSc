// test/test_sm_stepping_e2e.cc
// Test: IComputeDevice 完整 stepping 端到端 (per plan Task 2.15 + Oracle P1-3)
//
// 验证 L4 stepping 协议 (per architecture/15 §15.5.6):
//   A1: 完整 kernel lifetime: initialize → set_instr_descriptor_buf → exe_once loop → 末批结果取回
//   A2: 末批 is_finished() + get_register_value 轮询 (per HSK-9 §3 "末批指令结果取回" 协议)
//   A3: lane_id=0xFFFFFFFF 默认值语义 (per HSK-9 §3 "返回 lane 0 的值")
//
// 关键路径:
//   - PTX-EMU 驱动方: set_instr_descriptor_buf → 必须调 exe_once() 推进 cycle
//   - is_instruction_completed 轮询: PTX-EMU spin 直到返回 true
//   - get_register_value lane_id=0xFFFFFFFF: 表示 warp 所有 lane 寄存器值相同
//
// 作者 CppTLM Team / 2027-02-10 (Task 18b P1-15 实施)
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "tlm/gpu/streaming_multiprocessor_tlm.hh"

using namespace tlm;
using namespace cpptlm::gpu;

TEST_CASE("IComputeDevice 完整 kernel stepping lifetime (A1)",
          "[sm-stepping][sm-microarch][task18-p1-15]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);

    // 初始化
    DeviceConfig cfg{};
    cfg.num_sms = 1;
    REQUIRE(sm.initialize(cfg));

    // 注入 3 条 kScalarALU desc (模拟 PTX-EMU kernel 内多个 warp 调度)
    InstrDescriptor d0{};
    d0.instr_id = 1;
    d0.pipe = PipeClass::kScalarALU;
    d0.src_regs[0] = 1; d0.src_regs[1] = 2;
    d0.dst_regs[0] = 10;
    d0.num_src = 2; d0.num_dst = 1;
    sm.set_scalar_reg(1, 100); sm.set_scalar_reg(2, 200);

    InstrDescriptor d1{};
    d1.instr_id = 2;
    d1.pipe = PipeClass::kScalarALU;
    d1.src_regs[0] = 3; d1.src_regs[1] = 4;
    d1.dst_regs[0] = 11;
    d1.num_src = 2; d1.num_dst = 1;
    sm.set_scalar_reg(3, 50); sm.set_scalar_reg(4, 30);

    InstrDescriptor d2{};
    d2.instr_id = 3;
    d2.pipe = PipeClass::kScalarALU;
    d2.src_regs[0] = 5; d2.src_regs[1] = 6;
    d2.dst_regs[0] = 12;
    d2.num_src = 2; d2.num_dst = 1;
    sm.set_scalar_reg(5, 7); sm.set_scalar_reg(6, 13);

    InstrDescriptor batch[3] = {d0, d1, d2};
    sm.set_instr_descriptor_buf(batch, 3);

    // 推进 5 cycle (覆盖 3 条 kFixed1Cycle ScalarALU + 余量)
    for (int i = 0; i < 5; ++i) sm.exe_once();

    // 全部 3 条指令已完成
    REQUIRE(sm.is_instruction_completed(1));
    REQUIRE(sm.is_instruction_completed(2));
    REQUIRE(sm.is_instruction_completed(3));

    // 验证 ScalarALU 真值 ADD 结果
    uint64_t v10 = 0, v11 = 0, v12 = 0;
    REQUIRE(sm.get_register_value(0, 0, 10, &v10));
    REQUIRE(v10 == 300);  // 100 + 200
    REQUIRE(sm.get_register_value(0, 0, 11, &v11));
    REQUIRE(v11 == 80);   // 50 + 30
    REQUIRE(sm.get_register_value(0, 0, 12, &v12));
    REQUIRE(v12 == 20);   // 7 + 13

    // shutdown 清理
    sm.shutdown();
}

TEST_CASE("IComputeDevice 末批 is_finished + get_register_value 轮询 (A2)",
          "[sm-stepping][sm-microarch][task18-p1-15]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    DeviceConfig cfg{};
    REQUIRE(sm.initialize(cfg));

    InstrDescriptor d{};
    d.instr_id = 42;
    d.pipe = PipeClass::kScalarALU;
    d.latency_class = LatencyClass::kFixed1Cycle;
    d.src_regs[0] = 1; d.src_regs[1] = 2;
    d.dst_regs[0] = 7;
    d.num_src = 2; d.num_dst = 1;
    sm.set_scalar_reg(1, 50); sm.set_scalar_reg(2, 49);
    sm.set_instr_descriptor_buf(&d, 1);

    sm.exe_once();

    REQUIRE(sm.is_instruction_completed(42));

    uint64_t out_val = 0;
    REQUIRE(sm.get_register_value(0, 0, 7, &out_val));
    REQUIRE(out_val == 99);
}

TEST_CASE("IComputeDevice lane_id=0xFFFFFFFF 默认值语义 (A3)",
          "[sm-stepping][sm-microarch][task18-p1-15]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    DeviceConfig cfg{};
    REQUIRE(sm.initialize(cfg));

    // 写 warp 0 reg 5 = 0xCAFE (经 facade set_scalar_reg)
    sm.set_scalar_reg(5, 0xCAFE);

    // lane_id 默认 0xFFFFFFFF: 表示 warp 所有 lane 寄存器值相同, 返回 lane 0 的值
    uint64_t v_default = 0;
    REQUIRE(sm.get_register_value(0, 0, 5, &v_default));  // 默认 lane_id=0xFFFFFFFF
    REQUIRE(v_default == 0xCAFE);

    // lane_id 显式 0 也应该读到同样值 (per-warp 隔离, lane 0 是 warp 0 的代表)
    uint64_t v_lane0 = 0;
    REQUIRE(sm.get_register_value(0, 0, 5, &v_lane0, 0));
    REQUIRE(v_lane0 == 0xCAFE);
}
