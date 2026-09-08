// test/test_i_compute_device_interface.cc
// IComputeDevice + StreamingMultiprocessorTLM 单元测试 (per plan Task 4 Step 1+4)
//
// 验证:
//   1. IComputeDevice 是抽象类 (std::is_abstract_v == true)
//   2. InstrDescriptor 字段 (isa_type, instr_id, result_value, memory_data)
//   3. StreamingMultiprocessorTLM 派生自 IComputeDevice (15 方法可调用)
//   4. get_thread_state 返回 ThreadState 枚举值
//
// 作者 CppTLM Team / 日期 2027-02-09 (SM 重构 Task 4)
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "tlm/gpu/i_compute_device.hh"
#include "tlm/gpu/instruction_descriptor.hh"
#include "tlm/gpu/streaming_multiprocessor_tlm.hh"

using namespace cpptlm::gpu;

TEST_CASE("IComputeDevice is abstract (has pure virtual methods)", "[icompute][sm-microarch]") {
    STATIC_REQUIRE(std::is_abstract_v<IComputeDevice>);
}

TEST_CASE("InstrDescriptor ISA discriminator + instr_id fields exist", "[icompute][sm-microarch]") {
    InstrDescriptor desc{};
    desc.isa_type = cpptlm::gpu::IsaType::kCDNA64;
    desc.instr_id = 42;
    REQUIRE(desc.isa_type == cpptlm::gpu::IsaType::kCDNA64);
    REQUIRE(desc.instr_id == 42);
}

TEST_CASE("InstrDescriptor result_value[] holds SM-computed timing truth",
          "[icompute][sm-microarch]") {
    InstrDescriptor desc{};
    desc.result_value[0] = 0xDEADBEEFCAFEBABEull;
    desc.result_value[1] = 0x1234567890ABCDEFull;
    REQUIRE(desc.result_value[0] == 0xDEADBEEFCAFEBABEull);
    REQUIRE(desc.result_value[1] == 0x1234567890ABCDEFull);
}

TEST_CASE("ThreadState enum matches IPtxEmuDevice::ThreadState", "[icompute][sm-microarch]") {
    REQUIRE(static_cast<uint32_t>(ThreadState::kIdle) == 0u);
    REQUIRE(static_cast<uint32_t>(ThreadState::kRun) == 1u);
    REQUIRE(static_cast<uint32_t>(ThreadState::kExit) == 2u);
    REQUIRE(static_cast<uint32_t>(ThreadState::kBarSync) == 3u);
}

TEST_CASE("StreamingMultiprocessorTLM has module type", "[icompute][sm-microarch]") {
    EventQueue eq;
    tlm::StreamingMultiprocessorTLM sm("sm0", &eq);
    REQUIRE(sm.get_module_type() == "StreamingMultiprocessorTLM");
}

TEST_CASE("StreamingMultiprocessorTLM IComputeDevice 15 methods callable (stub returns defaults)",
          "[icompute][sm-microarch]") {
    EventQueue eq;
    tlm::StreamingMultiprocessorTLM sm("sm0", &eq);

    // 11 preserved
    DeviceConfig cfg{};
    REQUIRE(sm.initialize(cfg) == true); // Task 1.3 P1-3: ScalarALU 真值 (per plan)
    sm.shutdown();
    REQUIRE(sm.exe_once() == 0);
    REQUIRE(sm.sm_exe_once(0) == 0);
    REQUIRE(sm.warp_exe_once(0, 0) == 0);
    REQUIRE(sm.set_scoreboard(0, 0, 0xFF) == false);
    REQUIRE(sm.get_thread_state(0, 0, 0) == ThreadState::kIdle);
    REQUIRE(sm.set_active_mask(0, 0, 0xFF) == false);
    REQUIRE(sm.set_next_pc(0, 0, 0, 0x1000) == false);
    WarpStatus ws = sm.get_warp_status(0, 0);
    REQUIRE(ws.warp_id == 0);
    REQUIRE(ws.sm_id == 0);
    REQUIRE(sm.is_finished() == false);

    // 1 HSK-9 new
    InstrDescriptor desc{};
    sm.set_instr_descriptor_buf(&desc, 1);

    // 2 Round 4 new
    uint64_t out_val = 0;
    REQUIRE(sm.get_register_value(0, 0, 0, &out_val) == false);
    REQUIRE(sm.is_instruction_completed(42) == false);

    // 1 reset
    sm.reset();
}

TEST_CASE("StreamingMultiprocessorTLM tick() stub no-op", "[icompute][sm-microarch]") {
    EventQueue eq;
    tlm::StreamingMultiprocessorTLM sm("sm0", &eq);
    sm.tick(); // 不应抛异常
    REQUIRE(true);
}

// HSK-9 OpenSpec change hsk9-icompute-api-v1-consumer-pinning Task 1.5:
// 编译期 + 运行期验证 ICOMPUTE_API_VERSION 常量值
TEST_CASE("IComputeDevice ICOMPUTE_API_VERSION is 1", "[icompute][hsk9][sm-microarch]") {
    STATIC_REQUIRE(ICOMPUTE_API_VERSION == 1);
    REQUIRE(ICOMPUTE_API_VERSION == 1);
}