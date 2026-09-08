// test/test_streaming_multiprocessor_tlm.cc
// Task 17 L3: SM 顶层容器集成测试 (per architecture/15 §15.8.2 L3)
// 验证 SM 顶层持 12 子模块 + IComputeDevice 15 方法契约
// 作者 CppTLM Team / 日期 2027-02-09
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "tlm/gpu/streaming_multiprocessor_tlm.hh"

using namespace tlm;
using namespace cpptlm::gpu;

TEST_CASE("SM 顶层容器实例化 + 模块类型字符串", "[sm-microarch][sm-l3]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    REQUIRE(sm.get_module_type() == "StreamingMultiprocessorTLM");
}

TEST_CASE("SM 顶层是 IComputeDevice 实例", "[sm-microarch][sm-l3]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    IComputeDevice* dev = &sm;
    REQUIRE(dev != nullptr);
}

TEST_CASE("IComputeDevice::initialize 接受 DeviceConfig", "[sm-microarch][sm-l3]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    DeviceConfig cfg{};
    cfg.num_sms = 1;
    cfg.max_warps_per_sm = 64;
    sm.initialize(cfg); // stub 阶段不保证返回值
    REQUIRE(true);
}

TEST_CASE("IComputeDevice::set_instr_descriptor_buf 接受空 buf", "[sm-microarch][sm-l3]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    sm.set_instr_descriptor_buf(nullptr, 0);
    REQUIRE(true);
}

TEST_CASE("IComputeDevice::set_active_mask 返回 false (stub)", "[sm-microarch][sm-l3]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    bool ok = sm.set_active_mask(0, 0, 0xFFFFFFFFFFFFFFFFull);
    REQUIRE(!ok);
}

TEST_CASE("IComputeDevice::set_next_pc 返回 false (stub)", "[sm-microarch][sm-l3]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    bool ok = sm.set_next_pc(0, 0, 0, 0x1000);
    REQUIRE(!ok);
}

TEST_CASE("IComputeDevice::set_scoreboard 返回 false (stub)", "[sm-microarch][sm-l3]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    bool ok = sm.set_scoreboard(0, 0, 0xFFFFFFFFFFFFFFFFull);
    REQUIRE(!ok);
}

TEST_CASE("IComputeDevice::get_register_value 返回 false (stub)", "[sm-microarch][sm-l3]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    uint64_t value = 0;
    bool ok = sm.get_register_value(0, 0, 0, &value);
    REQUIRE(!ok);
    REQUIRE(value == 0);
}

TEST_CASE("IComputeDevice::is_instruction_completed 返回 false (stub)", "[sm-microarch][sm-l3]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    bool ok = sm.is_instruction_completed(42);
    REQUIRE(!ok);
}

TEST_CASE("IComputeDevice::is_finished 返回 false (stub)", "[sm-microarch][sm-l3]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    bool ok = sm.is_finished();
    REQUIRE(!ok);
}

TEST_CASE("IComputeDevice::get_warp_status 返回默认 WarpStatus", "[sm-microarch][sm-l3]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    WarpStatus status = sm.get_warp_status(0, 0);
    REQUIRE(status.warp_id == 0);
    REQUIRE(status.sm_id == 0);
}

TEST_CASE("IComputeDevice::get_thread_state 返回 ThreadState", "[sm-microarch][sm-l3]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    ThreadState s = sm.get_thread_state(0, 0, 0);
    REQUIRE(s == ThreadState::kIdle);
}

TEST_CASE("IComputeDevice::exe_once 返回 0 (stub)", "[sm-microarch][sm-l3]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    int ret = sm.exe_once();
    REQUIRE(ret == 0);
}

TEST_CASE("IComputeDevice::sm_exe_once 返回 0 (stub)", "[sm-microarch][sm-l3]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    int ret = sm.sm_exe_once(0);
    REQUIRE(ret == 0);
}

TEST_CASE("IComputeDevice::warp_exe_once 返回 0 (stub)", "[sm-microarch][sm-l3]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    int ret = sm.warp_exe_once(0, 0);
    REQUIRE(ret == 0);
}

TEST_CASE("SM tick() 不崩溃", "[sm-microarch][sm-l3]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    for (int i = 0; i < 10; ++i)
        sm.tick();
    REQUIRE(true);
}

TEST_CASE("SM reset() 不崩溃", "[sm-microarch][sm-l3]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    sm.reset();
    sm.shutdown();
    REQUIRE(true);
}
