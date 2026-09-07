// test/test_sm_lsu_global_e2e.cc
// Test: LsuGlobal 真值 + 异步内存回调骨架 (per plan Task 2.9 line 790)
//
// 验证 LsuGlobal 子模块真值 (per Oracle 预审 Task 2.9 APPROVE-WITH-FIXES):
//   A1: 模块身份 (lg()/lsu_global() accessor + 类型正确)
//   A2: 异步 load 真值 (注入 kLsuGlobal + is_memory → 10 cycle 后回写 dst + mark_completed)
//   A3: pipe 互斥 (kVectorALU desc 经 lg_ tick 不入 pending 队列, pending_count()==0)
//
// 关键路径 (per Oracle Q11):
//   - lg_ 必须 make_unique (已, SM.cc) + set_parent(this) (P0 修复, Oracle F-2)
//   - lsu_global_ 必须 make_unique (新, cpptlm::gpu::LsuGlobal 真值类, 镜像 SIMTLane 模式)
//   - lsu_global_tlm.stub 需补 parent_/set_parent 声明 (P1 修复, 镜像 simt_lane_tlm stub)
//   - SM.exe_once() 关键 P0 修正: lsu_global_->tick() 无条件在 head (异步不依赖 ring)
//   - 真值行为: enqueue 时快照 PendingRequest, tick 推进 counter, 归零回调写 scalar_regs_ + mark_completed
//   - latency_cycles_ = 10 (per Oracle Q3 B 固定)
//
// 作者 CppTLM Team / 2027-02-10 (Task 18b P1-9 实施, per Oracle 预审 Task 2.9)
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "tlm/gpu/streaming_multiprocessor_tlm.hh"

using namespace tlm;
using namespace cpptlm::gpu;

TEST_CASE("LsuGlobal 模块身份 + 真值 (per plan line 790 '异步内存回调骨架')",
          "[sm][lsu][sm-microarch][task18]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    DeviceConfig cfg{};
    REQUIRE(sm.initialize(cfg));

    // A1: 模块身份 (per Oracle Q8 A1)
    REQUIRE(sm.lg() != nullptr);
    REQUIRE(sm.lg()->get_module_type() == "LsuGlobal");
    REQUIRE(sm.lsu_global() != nullptr);  // cpptlm::gpu::LsuGlobal 真值类
    REQUIRE(sm.lsu_global()->pending_count() == 0);  // 初始空
}

TEST_CASE("LsuGlobal 异步 load 真值: 10 cycle 后回写 + mark_completed",
          "[sm][lsu][sm-microarch][task18]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    DeviceConfig cfg{};
    REQUIRE(sm.initialize(cfg));

    // A2: 异步 load 真值 (per Oracle Q8 A2 + Q3 B 固定 latency=10)
    // 注入 kLsuGlobal + is_memory desc, lg_ tick() dispatch 到 lsu_global_->execute()
    // enqueue PendingRequest {instr_id=700, dst=7, data=0xDEADBEEFCAFEBABE, cycles=10}
    InstrDescriptor desc{};
    desc.instr_id = 700;
    desc.pipe = PipeClass::kLsuGlobal;
    desc.latency_class = LatencyClass::kMemory;
    desc.is_memory = true;
    desc.memory_data = 0xDEADBEEFCAFEBABEULL;
    desc.memory_data_valid = 1;
    desc.dst_regs[0] = 7;
    desc.num_dst = 1;
    desc.target_vaddr = 0x1000;
    desc.num_src = 0;
    sm.set_instr_descriptor_buf(&desc, 1);

    // 推进 9 cycle (未到 latency=10, pending 仍 active)
    for (int i = 0; i < 9; ++i) {
        sm.exe_once();
    }
    REQUIRE_FALSE(sm.is_instruction_completed(700));  // 未归零回调

    // 推进 3 more cycle (总 12 cycle, 超过 latency=10, 归零回调)
    for (int i = 0; i < 3; ++i) {
        sm.exe_once();
    }
    REQUIRE(sm.is_instruction_completed(700));  // 归零回调
    // 数据回写: dst=7 寄存器存 0xDEADBEEFCAFEBABE
    uint64_t val = 0;
    REQUIRE(sm.get_register_value(0, 0, 7, &val));
    REQUIRE(val == 0xDEADBEEFCAFEBABEULL);
    // pending 队列空 (回调后弹出)
    REQUIRE(sm.lsu_global()->pending_count() == 0);
}

TEST_CASE("LsuGlobal pipe 互斥 (非 kLsuGlobal 不入 pending 队列)",
          "[sm][lsu][sm-microarch][task18]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    DeviceConfig cfg{};
    REQUIRE(sm.initialize(cfg));

    // A3: pipe 互斥 (per Oracle Q8 A3)
    // 注入 kVectorALU desc (非 kLsuGlobal, lg_ tick 静默不入 pending)
    InstrDescriptor desc{};
    desc.instr_id = 800;
    desc.pipe = PipeClass::kVectorALU;  // 非 kLsuGlobal
    desc.latency_class = LatencyClass::kFixed1Cycle;
    desc.dst_regs[0] = 9;
    desc.src_regs[0] = 1;
    desc.src_regs[1] = 2;
    desc.num_src = 2;
    desc.num_dst = 1;
    sm.set_scalar_reg(1, 100);
    sm.set_scalar_reg(2, 200);
    sm.set_instr_descriptor_buf(&desc, 1);

    for (int i = 0; i < 5; ++i) {
        sm.exe_once();
    }

    // va_ tick 执行 VIADD.U8x4 (u8 wrap): 100+200 lane0 wrap → 44 (per Task 2.6 真值)
    // 此处仅验证 reg 9 已被 VectorALU 写入 (非 0), 具体值已在 Task 2.6 测试覆盖
    uint64_t val9 = 0;
    REQUIRE(sm.get_register_value(0, 0, 9, &val9));
    REQUIRE(val9 != 0);  // VectorALU 已写入 (VIADD.U8x4 wrap 结果)

    // A3: lg_ 不应误入 pending 队列 (pipe 互斥, kVectorALU 不触发 LsuGlobal)
    REQUIRE(sm.lsu_global()->pending_count() == 0);
    // VectorALU 真值 (Task 2.6) 调 mark_completed → instr_id 800 已完成
    REQUIRE(sm.is_instruction_completed(800));
}
