// test/test_sm_bundle_internal_wiring.cc
// Test: 8 Bundle pipeline 3 段 C++ 直连验证 (per plan Task 2.14 + Oracle Batch 3 重定)
//
// 验证 SM 内部 3 段 pipeline bundle 交接 (per Oracle Q4 边界):
//   - 唯一真 queue 是 instr_ring_ (SM.hh fetch_next_instr), 其余是单描述 accessor
//   - 真值执行走 parent_ (sm.lsu_lds()->execute(d)), queue 注入只用于 ring 语义
//
// 验证的 3 段连接 (≥2 断言/连接 per v2 acceptance):
//   A1: FetchUnit → DecodeUnit (via fetched() accessor, decoded().pipe/latency_class)
//   A2: DecodeUnit → IssueUnit (via decoded() accessor, issued().pipe + warp_id round-robin)
//   A3: IssueUnit → ScalarALU (via issued() accessor, scalar_alu execute → mark_completed + reg write)
//
// 关键路径:
//   - 注入 kScalarALU desc, exe_once 推进 1 cycle 触发全链 dispatch
//   - 验证中间状态 (decoded/issued) 反映正确字段 + 最后状态 (completed + reg)
//
// 作者 CppTLM Team / 2027-02-10 (Task 18b P1-14 实施, per Oracle 预审 Batch 3 重定)
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "tlm/gpu/streaming_multiprocessor_tlm.hh"

using namespace tlm;
using namespace cpptlm::gpu;

TEST_CASE("Bundle FetchUnit → DecodeUnit (单描述 accessor, A1)",
          "[sm-bundle][sm-microarch][task18-p1-14]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    DeviceConfig cfg{};
    REQUIRE(sm.initialize(cfg));

    // 注入 kScalarALU desc
    InstrDescriptor desc{};
    desc.instr_id = 1001;
    desc.pipe = PipeClass::kScalarALU;
    desc.latency_class = LatencyClass::kFixed1Cycle;
    desc.src_regs[0] = 1;
    desc.src_regs[1] = 2;
    desc.dst_regs[0] = 5;
    desc.num_src = 2;
    desc.num_dst = 1;
    sm.set_scalar_reg(1, 100);
    sm.set_scalar_reg(2, 200);
    sm.set_instr_descriptor_buf(&desc, 1);

    // 推进 1 cycle: fu_ tick 取指 + consume ring, du_ tick dispatch
    sm.exe_once();

    // 验证 DecodeUnit 收到 desc (字段透传)
    auto decoded = sm.du()->decoded();
    REQUIRE(decoded.instr_id == 1001);
    REQUIRE(decoded.pipe == PipeClass::kScalarALU);
    REQUIRE(decoded.latency_class == LatencyClass::kFixed1Cycle);
}

TEST_CASE("Bundle DecodeUnit → IssueUnit (round-robin warp 调度, A2)",
          "[sm-bundle][sm-microarch][task18-p1-14]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    DeviceConfig cfg{};
    REQUIRE(sm.initialize(cfg));

    // 注入 kScalarALU desc (warpid=0 per Task 2.4 round-robin 起点)
    InstrDescriptor desc{};
    desc.instr_id = 1002;
    desc.pipe = PipeClass::kScalarALU;
    desc.latency_class = LatencyClass::kFixed1Cycle;
    desc.src_regs[0] = 0;
    desc.dst_regs[0] = 3;
    desc.num_src = 1;
    desc.num_dst = 1;
    sm.set_instr_descriptor_buf(&desc, 1);

    sm.exe_once();

    // 验证 IssueUnit 收到 desc + round-robin warp_id == 1 (per Task 2.4 round-robin 1→2→3→0)
    auto issued = sm.iu()->issued();
    REQUIRE(issued.instr_id == 1002);
    REQUIRE(issued.pipe == PipeClass::kScalarALU);
    REQUIRE(issued.warpid == 1);  // round-robin 起点 warp 1 (per Task 2.4 实现)
}

TEST_CASE("Bundle IssueUnit → ScalarALU (执行真值 + 完成, A3)",
          "[sm-bundle][sm-microarch][task18-p1-14]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    DeviceConfig cfg{};
    REQUIRE(sm.initialize(cfg));

    // 注入 ADD kScalarALU: r1=10, r2=20 → r3=30
    InstrDescriptor desc{};
    desc.instr_id = 1003;
    desc.pipe = PipeClass::kScalarALU;
    desc.latency_class = LatencyClass::kFixed1Cycle;
    desc.src_regs[0] = 1;
    desc.src_regs[1] = 2;
    desc.dst_regs[0] = 3;
    desc.num_src = 2;
    desc.num_dst = 1;
    sm.set_scalar_reg(1, 10);
    sm.set_scalar_reg(2, 20);
    sm.set_instr_descriptor_buf(&desc, 1);

    sm.exe_once();

    // 验证 ScalarALU 执行 (per Task 1.3 真值: ADD → set_scalar_reg + mark_completed)
    REQUIRE(sm.is_instruction_completed(1003));
    uint64_t v3 = 0;
    REQUIRE(sm.get_register_value(0, 0, 3, &v3));
    REQUIRE(v3 == 30);  // ADD: 10 + 20
}
