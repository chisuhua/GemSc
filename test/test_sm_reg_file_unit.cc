// test/test_sm_reg_file_unit.cc
// Test: RegFileUnit 真值 + scalar_regs_ 迁移 + P2-4 cycles 锁定 (per plan Task 2.11)
//
// 验证 StreamingMultiprocessorTLM 经 IComputeDevice 接口 + RegFileUnit 真值:
//   - A1: cpptlm::gpu::RegFileUnit::write/read round-trip (warp 0)
//   - A2: Per-warp isolation (warp 0 vs warp 1 reg 5 — 不同值; warp 0 write 不泄漏到 warp 1)
//   - A3: P2-4 LOCK — exe_once() 返回 1, 即使 scalar_alu_->execute() 返回 4 (kFixed4Cycle IMAD)
//
// 关键路径:
//   - SM-owns-state: RegFileUnit 持寄存器唯一真值源 (per architecture/15 §15.5.6)
//   - scalar_regs_ 已在 Task 2.11 迁移到 RegFileUnit 真值 (per Oracle F-1 P0 修复)
//   - facade 委托保留 unset 语义: get_scalar_reg → 0, get_register_value → false
//
// 作者 CppTLM Team / 2027-02-10 (Task 18b P1-11 实施)
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "tlm/gpu/streaming_multiprocessor_tlm.hh"

using namespace tlm;
using namespace cpptlm::gpu;

TEST_CASE("RegFileUnit truth class write/read round-trip (A1)",
          "[sm-regfile][sm-microarch][task18-p1-11]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    DeviceConfig cfg{};
    REQUIRE(sm.initialize(cfg));

    // 经 facade 写入 (warp 0 default)
    sm.set_scalar_reg(7, 0xCAFE);
    sm.set_scalar_reg(8, 0xBABE);

    // 经 IComputeDevice::get_register_value 读回 (warp_id=0, 必须保留 set→true 语义)
    uint64_t val7 = 0, val8 = 0;
    REQUIRE(sm.get_register_value(0, 0, 7, &val7));
    REQUIRE(sm.get_register_value(0, 0, 8, &val8));
    REQUIRE(val7 == 0xCAFE);
    REQUIRE(val8 == 0xBABE);

    // 经 RegFileUnit 真值类直读 (验证 facade 与真值一致性)
    REQUIRE(sm.reg_file()->read(0, 7, &val7));
    REQUIRE(val7 == 0xCAFE);
    REQUIRE(sm.reg_file()->read(0, 8, &val8));
    REQUIRE(val8 == 0xBABE);
}

TEST_CASE("RegFileUnit per-warp isolation (A2)", "[sm-regfile][sm-microarch][task18-p1-11]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    DeviceConfig cfg{};
    REQUIRE(sm.initialize(cfg));

    // Warp 0 写入 reg 5 = 0xAA (经 facade, 默认 warp 0)
    sm.set_scalar_reg(5, 0xAA);

    // Warp 1 写入 reg 5 = 0xBB (经真值类直写, 验证 reg_file() accessor + per-warp key)
    sm.reg_file()->write(1, 5, 0xBB);

    // 读 warp 0: 0xAA
    uint64_t v0 = 0;
    REQUIRE(sm.get_register_value(0, 0, 5, &v0));
    REQUIRE(v0 == 0xAA);

    // 读 warp 1: 0xBB (per-warp 隔离, 不应读到 warp 0 的 0xAA)
    uint64_t v1 = 0;
    REQUIRE(sm.get_register_value(0, 1, 5, &v1));
    REQUIRE(v1 == 0xBB);

    // 读 warp 2 reg 9 (未写入): false + 不修改 v
    uint64_t v2 = 0xDEADBEEF;
    REQUIRE_FALSE(sm.get_register_value(0, 2, 9, &v2));
    REQUIRE(v2 == 0xDEADBEEF); // unset 语义: 不写入 out_value
}

TEST_CASE("exe_once returns 1 regardless of ScalarALU::execute cycle count (P2-4 lock, A3)",
          "[sm-regfile][sm-microarch][task18-p1-11][p2-4-lock]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    DeviceConfig cfg{};
    REQUIRE(sm.initialize(cfg));

    // 注入 kFixed4Cycle IMAD — scalar_alu_->execute() 返回 4 (4 cycle 指令)
    // P2-4 关注: 即使 execute 返回 4 cycle, SM.exe_once() 每次调用只推进 1 cycle (即返回 1)
    // 这是 Task 2.13 HazardTracker 上线前必须锁定的契约 (否则 vmcnt cycle 计数失真)
    InstrDescriptor desc{};
    desc.instr_id = 99;
    desc.pipe = PipeClass::kScalarALU;
    desc.latency_class = LatencyClass::kFixed4Cycle;
    desc.src_regs[0] = 1;
    desc.src_regs[1] = 2;
    desc.dst_regs[0] = 3;
    desc.num_src = 2;
    desc.num_dst = 1;
    sm.set_scalar_reg(1, 100);
    sm.set_scalar_reg(2, 200);
    sm.set_instr_descriptor_buf(&desc, 1);

    // 第一次 exe_once: ring 非空, 应返回 1 (scalar_alu_->execute() 返回 4 但 exe_once 仍 1)
    // 这是 P2-4 契约的核心: 即使多 cycle 指令 in-flight, exe_once 每次只推进 1 cycle
    // (Task 2.13 HazardTracker 上线前必须锁定, 否则 vmcnt cycle 计数失真)
    int ret1 = sm.exe_once();
    REQUIRE(ret1 == 1);

    // IMAD 应已完成 (scalar_alu 真值对 kFixed4Cycle 的当前实现: 立即处理, 不阻塞 ring)
    // 实际 drain 由 scalar_alu.execute() 内部完成 (per Task 2.5 实现)
    REQUIRE(sm.is_instruction_completed(99));
}
