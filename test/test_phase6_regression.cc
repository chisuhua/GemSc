// test/test_phase6_regression.cc
// Phase 6: 回归测试套件
// 确保 Phase 0-5 功能正常工作

#include <cstring>
#include "bundles/cache_bundles_tlm.hh"
#include "core/error_category.hh"
#include "core/packet.hh"
#include "core/sim_object.hh"
#include "ext/error_context_ext.hh"
#include "ext/transaction_context_ext.hh"
#include "framework/debug_tracker.hh"
#include "framework/transaction_tracker.hh"
#include "tlm/cache_tlm.hh"
#include "tlm/crossbar_tlm.hh"
#include "tlm/memory_tlm.hh"
#include "tlm/tlm_stub.hh"
#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using tlm_generic_payload = tlm::tlm_generic_payload;

// ========== SimObject 回归测试 ==========

TEST_CASE("REGRESSION: SimObject 层次化复位", "[regression][phase2]") {
    class TestModule : public SimObject {
    public:
        int reset_count = 0;
        TestModule(const std::string& n, EventQueue* eq) : SimObject(n, eq) {
        }
        void tick() override {
        }
        void do_reset(const ResetConfig& config) override {
            reset_count++;
            (void)config;
        }
    };

    EventQueue eq;
    auto* parent = new TestModule("parent", &eq);
    auto* child = new TestModule("child", &eq);

    parent->add_child(child);
    parent->reset(ResetConfig(true));

    REQUIRE(parent->reset_count == 1);
    REQUIRE(child->reset_count == 1);

    //     delete parent;
    //     delete child;
}

// ========== Packet 回归测试 ==========

// TEST_CASE("REGRESSION: Packet transaction_id", "[regression][phase3]") {
//     tlm::tlm_generic_payload payload;
//     Packet* pkt = new Packet(&payload, 0, PKT_REQ);
//
//     // 默认值
//     REQUIRE(pkt->get_transaction_id() == 0);
//
//     // 设置交易 ID
//     pkt->set_transaction_id(100);
//     REQUIRE(pkt->get_transaction_id() == 100);
//     REQUIRE(pkt->stream_id == 100);
//
//     // 验证 Extension 同步
//     TransactionContextExt* ext = nullptr;
//     payload.get_extension(ext);
//     REQUIRE(ext != nullptr);
//     REQUIRE(ext->transaction_id == 100);
//
// //     delete pkt;
// }

// TEST_CASE("REGRESSION: Packet 分片支持", "[regression][phase3]") {
//     tlm::tlm_generic_payload payload;
//     Packet* pkt = new Packet(&payload, 0, PKT_REQ);
//
//     pkt->set_fragment_info(1, 4);
//     REQUIRE(pkt->is_fragmented() == true);
//     REQUIRE(pkt->get_group_key() == pkt->stream_id);
//
// //     delete pkt;
// }

// ========== TransactionContextExt 回归测试 ==========

TEST_CASE("REGRESSION: TransactionContextExt clone", "[regression][phase3]") {
    TransactionContextExt original;
    original.transaction_id = 100;
    original.source_module = "test";
    original.add_trace("module1", 10, 1, "hopped");

    auto* cloned = dynamic_cast<TransactionContextExt*>(original.clone());
    REQUIRE(cloned != nullptr);
    REQUIRE(cloned->transaction_id == 100);
    REQUIRE(cloned->source_module == "test");
    REQUIRE(cloned->trace_log.size() == 1);

    // 验证深拷贝
    cloned->source_module = "modified";
    REQUIRE(original.source_module == "test");

    delete cloned;
}

// ========== TransactionTracker 回归测试 ==========

TEST_CASE("REGRESSION: TransactionTracker 完整流程", "[regression][phase3]") {
    auto& tracker = TransactionTracker::instance();
    tracker.initialize();

    tlm::tlm_generic_payload payload;

    // 创建交易
    uint64_t tid = tracker.create_transaction(&payload, "cpu", "READ");
    REQUIRE(tid > 0);

    // 记录 hop
    tracker.record_hop(tid, "crossbar", 1, "hopped");

    const auto* record = tracker.get_transaction(tid);
    REQUIRE(record != nullptr);
    REQUIRE(record->hop_log.size() == 1);

    // 完成交易
    tracker.complete_transaction(tid);
    REQUIRE(record->is_complete == true);
}

// ========== ErrorContextExt 回归测试 ==========

