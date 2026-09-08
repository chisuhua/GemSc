// test/test_helper_safety_no_lazy.cc
// P0 死代码清理验证: helper 在 port 缺失时抛清晰错误, 不静默 fallback
// 作者: CppTLM Team / 日期: 2026-06-19
//
// Phase 3 dead-code cleanup: 这些测试验证 connectBus/connectCPUSideBus/connectMemSideBus
// 在 mem_side/cpu_side 端口未注册时应抛出 std::runtime_error (含 "mem_side"/"cpu_side"),
// 不再通过 lazy addUpstreamPort({4}) 静默 fallback。
//
// 当前 (pre-cleanup) 行为: lazy 注册 → 不抛 → 测试应 FAIL (TDD red phase)
// Cleanup 后 (Tasks 3.3-3.5) 预期: 抛 runtime_error → 测试 PASS
#include <stdexcept>
#include <string>
#include "core/event_queue.hh"
#include "core/port_manager.hh"
#include "tlm/cache_tlm.hh"
#include "tlm/crossbar_tlm.hh"
#include <catch2/catch_all.hpp>

// =====================================================================
// Case 1: CacheTLM::connectBus 在 mem_side 未注册时抛清晰错误
// =====================================================================
TEST_CASE("Helper safety: CacheTLM::connectBus throws when mem_side unregistered",
          "[helper_safety]") {
    EventQueue eq;
    CacheTLM cache("cache0", &eq);
    CrossbarTLM bus("bus", &eq);

    // 不预注册 mem_side, 直接调用 connectBus 应抛 (P0 修复: 删 lazy, 改清晰错误)
    REQUIRE_THROWS_AS(cache.connectBus(&bus), std::runtime_error);
    try {
        cache.connectBus(&bus);
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        // 错误信息应明确指出 mem_side port 未注册
        REQUIRE(msg.find("mem_side") != std::string::npos);
    }
}

// =====================================================================
// Case 2: CrossbarTLM::connectCPUSideBus 在 cpu_side 未注册时抛
// =====================================================================
TEST_CASE("Helper safety: CrossbarTLM::connectCPUSideBus throws when cpu_side unregistered",
          "[helper_safety]") {
    EventQueue eq;
    CrossbarTLM xbar("xbar", &eq);
    CrossbarTLM bus("bus", &eq);

    REQUIRE_THROWS_AS(xbar.connectCPUSideBus(&bus), std::runtime_error);
    try {
        xbar.connectCPUSideBus(&bus);
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        REQUIRE(msg.find("cpu_side") != std::string::npos);
    }
}

// =====================================================================
// Case 3: CrossbarTLM::connectMemSideBus 在 mem_side 未注册时抛
// =====================================================================
TEST_CASE("Helper safety: CrossbarTLM::connectMemSideBus throws when mem_side unregistered",
          "[helper_safety]") {
    EventQueue eq;
    CrossbarTLM xbar("xbar", &eq);
    CrossbarTLM bus("bus", &eq);

    REQUIRE_THROWS_AS(xbar.connectMemSideBus(&bus), std::runtime_error);
    try {
        xbar.connectMemSideBus(&bus);
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        REQUIRE(msg.find("mem_side") != std::string::npos);
    }
}
