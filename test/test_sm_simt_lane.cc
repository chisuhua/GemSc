// test/test_sm_simt_lane.cc
// Test: SIMTLane 真值 + EXEC mask 64-bit + 分歧检测 (per plan Task 2.8)
//
// 验证 SIMTLane 子模块真值 (per Oracle 预审 Task 2.8 APPROVE-WITH-FIXES):
//   A1: 模块身份 (sl()/simt_lane() accessor + 类型正确)
//   A2: EXEC mask round-trip (kSIMTLane 注入 + execute() dispatch + get_active_mask 验证)
//   A3: 分歧检测 (全 0 mask → !is_divergent, 全 0xFF... → !is_divergent, 部分 → is_divergent)
//
// 关键路径 (per Oracle Q11):
//   - sl_ 必须 make_unique (已, Task 2.1 SM.cc) + set_parent(this) (P0 修复)
//   - simt_lane_ 必须 make_unique (新, cpptlm::gpu::SIMTLane 真值类, 镜像 VectorALU/MatrixCore)
//   - simt_lane_tlm.stub 需补 parent_ + set_parent 声明 (P1 修复, 镜像 matrix_core_tlm stub)
//   - SM.exe_once() 加 sl_->tick() 在 mc_->tick() 之后 (per Oracle Q4 A 推荐)
//   - 真值行为: execute() 更新 exec_mask_ 从 desc.exec_mask + is_divergent() bool 检测
//   - exec_mask_ 初始化为 0xFFFFFFFFFFFFFFFFull (per Oracle Q5 P1 修正, 镜像硬件 reset)
//
// 作者 CppTLM Team / 2027-02-10 (Task 18b P1-8 实施, per Oracle 预审 Task 2.8)
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "tlm/gpu/streaming_multiprocessor_tlm.hh"

using namespace tlm;
using namespace cpptlm::gpu;

TEST_CASE("SIMTLane 模块身份 + 真值 (per plan line 789 'EXEC mask 64-bit + 分歧检测')",
          "[sm][simt][sm-microarch][task18]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    DeviceConfig cfg{};
    REQUIRE(sm.initialize(cfg));

    // A1: 模块身份 (per Oracle Q8 A1)
    REQUIRE(sm.sl() != nullptr);
    REQUIRE(sm.sl()->get_module_type() == "SIMTLane");
    REQUIRE(sm.simt_lane() != nullptr);  // cpptlm::gpu::SIMTLane 真值类
}

TEST_CASE("SIMTLane EXEC mask 64-bit round-trip via execute dispatch",
          "[sm][simt][sm-microarch][task18]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    DeviceConfig cfg{};
    REQUIRE(sm.initialize(cfg));

    // A2: EXEC mask 真值 round-trip (per Oracle Q8 A2)
    // 注入 kSIMTLane desc 带 exec_mask → sl_ tick() dispatch → simt_lane_->execute(desc)
    // 验证 get_active_mask() 返回 desc.exec_mask
    InstrDescriptor desc{};
    desc.instr_id = 500;
    desc.pipe = PipeClass::kSIMTLane;
    desc.latency_class = LatencyClass::kFixed1Cycle;
    desc.exec_mask = 0xA5A5A5A5A5A5A5A5ULL;  // 交替位 0xA5 = 1010 0101
    desc.dst_regs[0] = 0;  // SIMTLane 无 dst
    desc.src_regs[0] = 0;  // SIMTLane 无 src
    desc.num_src = 0;
    desc.num_dst = 0;
    sm.set_instr_descriptor_buf(&desc, 1);

    // 推进 4 cycle (fu consume + 各 ALU 静默 + sl_ dispatch 真值)
    for (int i = 0; i < 4; ++i) {
        sm.exe_once();
    }

    // 验证 EXEC mask 真值 (per Oracle Q5: cpptlm::gpu::SIMTLane::execute 写入 desc.exec_mask)
    REQUIRE(sm.simt_lane()->get_active_mask() == 0xA5A5A5A5A5A5A5A5ULL);
}

TEST_CASE("SIMTLane 分歧检测 (全 0/全 0xFF... = uniform, 部分 = divergent)",
          "[sm][simt][sm-microarch][task18]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    DeviceConfig cfg{};
    REQUIRE(sm.initialize(cfg));

    // A3: 分歧检测 (per Oracle Q8 A3 + Q3 A+C)
    // 全 0 mask → !is_divergent (无 active threads = uniform)
    sm.simt_lane()->set_active_mask(0x0ULL);
    REQUIRE_FALSE(sm.simt_lane()->is_divergent());

    // 全 all-ones → !is_divergent (全 active = uniform)
    sm.simt_lane()->set_active_mask(0xFFFFFFFFFFFFFFFFULL);
    REQUIRE_FALSE(sm.simt_lane()->is_divergent());

    // 部分 mask (前半 1, 后半 0) → is_divergent
    sm.simt_lane()->set_active_mask(0xFFFFFFFF00000000ULL);
    REQUIRE(sm.simt_lane()->is_divergent());

    // 部分 mask (交错) → is_divergent
    sm.simt_lane()->set_active_mask(0xA5A5A5A5A5A5A5A5ULL);
    REQUIRE(sm.simt_lane()->is_divergent());
}
