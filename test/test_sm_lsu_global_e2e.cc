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
//   - 真值行为: enqueue 时快照 PendingRequest, tick 推进 counter, 归零回调写 scalar_regs_ +
//   mark_completed
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
    REQUIRE(sm.lsu_global() != nullptr);            // cpptlm::gpu::LsuGlobal 真值类
    REQUIRE(sm.lsu_global()->pending_count() == 0); // 初始空
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
    REQUIRE_FALSE(sm.is_instruction_completed(700)); // 未归零回调

    // 推进 3 more cycle (总 12 cycle, 超过 latency=10, 归零回调)
    for (int i = 0; i < 3; ++i) {
        sm.exe_once();
    }
    REQUIRE(sm.is_instruction_completed(700)); // 归零回调
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
    desc.pipe = PipeClass::kVectorALU; // 非 kLsuGlobal
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
    REQUIRE(val9 != 0); // VectorALU 已写入 (VIADD.U8x4 wrap 结果)

    // A3: lg_ 不应误入 pending 队列 (pipe 互斥, kVectorALU 不触发 LsuGlobal)
    REQUIRE(sm.lsu_global()->pending_count() == 0);
    // VectorALU 真值 (Task 2.6) 调 mark_completed → instr_id 800 已完成
    REQUIRE(sm.is_instruction_completed(800));
}

TEST_CASE("LsuGlobal 多条目 FIFO 顺序 (Task 2.13.5 A4)",
          "[sm][lsu][sm-microarch][task18-p1-13-5]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    DeviceConfig cfg{};
    REQUIRE(sm.initialize(cfg));

    // 注入 2 条 kLsuGlobal desc, dst 不同, data 不同
    InstrDescriptor d1{};
    d1.instr_id = 901;
    d1.pipe = PipeClass::kLsuGlobal;
    d1.latency_class = LatencyClass::kMemory;
    d1.is_memory = true;
    d1.dst_regs[0] = 11;
    d1.memory_data = 0x1111;
    d1.num_dst = 1;
    d1.target_vaddr = 0x1000;
    InstrDescriptor d2{};
    d2.instr_id = 902;
    d2.pipe = PipeClass::kLsuGlobal;
    d2.latency_class = LatencyClass::kMemory;
    d2.is_memory = true;
    d2.dst_regs[0] = 22;
    d2.memory_data = 0x2222;
    d2.num_dst = 1;
    d2.target_vaddr = 0x2000;
    sm.set_instr_descriptor_buf(&d1, 1);
    sm.set_instr_descriptor_buf(&d2, 1);

    // 推进 1 cycle: fu_ consume 第一条 (901), lg_ tick dispatch → lsu_global_ enqueue
    sm.exe_once();
    REQUIRE(sm.lsu_global()->pending_count() == 1);
    // 推进 1 cycle: fu_ consume 第二条 (902), lg_ tick dispatch → lsu_global_ enqueue
    sm.exe_once();
    REQUIRE(sm.lsu_global()->pending_count() == 2);

    // 推进 9 cycle (901 在第 11 cycle 归零回调: enqueue cycle 2 + 9 次 lg head tick 后 remaining
    // 1→0)
    for (int i = 0; i < 9; ++i)
        sm.exe_once();
    REQUIRE(sm.is_instruction_completed(901));
    REQUIRE_FALSE(sm.is_instruction_completed(902));
    uint64_t v11 = 0;
    REQUIRE(sm.get_register_value(0, 0, 11, &v11));
    REQUIRE(v11 == 0x1111);

    // 推进 1 more cycle (902 起始剩余 10, 现在 10→9)
    sm.exe_once();
    // 推进 9 cycle (902 在第 21 cycle 归零)
    for (int i = 0; i < 9; ++i)
        sm.exe_once();
    REQUIRE(sm.is_instruction_completed(902));
    uint64_t v22 = 0;
    REQUIRE(sm.get_register_value(0, 0, 22, &v22));
    REQUIRE(v22 == 0x2222);
    REQUIRE(sm.lsu_global()->pending_count() == 0);
}

TEST_CASE("LsuGlobal latency_cycles() 覆盖路径 (Task 2.13.5 A5)",
          "[sm][lsu][sm-microarch][task18-p1-13-5]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    DeviceConfig cfg{};
    REQUIRE(sm.initialize(cfg));

    // 调短 latency_cycles=3, 验证 3 cycle 归零 (而非默认 10)
    sm.lsu_global()->set_latency_cycles(3);

    InstrDescriptor desc{};
    desc.instr_id = 950;
    desc.pipe = PipeClass::kLsuGlobal;
    desc.latency_class = LatencyClass::kMemory;
    desc.is_memory = true;
    desc.dst_regs[0] = 33;
    desc.memory_data = 0x3333;
    desc.num_dst = 1;
    desc.target_vaddr = 0x3000;
    sm.set_instr_descriptor_buf(&desc, 1);

    // 推进 2 cycle (未到 latency=3, pending 仍 active)
    sm.exe_once(); // cycle 1: fu consume + lg enqueue (remaining=3)
    sm.exe_once(); // cycle 2: lg tick head: remaining 3→2
    REQUIRE_FALSE(sm.is_instruction_completed(950));
    REQUIRE(sm.lsu_global()->pending_count() == 1);

    // 推进 2 more cycle (总 4 cycle, remaining 2→1→0 归零回调)
    sm.exe_once(); // cycle 3: remaining 2→1
    REQUIRE_FALSE(sm.is_instruction_completed(950));
    sm.exe_once(); // cycle 4: remaining 1→0 → 归零回调 write reg + mark_completed
    REQUIRE(sm.is_instruction_completed(950));
    REQUIRE(sm.lsu_global()->pending_count() == 0);
    uint64_t v33 = 0;
    REQUIRE(sm.get_register_value(0, 0, 33, &v33));
    REQUIRE(v33 == 0x3333);
}
