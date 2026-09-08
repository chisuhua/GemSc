// test/test_gpu_standalone.cc
// GPUTLM v0 单元测试（Catch2 v3.7.0）
// 功能描述：验证 GPUTLM 黑盒发起器在 5 个典型场景下行为正确。
// 作者 CppTLM Team / 日期 2026-06-11
// 参考：docs/superpowers/specs/2026-06-11-phase7a-gpu-infra-design.md §4.3
//      docs/superpowers/plans/2026-06-11-phase7a-gpu-infra.md Task 5

#include "chstream_register.hh"
#include "bundles/compute_bundles_tlm.hh"
#include "core/event_queue.hh"
#include "core/module_factory.hh"
#include "framework/chstream_adapter_factory.hh"
#include "modules.hh"
#include "tlm/gpu/gpu_tlm.hh"
#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace tlm;
using namespace bundles;

namespace {

    // 一次性注册所有 ChStream 模块
    void registerChStreamModules() {
        static bool registered = false;
        if (!registered) {
            REGISTER_OBJECT;
            REGISTER_CHSTREAM;
            registered = true;
        }
    }

    // 创建一个绑定到 GPUTLM 的 adapter（手工绑定，不走 ModuleFactory Step 7）
    struct AdapterHandle {
        GPUTLM* gpu;
        cpptlm::StreamAdapter<GPUTLM, ComputeReqBundle, ComputeRespBundle>* adapter;

        AdapterHandle(GPUTLM* g) : gpu(g), adapter(nullptr) {
            // 直接 new adapter（v0 简化版，绕开 ModuleFactory 完整管线）
            adapter = new cpptlm::StreamAdapter<GPUTLM, ComputeReqBundle, ComputeRespBundle>(g);
            g->set_stream_adapter(adapter);
        }
        ~AdapterHandle() {
            if (adapter)
                delete adapter;
        }
    };

} // namespace

TEST_CASE("GPUTLM_Standalone.SendReadRequest", "[gpu]") {
    registerChStreamModules();
    EventQueue eq;
    GPUTLM gpu("gpu0", &eq);
    gpu.set_num_kernels(1);
    gpu.set_num_workgroups(1);
    gpu.set_workgroup_size(64);
    gpu.set_coalescing_factor(64); // 64/64 = 1 request per WG per cycle

    AdapterHandle handle(&gpu);

    // GPUTLM v0: each tick during kernel_duration_ issues 1 batch.
    // 1 kernel × ~99 active cycles × 1 WG × 1 req ≈ 99 requests.
    for (int i = 0; i < 200; ++i)
        gpu.tick();

    REQUIRE(gpu.stats_requests_issued() >= 99);
    REQUIRE(gpu.stats_requests_issued() <= 100);
}

TEST_CASE("GPUTLM_Standalone.SendWriteRequest", "[gpu]") {
    registerChStreamModules();
    EventQueue eq;
    GPUTLM gpu("gpu0", &eq);
    gpu.set_num_kernels(1);
    gpu.set_num_workgroups(2);
    gpu.set_workgroup_size(64);
    gpu.set_coalescing_factor(32); // 64/32 = 2 requests per WG per cycle

    AdapterHandle handle(&gpu);

    for (int i = 0; i < 1000; ++i)
        gpu.tick();

    // 99 batches × 2 WG × 2 req = 396 requests (approx)
    REQUIRE(gpu.stats_requests_issued() >= 390);
    REQUIRE(gpu.stats_requests_issued() <= 400);
    REQUIRE(gpu.stats_writes() + gpu.stats_reads() == gpu.stats_requests_issued());
    // 50% 写概率 + 大量采样：两个都 >=1
    REQUIRE(gpu.stats_writes() >= 1);
    REQUIRE(gpu.stats_reads() >= 1);
}

TEST_CASE("GPUTLM_Standalone.MultiKernel", "[gpu]") {
    registerChStreamModules();
    EventQueue eq;
    GPUTLM gpu("gpu0", &eq);
    gpu.set_num_kernels(3);
    gpu.set_num_workgroups(2);
    gpu.set_workgroup_size(32);
    gpu.set_kernel_duration(50);

    AdapterHandle handle(&gpu);

    // 3 kernels × kernel_duration(50) ticks = 150 active ticks
    for (int i = 0; i < 200; ++i)
        gpu.tick();

    REQUIRE(gpu.stats_kernels_launched() == 3);
    // 49 active ticks per kernel × 3 kernels × 2 WG = 294 workgroups
    REQUIRE(gpu.stats_workgroups_dispatched() >= 290);
    REQUIRE(gpu.stats_workgroups_dispatched() <= 300);
    // 每个 WG × 32 req = many requests
    REQUIRE(gpu.stats_requests_issued() >= 32 * 290);
}

TEST_CASE("GPUTLM_Standalone.CoalescingFactor", "[gpu]") {
    registerChStreamModules();
    EventQueue eq;
    GPUTLM gpu("gpu0", &eq);
    gpu.set_num_kernels(1);
    gpu.set_num_workgroups(1);
    gpu.set_workgroup_size(64);
    gpu.set_coalescing_factor(8); // 64/8 = 8 requests per WG per cycle

    AdapterHandle handle(&gpu);

    for (int i = 0; i < 200; ++i)
        gpu.tick();

    // 99 batches × 1 WG × 8 req = 792 requests
    REQUIRE(gpu.stats_requests_issued() >= 780);
    REQUIRE(gpu.stats_requests_issued() <= 800);
}

TEST_CASE("GPUTLM_Standalone.Reset", "[gpu]") {
    registerChStreamModules();
    EventQueue eq;
    GPUTLM gpu("gpu0", &eq);
    gpu.set_num_kernels(2);
    gpu.set_num_workgroups(1);

    AdapterHandle handle(&gpu);

    for (int i = 0; i < 100; ++i)
        gpu.tick();
    REQUIRE(gpu.stats_requests_issued() > 0);

    gpu.do_reset(ResetConfig{});
    REQUIRE(gpu.stats_requests_issued() == 0);
    REQUIRE(gpu.stats_kernels_launched() == 0);
}