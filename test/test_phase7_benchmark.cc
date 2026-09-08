// test/test_phase7_benchmark.cc
// Phase 7 性能基准测试：CacheTLM tick 延迟
// 作者 CppTLM Team
// 日期 2026-04-13
#include <chrono>
#include <cstdint>
#include "bundles/cache_bundles_tlm.hh"
#include "catch_amalgamated.hpp"
#include "core/chstream_module.hh"
#include "core/chstream_port.hh"
#include "core/module_factory.hh"
#include "framework/chstream_adapter_factory.hh"
#include "framework/stream_adapter.hh"
#include "tlm/cache_tlm.hh"

namespace {
    auto _register_bench = []() {
        ModuleFactory::registerObject<CacheTLM>("CacheTLM");
        ChStreamAdapterFactory::get()
            .registerAdapter<CacheTLM, bundles::CacheReqBundle, bundles::CacheRespBundle>(
                "CacheTLM");
        return 0;
    }();
} // namespace

TEST_CASE("Phase 7: Benchmark — CacheTLM tick latency", "[phase7][benchmark]") {
    EventQueue eq;
    CacheTLM cache("cache_bench", &eq);

    const int iterations = 100000;

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; i++) {
        cache.tick();
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    double avg_ns = (static_cast<double>(elapsed) / iterations) * 1000.0;
    WARN("CacheTLM avg tick latency: " << avg_ns << " ns/op (" << iterations << " iterations)");

    CHECK(avg_ns < 1000.0);
}
