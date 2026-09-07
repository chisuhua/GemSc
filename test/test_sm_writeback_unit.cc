// test/test_sm_writeback_unit.cc
// Test: WritebackUnit 真值 + in-flight 队列 + per-warp 写回 (per plan Task 2.12)
//
// 验证 cpptlm::gpu::WritebackUnit:
//   - A1: scalar_reg write back (result_num=1, dst_regs[0]=7, result_value[0]=0xCAFE)
//   - A2: vector_reg write back (result_num=4, 4 对 dst/value 同时写回)
//   - A3: in-flight 队列 drain (2 条入队, finish_cycle=1/2, 推进 1 cycle 后只 drain 第 1 条)
//
// 关键路径 (per Oracle F-2 P0 re-scope):
//   - re-scoped to rf_-only (HT-release 推迟 Task 2.13)
//   - 回写循环用 result_num 守卫 (per Oracle F-1 P1)
//   - warp 来源是 desc.warpid (per Oracle F-2 P1, per-warp key)
//
// 作者 CppTLM Team / 2027-02-10 (Task 18b P1-12 实施)
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "tlm/gpu/streaming_multiprocessor_tlm.hh"

using namespace tlm;
using namespace cpptlm::gpu;

TEST_CASE("WritebackUnit scalar write back via result_num=1 (A1)",
          "[sm-wb][sm-microarch][task18-p1-12]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    DeviceConfig cfg{};
    REQUIRE(sm.initialize(cfg));

    // 构造单结果 desc: result_value[0]=0xCAFE → dst_regs[0]=7, warpid=0
    InstrDescriptor desc{};
    desc.instr_id = 1;
    desc.pipe = PipeClass::kScalarALU;
    desc.latency_class = LatencyClass::kFixed1Cycle;
    desc.warpid = 0;
    desc.result_num = 1;
    desc.dst_regs[0] = 7;
    desc.result_value[0] = 0xCAFE;
    // 写回 RF: 入队 finish_cycle=0 (当前周期立即完成)
    sm.writeback()->enqueue(desc, 0);

    // 推进 1 周期 + WB tick
    eq.run(1);
    sm.wb()->tick();

    // 验证: RegFileUnit warp 0 reg 7 = 0xCAFE
    uint64_t v = 0;
    REQUIRE(sm.reg_file()->read(0, 7, &v));
    REQUIRE(v == 0xCAFE);
    REQUIRE(sm.writeback()->pending_count() == 0);
}

TEST_CASE("WritebackUnit vector write back via result_num=4 (A2)",
          "[sm-wb][sm-microarch][task18-p1-12]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    DeviceConfig cfg{};
    REQUIRE(sm.initialize(cfg));

    // 构造 4 结果 desc (VIADD.U8x4 真值): dst=[10,11,12,13], value=[0xAA,0xBB,0xCC,0xDD]
    InstrDescriptor desc{};
    desc.instr_id = 2;
    desc.pipe = PipeClass::kVectorALU;
    desc.latency_class = LatencyClass::kFixed4Cycle;
    desc.warpid = 1;  // per-warp: warp 1 (测试 per-warp 隔离)
    desc.result_num = 4;
    desc.dst_regs[0] = 10;
    desc.dst_regs[1] = 11;
    desc.dst_regs[2] = 12;
    desc.dst_regs[3] = 13;
    desc.result_value[0] = 0xAA;
    desc.result_value[1] = 0xBB;
    desc.result_value[2] = 0xCC;
    desc.result_value[3] = 0xDD;
    sm.writeback()->enqueue(desc, 0);

    eq.run(1);
    sm.wb()->tick();

    // 验证 4 个寄存器全部写入 warp 1
    uint64_t v = 0;
    REQUIRE(sm.reg_file()->read(1, 10, &v));
    REQUIRE(v == 0xAA);
    REQUIRE(sm.reg_file()->read(1, 11, &v));
    REQUIRE(v == 0xBB);
    REQUIRE(sm.reg_file()->read(1, 12, &v));
    REQUIRE(v == 0xCC);
    REQUIRE(sm.reg_file()->read(1, 13, &v));
    REQUIRE(v == 0xDD);
}

TEST_CASE("WritebackUnit in-flight queue drain order (A3)",
          "[sm-wb][sm-microarch][task18-p1-12]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    DeviceConfig cfg{};
    REQUIRE(sm.initialize(cfg));

    // 2 条入队: 第 1 条 finish_cycle=1, 第 2 条 finish_cycle=2
    InstrDescriptor d1{};
    d1.instr_id = 10;
    d1.pipe = PipeClass::kScalarALU;
    d1.warpid = 0;
    d1.result_num = 1;
    d1.dst_regs[0] = 100;
    d1.result_value[0] = 0x1111;
    sm.writeback()->enqueue(d1, 1);  // 第 1 周期完成

    InstrDescriptor d2{};
    d2.instr_id = 11;
    d2.pipe = PipeClass::kScalarALU;
    d2.warpid = 0;
    d2.result_num = 1;
    d2.dst_regs[0] = 200;
    d2.result_value[0] = 0x2222;
    sm.writeback()->enqueue(d2, 2);  // 第 2 周期完成

    REQUIRE(sm.writeback()->pending_count() == 2);

    // 推进 1 周期 + WB tick: 只 drain 第 1 条 (finish_cycle=1 ≤ current=1)
    eq.run(1);
    sm.wb()->tick();

    uint64_t v = 0;
    REQUIRE(sm.reg_file()->read(0, 100, &v));
    REQUIRE(v == 0x1111);
    REQUIRE_FALSE(sm.reg_file()->read(0, 200, &v));  // 第 2 条尚未完成
    REQUIRE(sm.writeback()->pending_count() == 1);

    // 推进 1 周期 + WB tick: drain 第 2 条
    eq.run(1);
    sm.wb()->tick();

    REQUIRE(sm.reg_file()->read(0, 200, &v));
    REQUIRE(v == 0x2222);
    REQUIRE(sm.writeback()->pending_count() == 0);
}
