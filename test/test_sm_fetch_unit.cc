// test/test_sm_fetch_unit.cc
// Test: FetchUnitTLM 真值 + SM.exe_once 取指职责迁移 (per plan Task 2.2)
//
// 验证 FetchUnitTLM 真实行为 (per Oracle 预审 Task 2.2 Q8 模板, ≥2 断言真实行为):
//   1. 数据正确传递: tick() 后 FetchToIssueBundle.instr_desc/warp_id/pc 等于 ring front
//   2. consume 推进: tick() 后 ring_count 减 1; 连续 2 次 tick 第二条被取到
//   3. 空 ring tick() 安全: 不崩溃, count 不变, has_fetched() == false
//   4. (集成锚点) exe_once() 迁移后 ScalarALU 真值仍工作 (test_sm_scalar_alu_e2e 锚点)
//
// 关键路径 (per Oracle F-1/F-2/F-4 修正):
//   - FetchUnitTLM 持 SM 顶层 parent 指针 (仿 cpptlm::gpu::ScalarALU 先例)
//   - SM 顶层加 fetch_next_instr() accessor + fu()/ring_count() accessor
//   - SM.exe_once() 取指/consume 下沉到 FetchUnitTLM.tick() (消除双消费者)
//   - SM 构造函数 make_unique<sm::FetchUnitTLM>(this) (P0 修复 12 子模块未构造)
//
// 作者 CppTLM Team / 2027-02-10 (Task 18b P1-2 实施, per Oracle 预审 Task 2.2)
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "tlm/gpu/streaming_multiprocessor_tlm.hh"

using namespace tlm;
using namespace cpptlm::gpu;

TEST_CASE("FetchUnitTLM 从 ring buffer 取下一条写 FetchToIssueBundle (真值, ≥2 断言)",
          "[sm-unit][sm-fetch][task18]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    DeviceConfig cfg{};
    REQUIRE(sm.initialize(cfg));

    // 准备 2 条 desc (InstrDescriptor warpid 字段是 u8, 非 warp_id)
    InstrDescriptor d[2] = {};
    d[0].instr_id = 100;
    d[0].pipe = PipeClass::kScalarALU;
    d[0].latency_class = LatencyClass::kFixed1Cycle;
    d[0].warpid = 3;
    d[0].pc = 0x1000;
    d[1].instr_id = 101;
    d[1].pipe = PipeClass::kVectorALU;
    d[1].latency_class = LatencyClass::kFixed4Cycle;
    d[1].warpid = 5;
    d[1].pc = 0x1008;
    sm.set_instr_descriptor_buf(d, 2);
    REQUIRE(sm.ring_count() == 2);

    // 断言 1: 数据正确传递 (FetchToIssueBundle.instr_desc/warp_id/pc == ring front)
    sm.fu()->tick();
    REQUIRE(sm.fu()->has_fetched());
    REQUIRE(sm.fu()->fetched().instr_desc.instr_id == 100);
    REQUIRE(sm.fu()->fetched().warp_id == 3);
    REQUIRE(sm.fu()->fetched().pc == 0x1000);

    // 断言 2: consume 推进 (ring_count 减 1)
    REQUIRE(sm.ring_count() == 1);

    // 断言 3: 多 cycle 推进 (第二次 tick 取第二条)
    sm.fu()->tick();
    REQUIRE(sm.fu()->fetched().instr_desc.instr_id == 101);
    REQUIRE(sm.fu()->fetched().warp_id == 5);
    REQUIRE(sm.fu()->fetched().pc == 0x1008);
    REQUIRE(sm.ring_count() == 0);

    // 断言 4: 空 ring tick() 安全 (不崩溃, count 不变, has_fetched false)
    sm.fu()->tick();
    REQUIRE(sm.ring_count() == 0);
    REQUIRE(!sm.fu()->has_fetched());
}
