// test/test_sm_issue_unit.cc
// Test: IssueUnitTLM 真值 + Round-robin warp 调度 (per plan Task 2.4)
//
// 验证 IssueUnitTLM 真实行为 (per Oracle 预审 Task 2.4 Q13 模板, ≥3 断言真实行为):
//   1. 4 cycle warp_id 序列 1→2→3→0 (Round-robin + wrap-around 边界)
//   2. issued().instr_desc.instr_id 透传 (继承 DecodeToIssueBundle 字段)
//   3. 空 decoded tick 安全 (has_issued() == false)
//
// 关键路径 (per Oracle 预审 Task 2.4 Q13):
//   - IssueUnitTLM 持 SM 顶层 parent 指针 (镜像 FetchUnitTLM/DecodeUnitTLM 模式)
//   - 2 参数构造 (name, eq) + set_parent(SM*) 接口
//   - read parent_->du()->decoded() (已 Decode, 不再 Decode)
//   - Round-robin: (last_issued_warp_id_ + 1) % num_warps_ (num_warps_=4)
//   - 首 tick 约定: warp_id = 1 (last_issued_warp_id_ 初值 0, 调度到 1)
//   - SM 构造函数必须 iu_->set_parent(this) (per Oracle F-2 P0 修复)
//
// 作者 CppTLM Team / 2027-02-10 (Task 18b P1-4 实施, per Oracle 预审 Task 2.4)
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "tlm/gpu/streaming_multiprocessor_tlm.hh"

using namespace tlm;
using namespace cpptlm::gpu;

TEST_CASE("IssueUnitTLM Round-robin warp 调度 (真值, ≥3 断言)", "[sm-unit][sm-issue][task18]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    DeviceConfig cfg{};
    REQUIRE(sm.initialize(cfg));

    // 准备 4 条 desc (覆盖 num_warps_=4, 验证 wrap-around 边界)
    InstrDescriptor d[4] = {};
    for (uint32_t i = 0; i < 4; ++i) {
        d[i].instr_id = 100 + i;
        d[i].pipe = PipeClass::kScalarALU;
        d[i].latency_class = LatencyClass::kFixed1Cycle;
    }
    sm.set_instr_descriptor_buf(d, 4);

    // 约定: 首 tick 从 last_issued_warp_id_=0 调度到 1, 4 cycle 序列 1→2→3→0 (wrap-around)
    const uint32_t expect[4] = {1, 2, 3, 0};
    for (uint32_t i = 0; i < 4; ++i) {
        sm.fu()->tick();
        sm.du()->tick();
        sm.iu()->tick();
        // 断言 1: Round-robin warp_id 序列 + wrap-around 边界
        REQUIRE(sm.iu()->has_issued());
        REQUIRE(sm.iu()->issued().warp_id == expect[i]);
        // 断言 2: 继承字段透传 (instr_id 来自 decoded, 无 slice)
        REQUIRE(sm.iu()->issued().instr_desc.instr_id == 100 + i);
    }

    // 断言 3: 空 decoded tick 安全 (per Oracle Q13)
    StreamingMultiprocessorTLM sm2("sm1", &eq);
    REQUIRE(sm2.initialize(cfg));
    sm2.iu()->tick(); // 无 decoded → has_issued false
    REQUIRE(!sm2.iu()->has_issued());
}