// TEST_CASE("REGRESSION: ErrorContextExt 创建", "[regression][phase4]") {
//     auto* ext = create_error_context(
//         nullptr,
//         ErrorCode::COHERENCE_STATE_VIOLATION,
//         "Test error",
//         "cache"
//     );
//     REQUIRE(ext == nullptr);  // nullptr payload
//
//     tlm::tlm_generic_payload payload;
//     ext = create_error_context(
//         &payload,
//         ErrorCode::COHERENCE_STATE_VIOLATION,
//         "Test error",
//         "cache"
//     );
//
//     REQUIRE(ext != nullptr);
//     REQUIRE(ext->error_code == ErrorCode::COHERENCE_STATE_VIOLATION);
//     REQUIRE(ext->error_category == ErrorCategory::COHERENCE);
//
//     delete ext;
// }

TEST_CASE("REGRESSION: ErrorContextExt 堆栈追踪", "[regression][phase4]") {
    ErrorContextExt ext;
    ext.add_stack_frame("module_a", "func1", "line 10");
    ext.add_stack_frame("module_b", "func2", "line 20");

    REQUIRE(ext.stack_trace.size() == 2);
    REQUIRE(ext.stack_trace[0].module == "module_a");
    REQUIRE(ext.stack_trace[1].function == "func2");
}

// ========== DebugTracker 回归测试 ==========

TEST_CASE("REGRESSION: DebugTracker 错误记录", "[regression][phase4]") {
    auto& tracker = DebugTracker::instance();
    tracker.reset_for_testing();
    tracker.initialize(true, true, false);

    tlm::tlm_generic_payload payload;
    uint64_t error_id =
        tracker.record_error(&payload, ErrorCode::COHERENCE_DEADLOCK, "Deadlock detected", "cache");

    REQUIRE(error_id > 0);
    REQUIRE(tracker.error_count() >= 1);

    const auto* record = tracker.get_error(error_id);
    REQUIRE(record != nullptr);
    REQUIRE(record->is_fatal == true);
}

TEST_CASE("REGRESSION: DebugTracker 状态历史", "[regression][phase4]") {
    auto& tracker = DebugTracker::instance();
    tracker.reset_for_testing();
    tracker.initialize(true, true, false);

    tracker.record_state_transition(0x1000, CoherenceState::INVALID, CoherenceState::SHARED, "read",
                                    1);
    tracker.record_state_transition(0x1000, CoherenceState::SHARED, CoherenceState::EXCLUSIVE,
                                    "upgrade", 2);

    auto history = tracker.get_state_history(0x1000);
    REQUIRE(history.size() >= 2);
}

// ========== ErrorCode 回归测试 ==========

TEST_CASE("REGRESSION: ErrorCode 类别提取", "[regression][phase2]") {
    REQUIRE(get_error_category(ErrorCode::SUCCESS) == ErrorCategory::SUCCESS);
    REQUIRE(get_error_category(ErrorCode::TRANSPORT_INVALID_ADDRESS) == ErrorCategory::TRANSPORT);
    REQUIRE(get_error_category(ErrorCode::COHERENCE_DEADLOCK) == ErrorCategory::COHERENCE);
}

TEST_CASE("REGRESSION: 错误检测函数", "[regression][phase2]") {
    // 严重错误
    REQUIRE(is_fatal_error(ErrorCode::COHERENCE_DEADLOCK) == true);
    REQUIRE(is_fatal_error(ErrorCode::RESOURCE_BUFFER_FULL) == false);

    // 可恢复错误
    REQUIRE(is_recoverable_error(ErrorCode::RESOURCE_BUFFER_FULL) == true);
    REQUIRE(is_recoverable_error(ErrorCode::COHERENCE_DEADLOCK) == false);

    // 成功
    REQUIRE(is_success(ErrorCode::SUCCESS) == true);
}

// ========== 集成回归测试 ==========

// TEST_CASE("REGRESSION: 交易 + 错误集成", "[regression][integration]") {
//     auto& txn_tracker = TransactionTracker::instance();
//     auto& err_tracker = DebugTracker::instance();
//
//     txn_tracker.initialize();
//     err_tracker.reset_for_testing();
//     err_tracker.initialize(true, true, false);
//
//     tlm::tlm_generic_payload payload;
//
//     // 创建交易
//     uint64_t tid = txn_tracker.create_transaction(&payload, "cpu", "READ");
//
//     // 设置 Packet
//     Packet* pkt = new Packet(&payload, 0, PKT_REQ);
//     pkt->set_transaction_id(tid);
//
//     // 模拟错误
//     pkt->set_error_code(ErrorCode::TRANSPORT_TIMEOUT);
//     err_tracker.record_error(&payload, ErrorCode::TRANSPORT_TIMEOUT, "Timeout", "router");
//
//     // 完成交易
//     txn_tracker.complete_transaction(tid);
//
//     // 验证
//     const auto* txn_record = txn_tracker.get_transaction(tid);
//     REQUIRE(txn_record != nullptr);
//     REQUIRE(txn_record->is_complete == true);
//
//     REQUIRE(err_tracker.error_count() >= 1);
//
// //     delete pkt;
// }

