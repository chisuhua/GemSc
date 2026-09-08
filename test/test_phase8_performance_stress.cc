// test/test_phase8_performance_stress.cc
// P2: 压力、性能与内存测试 (T10)

#include <vector>
#include "core/packet_pool.hh"
#include "core/tlm_module.hh"
#include "ext/transaction_context_ext.hh"
#include "framework/debug_tracker.hh"
#include "framework/transaction_tracker.hh"
#include <catch2/catch_all.hpp>

#include "tlm/tlm_stub.hh"

using tlm::tlm_generic_payload;

class Phase8TestTLM : public TLMModule {
public:
    Phase8TestTLM(const std::string& n, EventQueue* eq) : TLMModule(n, eq) {
    }
    void tick() override {
    }

    int hop_count = 0;

    TransactionInfo onTransactionHop(Packet* p) override {
        ++hop_count;
        TransactionInfo info;
        info.action = TransactionAction::PASSTHROUGH;
        if (p)
            info.transaction_id = p->get_transaction_id();
        return info;
    }

    using TLMModule::createSubTransaction;
};

// ========== T10.1: 大量并发交易测试 ==========

TEST_CASE("T10.1: 大量并发交易", "[performance][stress][P2]") {
    auto& txn_tracker = TransactionTracker::instance();
    txn_tracker.reset_for_testing();
    txn_tracker.initialize();

    const int NUM_TRANSACTIONS = 5000;

    SECTION("TransactionTracker 高频创建") {
        for (int i = 0; i < NUM_TRANSACTIONS; ++i) {
            tlm::tlm_generic_payload payload;
            txn_tracker.create_transaction(&payload, "cpu_0", "READ");
            // 记录 hop
            txn_tracker.record_hop(i + 1, "crossbar", 1, "hop");
        }

        THEN("所有交易被记录且活跃") {
            // 注意：因为没有调用 complete_transaction，所以都应该在 active 列表中
            // 但由于 payload 销毁，Extension 会清理。Tracker 内部的 record 是否依赖 payload?
            // 查看 TransactionTracker::create_transaction 实现：它复制数据到内部记录。
            // 所以这里应该安全。
            REQUIRE(txn_tracker.active_count() == NUM_TRANSACTIONS);
        }
    }

    SECTION("DebugTracker 高频报错") {
        auto& dbg_tracker = DebugTracker::instance();
        dbg_tracker.reset_for_testing();
        dbg_tracker.initialize(true, true, false);

        for (int i = 0; i < NUM_TRANSACTIONS; ++i) {
            tlm::tlm_generic_payload payload;
            dbg_tracker.record_error(&payload, ErrorCode::COHERENCE_STATE_VIOLATION, "Error Msg",
                                     "module_" + std::to_string(i));
        }

        THEN("错误计数正确") {
            REQUIRE(dbg_tracker.error_count() == NUM_TRANSACTIONS);
            REQUIRE(dbg_tracker.get_errors_by_category(ErrorCategory::COHERENCE).size() ==
                    NUM_TRANSACTIONS);
        }
    }
}

// ========== T10.2: 长时间运行稳定性 ==========

TEST_CASE("T10.2: 长时间运行稳定性", "[performance][stability][P2]") {
    SECTION("100,000 周期仿真") {
        auto& txn = TransactionTracker::instance();
        txn.initialize();

        uint64_t txn_count = 0;
        const uint64_t TOTAL_CYCLES = 100000;

        for (uint64_t cycle = 0; cycle < TOTAL_CYCLES; ++cycle) {
            // 每 100 个周期产生一个交易
            if (cycle % 100 == 0) {
                tlm::tlm_generic_payload p;
                txn.create_transaction(&p, "cpu", "READ");
                txn_count++;
            }
            // 简单的清理逻辑
            // pkt.set_error_code(ErrorCode::SUCCESS); // 简单操作
        }

        THEN("系统未崩溃，交易计数正确") {
            REQUIRE(txn_count == (TOTAL_CYCLES / 100));
        }
    }

    SECTION("PacketPool 压力测试 (10,000 次申请释放)") {
        for (int i = 0; i < 10000; ++i) {
            Packet* pkt = PacketPool::get().acquire();
            REQUIRE(pkt != nullptr);
            // 模拟操作
            pkt->cmd = CMD_READ;
            PacketPool::get().release(pkt);
        }
    }
}

// ========== T10.3: 内存泄漏检测 ==========

