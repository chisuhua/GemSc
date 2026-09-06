// test/test_sm_decode_unit.cc
// Test: DecodeUnitTLM 真值 + 从 fetched 提取 pipe/latency_class (per plan Task 2.3)
//
// 验证 DecodeUnitTLM 真实行为 (per Oracle 预审 Task 2.3 Q11 模板, ≥2 断言真实行为):
//   1. decoded().pipe == fetched().instr_desc.pipe (从 InstrDescriptor 提取)
//   2. decoded().latency_class == fetched().instr_desc.latency_class
//   3. decoded().instr_desc.instr_id == fetched().instr_desc.instr_id (继承透传, 无 slice)
//   4. 空 fetched tick 安全: has_decoded() == false
//
// 关键路径 (per Oracle F-2/F-4/F-5 修正):
//   - DecodeUnitTLM 持 SM 顶层 parent 指针 (仿 FetchUnitTLM cpptlm::gpu::ScalarALU 先例)
//   - 2 参数构造 (name, eq) + set_parent(SM*) 接口 (per module_factory 2 参数 lambda)
//   - SM 构造函数必须 du_->set_parent(this) (Task 2.3 P0 修复, Task 2.2 漏了)
//   - SM .hh 加 du() accessor
//   - SM.exe_once() 插入 du_->tick() 在 fu_->tick() 之后 (per Oracle Q4 A 策略)
//
// 作者 CppTLM Team / 2027-02-10 (Task 18b P1-3 实施, per Oracle 预审 Task 2.3)
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "tlm/gpu/streaming_multiprocessor_tlm.hh"

using namespace tlm;
using namespace cpptlm::gpu;

TEST_CASE("DecodeUnitTLM 提取 pipe/latency_class (真值, ≥2 断言)",
          "[sm-unit][sm-decode][task18]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    DeviceConfig cfg{};
    REQUIRE(sm.initialize(cfg));

    // 准备 2 条 desc (不同 pipe/latency 验证提取)
    InstrDescriptor d[2] = {};
    d[0].instr_id = 100;
    d[0].pipe = PipeClass::kVectorALU;
    d[0].latency_class = LatencyClass::kFixed4Cycle;
    d[1].instr_id = 101;
    d[1].pipe = PipeClass::kScalarALU;
    d[1].latency_class = LatencyClass::kFixed1Cycle;
    sm.set_instr_descriptor_buf(d, 2);

    // Fetch + Decode pipeline
    sm.fu()->tick();
    sm.du()->tick();

    // 断言 1: pipe 正确传递
    REQUIRE(sm.du()->has_decoded());
    REQUIRE(sm.du()->decoded().pipe == PipeClass::kVectorALU);

    // 断言 2: latency_class 正确传递
    REQUIRE(sm.du()->decoded().latency_class == LatencyClass::kFixed4Cycle);

    // 断言 3: 继承字段透传 (instr_id/warp_id/pc 无 slice)
    REQUIRE(sm.du()->decoded().instr_desc.instr_id == 100);
    REQUIRE(sm.du()->decoded().warp_id == sm.fu()->fetched().warp_id);

    // 多 cycle 推进: 第 2 条 desc 应被 Decode 提取
    sm.fu()->tick();
    sm.du()->tick();
    REQUIRE(sm.du()->decoded().pipe == PipeClass::kScalarALU);
    REQUIRE(sm.du()->decoded().latency_class == LatencyClass::kFixed1Cycle);
    REQUIRE(sm.du()->decoded().instr_desc.instr_id == 101);

    // 断言 4: 空 fetched tick 安全 (per Oracle Q11)
    StreamingMultiprocessorTLM sm2("sm1", &eq);
    REQUIRE(sm2.initialize(cfg));
    sm2.du()->tick();  // 无 fetch → has_decoded false
    REQUIRE(!sm2.du()->has_decoded());
}
