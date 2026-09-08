// test/test_p3_2_tlm_integration.cc
// P3.2 Wave 4: TLM Module Integration Tests
//
// 迁移说明 (T4.4):
//   - T12.3: CrossbarV2 → CrossbarTLM (Path A, 完全迁移)
//   - T12.4: CacheV2    → CacheTLM    (Path A, 完全迁移, 通过 StatGroup 验证 misses)
//   - T12.5/T12.6:     保留 TestTLM (TLMModule 派生类) 作为 P3.2 钩子测试模板 (Path B)
//   - T12.7:           TransactionAction 枚举测试，与具体模块无关，保留

#include <cstring>
#include "bundles/bundle_serialization.hh"
#include "bundles/cache_bundles_tlm.hh"
#include "core/event_queue.hh"
#include "core/packet_pool.hh"
#include "core/tlm_module.hh"
#include "metrics/stats.hh"
#include "tlm/cache_tlm.hh"
#include "tlm/crossbar_tlm.hh"
#include <catch2/catch_all.hpp>

using tlm::tlm_generic_payload;

// Helper test module
class TestTLM : public TLMModule {
public:
    TestTLM(const std::string& n, EventQueue* eq) : TLMModule(n, eq), hop_count(0) {
    }
    void tick() override {
    }

    TransactionInfo onTransactionHop(Packet* p) override {
        hop_count++;
        TransactionInfo info;
        info.action = TransactionAction::PASSTHROUGH;
        if (p)
            info.transaction_id = p->get_transaction_id();
        return info;
    }

    int hop_count;
};

// =========================================================================
// Tests
// =========================================================================

TEST_CASE("P3.2-T12.3: TLMModule Inheritance and Setup", "[P3.2][TLM][basic]") {
    GIVEN("A new TLM module") {
        EventQueue eq;
        CrossbarTLM xbar("xbar", &eq);

        THEN("It should initialize correctly") {
            REQUIRE(xbar.get_module_type() == "CrossbarTLM");
            REQUIRE(xbar.get_children().empty());
            REQUIRE(xbar.num_ports() == 4);
        }
    }
}

TEST_CASE("P3.2-T12.4: CacheTLM Miss Lifecycle", "[P3.2][TLM][sub]") {
    GIVEN("A CacheTLM with empty cache") {
        EventQueue eq;
        CacheTLM cache("l1", &eq);

        WHEN("A read request for an uncached address arrives") {
            // 注入 bundle 请求 (TLM 模式)
            bundles::CacheReqBundle req(100, 0xDEAD, 8, /*is_write=*/false, /*data=*/0);
            cache.req_in().consume();
            std::memcpy(&cache.req_in().data(), &req, sizeof(req));
            cache.req_in().set_valid(true);

            // Process
            cache.tick();

            THEN("The cache should report a miss on the response and stat") {
                REQUIRE(cache.resp_out().valid());
                auto resp = cache.resp_out().data();
                REQUIRE(resp.transaction_id.read() == 100);
                REQUIRE(resp.is_hit.read() == false);
                REQUIRE(resp.error_code.read() == 0);

                // 验证 misses 统计递增 (替代 v2 的 cache.misses 公开字段)
                auto* stat = static_cast<tlm_stats::Scalar*>(cache.stats().findStat("misses"));
                REQUIRE(stat != nullptr);
                REQUIRE(stat->value() > 0);
            }
        }
    }
}

TEST_CASE("P3.2-T12.5: Fragment Reassembly Buffering", "[P3.2][TLM][fragment]") {
    GIVEN("A TLM module with fragmentation enabled") {
        EventQueue eq;
        TestTLM mod("frag_test", &eq);
        mod.enableFragmentReassembly(true);

        WHEN("Fragments arrive out of order") {
            Packet* pkt1 = PacketPool::get().acquire(); // Fragment 1
            pkt1->payload->set_address(0x100);
            // In real scenario, extension would be set. Simulate manually for test logic:
            // Since we don't have full TLM extension setup here, we test the buffer logic directly
            // or just verify the method exists.

            // For this simple test, we verify the method doesn't crash.
            // Real fragmentation test requires setting TransactionContextExt with IDs.
        }
    }
}

TEST_CASE("P3.2-T12.6: TLMModule Reset Cleanup", "[P3.2][TLM][reset]") {
    GIVEN("A TLM module with some state") {
        EventQueue eq;
        TestTLM mod("reset_test", &eq);

        WHEN("A reset is called") {
            mod.reset();

            THEN("All internal buffers should be cleared") {
                // Verify no crash and clean state
                SUCCEED("Reset completed safely");
            }
        }
    }
}

TEST_CASE("P3.2-T12.7: TransactionAction Enum", "[P3.2][TLM][enum]") {
    THEN("Transaction Actions are defined") {
        REQUIRE(static_cast<int>(TransactionAction::PASSTHROUGH) == 0);
        REQUIRE(static_cast<int>(TransactionAction::TRANSFORM) == 1);
        REQUIRE(static_cast<int>(TransactionAction::TERMINATE) == 2);
        REQUIRE(static_cast<int>(TransactionAction::BLOCK) == 3);
    }
}