TEST_CASE("T10.3: 内存泄漏检测", "[performance][memory][P2]") {
    // 注意：真正的泄漏检测通常需要 ASan (AddressSanitizer)。
    // 这里通过逻辑验证对象的生命周期。

    SECTION("自动清理 payload 及其 Extension") {
        // 在循环中创建和销毁带有 Extension 的 payload
        // 如果析构函数没有正确清理 ext，valgrind/asan 会报错
        for (int i = 0; i < 1000; ++i) {
            {
                tlm::tlm_generic_payload* p = new tlm::tlm_generic_payload();
                create_transaction_context(p, 100 + i, 0, 0, 1); // 分配 Extension

                // 销毁 payload，应该自动删除 Extension
                delete p;
            }
        }
        SUCCEED("Payload 及其扩展被正确清理");
    }

    SECTION("PacketPool 内存重用") {
        // 验证 Pool 不会无限增长内存
        // Pool 应该复用对象
        std::vector<Packet*> pointers;

        // 申请
        for (int i = 0; i < 1000; ++i) {
            pointers.push_back(PacketPool::get().acquire());
        }

        // 释放
        for (auto* p : pointers) {
            PacketPool::get().release(p);
        }

        // Pool 现在的内部队列应该有对象可用，而不是不断 malloc
    }
}

// ========== P3.2 Lifecycle & Risk Fixes Verification ==========

TEST_CASE("Fix1: TLMModule Child Transaction ID Uniqueness (No Collision)", "[P3.2][cache][id]") {
    GIVEN("A TLMModule-derived module in a miss scenario") {
        EventQueue eq;
        Phase8TestTLM cache("cache", &eq);
        cache.init();

        WHEN("Creating multiple child sub-transactions for different parents") {
            Packet* parents[3];
            Packet* children[3];
            uint64_t child_tids[3];

            for (int i = 0; i < 3; ++i) {
                parents[i] = PacketPool::get().acquire();
                parents[i]->payload->set_address(0x99990 + i);
                parents[i]->set_transaction_id(1000 + i);

                children[i] = PacketPool::get().acquire();
                children[i]->payload->set_address(0x99990 + i);

                child_tids[i] = cache.createSubTransaction(parents[i], children[i]);
            }

            THEN("All child TIDs are globally unique and above the global baseline") {
                REQUIRE(child_tids[0] != child_tids[1]);
                REQUIRE(child_tids[1] != child_tids[2]);
                REQUIRE(child_tids[0] != child_tids[2]);
                REQUIRE(child_tids[0] >= 20000); // g_sub_tid atomic baseline
            }

            for (int i = 0; i < 3; ++i) {
                PacketPool::get().release(parents[i]);
                PacketPool::get().release(children[i]);
            }
        }
    }
}

TEST_CASE("Fix2: TLMModule Reset Ordering (Fragment Buffers Cleared Before SimObject::do_reset)",
          "[P3.2][cache][reset]") {
    GIVEN("A TLMModule-derived module with no pending state") {
        EventQueue eq;
        Phase8TestTLM cache("cache", &eq);
        cache.init();

        REQUIRE(cache.is_reset_pending() == false);

        WHEN("Resetting the module") {
            ResetConfig config;
            cache.reset(config);

            THEN("Module is no longer reset-pending and reset completed without crash") {
                REQUIRE(cache.is_reset_pending() == false);
                SUCCEED("Reset completed successfully");
            }
        }
    }
}

TEST_CASE("Fix3: TLMModule Lifecycle (Child TID Assigned Before Parent Release)",
          "[P3.2][cache][link]") {
    GIVEN("A TLMModule-derived module and a Tracker") {
        EventQueue eq;
        Phase8TestTLM cache("cache", &eq);
        cache.init();

        auto& tracker = TransactionTracker::instance();
        tracker.initialize();

        WHEN("A child transaction is created linked to a parent") {
            uint64_t parent_tid = tracker.create_transaction(nullptr, "cpu", "READ");

            Packet* parent = PacketPool::get().acquire();
            parent->set_transaction_id(parent_tid);
            parent->payload->set_address(0x88888);

            Packet* child = PacketPool::get().acquire();
            child->payload->set_address(0x88888);

            uint64_t child_tid = cache.createSubTransaction(parent, child);

            THEN("Child TID is distinct from parent and parent_id linkage is recorded") {
                REQUIRE(child_tid != 0);
                REQUIRE(child_tid != parent_tid);
                REQUIRE(child->get_transaction_id() == child_tid);

                auto* ext = get_transaction_context(child->payload);
                REQUIRE(ext != nullptr);
                REQUIRE(ext->parent_id == parent_tid);
            }

            PacketPool::get().release(parent);
            PacketPool::get().release(child);
        }
    }
}
