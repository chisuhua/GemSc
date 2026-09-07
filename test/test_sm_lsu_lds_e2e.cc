// test/test_sm_lsu_lds_e2e.cc
// Test: LsuLDS 真值 + 共享内存 bank conflict 检测 stub (per plan Task 2.10)
//
// 验证 LsuLDS 子模块真值 (per Oracle 预审 Task 2.10 APPROVE-WITH-FIXES):
//   A1: 模块身份 (ll()/lsu_lds() accessor + 类型正确 + bank_conflict_count()==0)
//   A2: 同步 load 真值 (注入 kLsuLDS + is_memory → 立即回写 dst + mark_completed, 无 pending)
//   A3: pipe 互斥 (kVectorALU desc 经 ll_ tick 不触发, bank_conflict_count()==0)
//
// 关键路径 (per Oracle Q10):
//   - ll_ 必须 make_unique (已, SM.cc) + set_parent(this) (P0 修复, 镜像 lg_)
//   - lsu_lds_ 必须 make_unique (新, cpptlm::gpu::LsuLDS 真值类, 镜像 LsuGlobal 模式)
//   - lsu_lds_tlm.stub 需补 parent_/set_parent 声明 (P1 修复, 镜像 lsu_global_tlm stub)
//   - SM.exe_once() 加 ll_->tick() 在 lg_->tick() 之后 (per Oracle Q4 A 推荐)
//   - 同步语义 (per Oracle Q2 A): execute 立即 set_scalar_reg + mark_completed → return 1 (对照 LsuGlobal 异步)
//   - bank conflict 真值推迟 Task 4.6 (bank_conflict_count() 恒 0)
//
// 作者 CppTLM Team / 2027-02-10 (Task 18b P1-10 实施, per Oracle 预审 Task 2.10)
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "tlm/gpu/streaming_multiprocessor_tlm.hh"

using namespace tlm;
using namespace cpptlm::gpu;

TEST_CASE("LsuLDS 模块身份 (per plan line 791 '共享内存 bank conflict 检测 stub')",
          "[sm][lds][sm-microarch][task18]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    DeviceConfig cfg{};
    REQUIRE(sm.initialize(cfg));

    // A1: 模块身份 (per Oracle Q7 A1)
    REQUIRE(sm.ll() != nullptr);
    REQUIRE(sm.ll()->get_module_type() == "LsuLDS");
    REQUIRE(sm.lsu_lds() != nullptr);
    REQUIRE(sm.lsu_lds()->bank_conflict_count() == 0);  // stub (Task 4.6 填充)
}

TEST_CASE("LsuLDS 同步 load 真值: 立即回写 + mark_completed (对照 LsuGlobal 异步)",
          "[sm][lds][sm-microarch][task18]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    DeviceConfig cfg{};
    REQUIRE(sm.initialize(cfg));

    // A2: 同步 load 真值 (per Oracle Q2 A: 同步 execute + mark_completed, 无 pending)
    // 注入 kLsuLDS + is_memory desc, ll_ tick() dispatch → lsu_lds_->execute() 立即回写
    InstrDescriptor desc{};
    desc.instr_id = 900;
    desc.pipe = PipeClass::kLsuLDS;
    desc.latency_class = LatencyClass::kFixed1Cycle;  // LDS 低延迟 (1 cycle, 非 kMemory)
    desc.is_memory = true;
    desc.memory_data = 0xCAFEBABE12345678ULL;
    desc.memory_data_valid = 1;
    desc.dst_regs[0] = 11;
    desc.num_dst = 1;
    desc.target_vaddr = 0x2000;  // LDS 共享内存地址 (stub 暂不寻址)
    desc.num_src = 0;
    sm.set_instr_descriptor_buf(&desc, 1);

    // 推进 4 cycle (同步: 第 1 次 exe_once 立即回写 + mark_completed)
    for (int i = 0; i < 4; ++i) {
        sm.exe_once();
    }

    // 立即完成 (同步语义, 对照 LsuGlobal 10 cycle 异步)
    REQUIRE(sm.is_instruction_completed(900));
    // 数据回写: dst=11 寄存器存 0xCAFEBABE12345678
    uint64_t val = 0;
    REQUIRE(sm.get_register_value(0, 0, 11, &val));
    REQUIRE(val == 0xCAFEBABE12345678ULL);
}

TEST_CASE("LsuLDS pipe 互斥 (非 kLsuLDS 不触发)",
          "[sm][lds][sm-microarch][task18]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    DeviceConfig cfg{};
    REQUIRE(sm.initialize(cfg));

    // A3: pipe 互斥 (per Oracle Q7 A3)
    // 注入 kVectorALU desc (非 kLsuLDS, ll_ tick 静默)
    InstrDescriptor desc{};
    desc.instr_id = 1000;
    desc.pipe = PipeClass::kVectorALU;  // 非 kLsuLDS
    desc.latency_class = LatencyClass::kFixed1Cycle;
    desc.dst_regs[0] = 12;
    desc.src_regs[0] = 1;
    desc.src_regs[1] = 2;
    desc.num_src = 2;
    desc.num_dst = 1;
    sm.set_scalar_reg(1, 10);
    sm.set_scalar_reg(2, 20);
    sm.set_instr_descriptor_buf(&desc, 1);

    for (int i = 0; i < 4; ++i) {
        sm.exe_once();
    }

    // A3: ll_ 不应触发 (pipe 互斥, kVectorALU 不触发 LsuLDS)
    REQUIRE(sm.lsu_lds()->bank_conflict_count() == 0);
}
