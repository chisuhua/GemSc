// test/test_sm_hazard_tracker.cc
// Test: HazardTracker 真值 + kVirtualReg/kHardwareCounter (per plan Task 2.13)
//
// 验证 cpptlm::gpu::HazardTracker (per Oracle F-2 P0 签名 (parent)):
//   - A1: vmcnt increment/decrement (×2 增, ×1 减后剩 1)
//   - A2: s_waitcnt vmcnt(N) blocks until vmcnt ≤ N (×2 减后 is_stalled=false)
//   - A3: kVirtualReg RAW hazard (allocate 后 can_allocate=false, release 后 true)
//
// 关键路径 (per Oracle F-2 P0 + P-2):
//   - 两张容器 (vmcnts_ map + allocated_vregs_ set), 同 key codec (镜像 RegFileUnit)
//   - 测试经 SM 构造, 直调 sm.hazard_tracker() accessor (per Oracle F-2 P0 修正)
//
// 作者 CppTLM Team / 2027-02-10 (Task 18b P1-13 实施)
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "tlm/gpu/streaming_multiprocessor_tlm.hh"

using namespace tlm;
using namespace cpptlm::gpu;

TEST_CASE("HazardTracker vmcnt increment/decrement (A1)",
          "[sm-hazard][sm-microarch][task18-p1-13]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    DeviceConfig cfg{};
    REQUIRE(sm.initialize(cfg));

    auto* ht = sm.hazard_tracker();
    REQUIRE(ht != nullptr);

    // increment ×2 → vmcnt=2
    ht->increment_vmcnt(0, 0);
    ht->increment_vmcnt(0, 0);
    REQUIRE(ht->vmcnt(0, 0) == 2);

    // decrement ×1 → vmcnt=1
    ht->decrement_vmcnt(0, 0);
    REQUIRE(ht->vmcnt(0, 0) == 1);

    // decrement ×1 → vmcnt=0 (erase key)
    ht->decrement_vmcnt(0, 0);
    REQUIRE(ht->vmcnt(0, 0) == 0);
}

TEST_CASE("s_waitcnt vmcnt(N) blocks until vmcnt ≤ N (A2)",
          "[sm-hazard][sm-microarch][task18-p1-13]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    DeviceConfig cfg{};
    REQUIRE(sm.initialize(cfg));

    auto* ht = sm.hazard_tracker();

    // increment ×2 → vmcnt=2
    ht->increment_vmcnt(0, 0);
    ht->increment_vmcnt(0, 0);
    REQUIRE(ht->vmcnt(0, 0) == 2);

    // is_stalled_vmcnt(warp, vgpr, 0) → 等待 vmcnt ≤ 0, 此时 vmcnt=2 → true
    REQUIRE(ht->is_stalled_vmcnt(0, 0, 0));
    // is_stalled_vmcnt(warp, vgpr, 1) → 等待 vmcnt ≤ 1, 此时 vmcnt=2 → 仍 true
    REQUIRE(ht->is_stalled_vmcnt(0, 0, 1));
    // is_stalled_vmcnt(warp, vgpr, 2) → 等待 vmcnt ≤ 2, 此时 vmcnt=2 → false
    REQUIRE_FALSE(ht->is_stalled_vmcnt(0, 0, 2));

    // decrement ×2 (per plan v3 修订) → vmcnt=0
    ht->decrement_vmcnt(0, 0);
    ht->decrement_vmcnt(0, 0);
    REQUIRE(!ht->is_stalled_vmcnt(0, 0, 0));
}

TEST_CASE("kVirtualReg RAW hazard: duplicate allocate blocks (A3)",
          "[sm-hazard][sm-microarch][task18-p1-13]") {
    EventQueue eq;
    StreamingMultiprocessorTLM sm("sm0", &eq);
    DeviceConfig cfg{};
    REQUIRE(sm.initialize(cfg));

    auto* ht = sm.hazard_tracker();

    // 初始: vgpr 5 未占用 → can_allocate=true
    REQUIRE(ht->can_allocate(0, 5));
    // allocate 后 → can_allocate=false (RAW 阻塞)
    ht->allocate(0, 5);
    REQUIRE_FALSE(ht->can_allocate(0, 5));
    // release 后 → can_allocate=true 恢复
    ht->release(0, 5);
    REQUIRE(ht->can_allocate(0, 5));

    // per-warp 隔离: warp 0 allocate 不影响 warp 1
    ht->allocate(0, 10);
    REQUIRE_FALSE(ht->can_allocate(0, 10));
    REQUIRE(ht->can_allocate(1, 10));  // warp 1 未占用
    ht->release(0, 10);
}