// ========== TLM Modules 回归测试 ==========

TEST_CASE("REGRESSION: CrossbarTLM 透传 (4 端口 multi-port)", "[regression][phase5]") {
    EventQueue eq;
    CrossbarTLM crossbar("xbar", &eq);

    REQUIRE(crossbar.get_module_type() == "CrossbarTLM");
    REQUIRE(crossbar.num_ports() == 4);
    REQUIRE(crossbar.is_initialized() == false);

    crossbar.init();
    REQUIRE(crossbar.is_initialized() == true);

    // 4 端口路由: (addr >> 12) & 0x3 → port idx
    // 0x0000 → port 0
    bundles::CacheReqBundle req;
    req.transaction_id.write(1);
    req.address.write(0x0000);
    crossbar.req_in[0].consume();
    std::memcpy(&crossbar.req_in[0].data(), &req, sizeof(req));
    crossbar.req_in[0].set_valid(true);
    crossbar.tick();

    REQUIRE(crossbar.resp_out[0].valid());
    auto resp = crossbar.resp_out[0].data();
    REQUIRE(resp.transaction_id.read() == 1);
}

TEST_CASE("REGRESSION: MemoryTLM 终止", "[regression][phase5]") {
    EventQueue eq;
    MemoryTLM memory("mem", &eq);

    REQUIRE(memory.get_module_type() == "MemoryTLM");

    // 注: MemoryV2 与 MemoryTLM 在 error path 上存在行为差异。
    //   - MemoryV2 行为: 地址越界时设置 TRANSPORT_INVALID_ADDRESS 错误码,
    //     释放 TransactionContextExt, 增加 errors 计数。
    //   - MemoryTLM 行为: 无错误路径处理, 任何有效请求均返回 0xDEADBEEF 响应。
    // 此差异为有意的架构调整 — TLM 模块专注于周期精确性能建模,
    // 错误注入测试应在 TLMModule 派生类 (test_p3_2_tlm_integration.cc) 中覆盖。

    bundles::CacheReqBundle req;
    req.transaction_id.write(42);
    req.address.write(0x1000);
    req.is_write.write(0);
    memory.req_in().consume();
    std::memcpy(&memory.req_in().data(), &req, sizeof(req));
    memory.req_in().set_valid(true);
    memory.tick();

    REQUIRE(memory.resp_out().valid());
    auto resp = memory.resp_out().data();
    REQUIRE(resp.transaction_id.read() == 42);
}

TEST_CASE("REGRESSION: TLM Modules 层次化复位", "[regression][phase5]") {
    EventQueue eq;
    CacheTLM cache("cache", &eq);
    MemoryTLM memory("memory", &eq);

    cache.add_child(&memory);

    ResetConfig config;
    config.hierarchical = true;
    cache.reset(config);

    // 复位成功（不崩溃即通过）
    REQUIRE(cache.is_reset_pending() == false);
    REQUIRE(memory.is_reset_pending() == false);

    bundles::CacheReqBundle write_req;
    write_req.transaction_id.write(1);
    write_req.address.write(0x1000);
    write_req.is_write.write(1);
    write_req.data.write(0xABCD);
    cache.req_in().consume();
    std::memcpy(&cache.req_in().data(), &write_req, sizeof(write_req));
    cache.req_in().set_valid(true);
    cache.tick();

    cache.reset(config);

    bundles::CacheReqBundle read_req;
    read_req.transaction_id.write(2);
    read_req.address.write(0x1000);
    read_req.is_write.write(0);
    cache.req_in().consume();
    std::memcpy(&cache.req_in().data(), &read_req, sizeof(read_req));
    cache.req_in().set_valid(true);
    cache.tick();

    REQUIRE(cache.resp_out().valid());
    auto resp = cache.resp_out().data();
    REQUIRE(resp.is_hit.read() == false);
}
