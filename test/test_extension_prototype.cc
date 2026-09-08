// test/test_extension_prototype.cc
// Phase 0: TransactionContextExt 原型验证测试

#include <cstring>
#include "ext/transaction_context_ext.hh"
#include "tlm/tlm_stub.hh"
#include <catch2/catch_all.hpp>

/**
 * Phase 0 测试目标：
 * 1. TransactionContextExt 可正确创建
 * 2. clone() 方法返回正确的深拷贝
 * 3. copy_from() 正确复制所有字段
 * 4. 辅助方法 (is_root, is_fragmented 等) 正确工作
 */

TEST_CASE("TransactionContextExt 基础构造", "[extension][prototype]") {
    SECTION("默认构造") {
        TransactionContextExt ext;

        REQUIRE(ext.transaction_id == 0);
        REQUIRE(ext.parent_id == 0);
        REQUIRE(ext.fragment_id == 0);
        REQUIRE(ext.fragment_total == 1);
        REQUIRE(ext.source_module.empty());
        REQUIRE(ext.type.empty());
        REQUIRE(ext.priority == 0);
        REQUIRE(ext.trace_log.empty());
    }

    SECTION("字段赋值") {
        TransactionContextExt ext;
        ext.transaction_id = 100;
        ext.parent_id = 0;
        ext.fragment_id = 0;
        ext.fragment_total = 1;
        ext.source_module = "cpu_0";
        ext.type = "READ";
        ext.priority = 3;

        REQUIRE(ext.transaction_id == 100);
        REQUIRE(ext.source_module == "cpu_0");
        REQUIRE(ext.type == "READ");
        REQUIRE(ext.priority == 3);
    }
}

TEST_CASE("TransactionContextExt clone 方法", "[extension][prototype]") {
    GIVEN("一个已设置的扩展对象") {
        TransactionContextExt original;
        original.transaction_id = 100;
        original.parent_id = 0;
        original.fragment_id = 0;
        original.fragment_total = 1;
        original.source_module = "test_module";
        original.type = "WRITE";
        original.priority = 5;
        original.add_trace("module1", 10, 2, "hopped");
        original.add_trace("module2", 20, 3, "blocked");

        WHEN("调用 clone()") {
            auto* cloned_ptr = original.clone();

            THEN("返回非空指针") {
                REQUIRE(cloned_ptr != nullptr);

                AND_THEN("类型正确") {
                    auto* cloned = dynamic_cast<TransactionContextExt*>(cloned_ptr);
                    REQUIRE(cloned != nullptr);

                    AND_THEN("所有字段匹配") {
                        REQUIRE(cloned->transaction_id == 100);
                        REQUIRE(cloned->parent_id == 0);
                        REQUIRE(cloned->fragment_id == 0);
                        REQUIRE(cloned->fragment_total == 1);
                        REQUIRE(cloned->source_module == "test_module");
                        REQUIRE(cloned->type == "WRITE");
                        REQUIRE(cloned->priority == 5);
                        REQUIRE(cloned->trace_log.size() == 2);
                    }

                    AND_THEN("trace_log 深拷贝") {
                        auto* cloned = dynamic_cast<TransactionContextExt*>(cloned_ptr);
                        cloned->trace_log[0].module = "modified";
                        REQUIRE(original.trace_log[0].module == "module1");
                        REQUIRE(cloned->trace_log[0].module == "modified");
                    }
                }
            }

            // 清理
        }
    }
}

TEST_CASE("TransactionContextExt copy_from 方法", "[extension][prototype]") {
    GIVEN("两个扩展对象，一个有数据，一个为空") {
        TransactionContextExt source;
        source.transaction_id = 200;
        source.parent_id = 100;
        source.fragment_id = 1;
        source.fragment_total = 4;
        source.source_module = "cache";
        source.type = "READ";
        source.priority = 7;
        source.add_trace("router", 5, 1, "hopped");

        TransactionContextExt dest;

        WHEN("调用 copy_from") {
            dest.copy_from(source);

            THEN("所有字段被复制") {
                REQUIRE(dest.transaction_id == 200);
                REQUIRE(dest.parent_id == 100);
                REQUIRE(dest.fragment_id == 1);
                REQUIRE(dest.fragment_total == 4);
                REQUIRE(dest.source_module == "cache");
                REQUIRE(dest.type == "READ");
                REQUIRE(dest.priority == 7);
                REQUIRE(dest.trace_log.size() == 1);
            }
        }
    }
}

TEST_CASE("TransactionContextExt 辅助方法", "[extension][prototype]") {
    SECTION("根交易判断") {
        TransactionContextExt ext;
        ext.parent_id = 0;
        ext.fragment_total = 1;
        REQUIRE(ext.is_root() == true);
        REQUIRE(ext.is_fragmented() == false);
    }

    SECTION("父子交易判断") {
        TransactionContextExt ext;
        ext.parent_id = 100;
        ext.fragment_total = 1;
        REQUIRE(ext.is_root() == false);
        REQUIRE(ext.is_fragmented() == false);
    }

    SECTION("分片交易判断 - 第一个分片") {
        TransactionContextExt ext;
        ext.parent_id = 100;
        ext.fragment_id = 0;
        ext.fragment_total = 4;
        REQUIRE(ext.is_fragmented() == true);
        REQUIRE(ext.is_first_fragment() == true);
        REQUIRE(ext.is_last_fragment() == false);
    }

    SECTION("分片交易判断 - 最后一个分片") {
        TransactionContextExt ext;
        ext.parent_id = 100;
        ext.fragment_id = 3;
        ext.fragment_total = 4;
        REQUIRE(ext.is_fragmented() == true);
        REQUIRE(ext.is_first_fragment() == false);
        REQUIRE(ext.is_last_fragment() == true);
    }

    SECTION("分组键计算") {
        TransactionContextExt root;
        root.transaction_id = 50;
        root.parent_id = 0;
        REQUIRE(root.get_group_key() == 50);

        TransactionContextExt child;
        child.transaction_id = 51;
        child.parent_id = 50;
        REQUIRE(child.get_group_key() == 50);
    }
}

TEST_CASE("TransactionContextExt reset 方法", "[extension][prototype]") {
    GIVEN("一个已设置的扩展对象") {
        TransactionContextExt ext;
        ext.transaction_id = 100;
        ext.parent_id = 50;
        ext.fragment_id = 2;
        ext.fragment_total = 4;
        ext.source_module = "test";
        ext.type = "WRITE";
        ext.priority = 3;
        ext.add_trace("m1", 10, 1, "event1");

        WHEN("调用 reset()") {
            ext.reset();

            THEN("所有字段重置为默认值") {
                REQUIRE(ext.transaction_id == 0);
                REQUIRE(ext.parent_id == 0);
                REQUIRE(ext.fragment_id == 0);
                REQUIRE(ext.fragment_total == 1);
                REQUIRE(ext.source_module.empty());
                REQUIRE(ext.type.empty());
                REQUIRE(ext.priority == 0);
                REQUIRE(ext.trace_log.empty());
            }
        }
    }
}

TEST_CASE("便捷函数测试", "[extension][prototype]") {
    GIVEN("一个 tlm_generic_payload 对象") {
        tlm::tlm_generic_payload payload;

        SECTION("get_transaction_context - 未设置时") {
            TransactionContextExt* ext = get_transaction_context(&payload);
            REQUIRE(ext == nullptr);
        }

        SECTION("set_transaction_context") {
            TransactionContextExt source;
            source.transaction_id = 300;
            source.source_module = "memory";

            set_transaction_context(&payload, source);

            TransactionContextExt* retrieved = get_transaction_context(&payload);
            REQUIRE(retrieved != nullptr);
            REQUIRE(retrieved->transaction_id == 300);
            REQUIRE(retrieved->source_module == "memory");
        }

        SECTION("create_transaction_context") {
            TransactionContextExt* ext = create_transaction_context(&payload,
                                                                    400, // tid
                                                                    0,   // parent_id
                                                                    0,   // fragment_id
                                                                    1    // fragment_total
            );

            REQUIRE(ext != nullptr);
            REQUIRE(ext->transaction_id == 400);
            REQUIRE(ext->parent_id == 0);
            REQUIRE(ext->fragment_id == 0);
            REQUIRE(ext->fragment_total == 1);
        }

        SECTION("create_transaction_context - 分片交易") {
            TransactionContextExt* ext = create_transaction_context(&payload,
                                                                    501, // tid
                                                                    500, // parent_id
                                                                    1,   // fragment_id
                                                                    4    // fragment_total
            );

            REQUIRE(ext != nullptr);
            REQUIRE(ext->transaction_id == 501);
            REQUIRE(ext->parent_id == 500);
            REQUIRE(ext->fragment_id == 1);
            REQUIRE(ext->fragment_total == 4);
            REQUIRE(ext->is_fragmented() == true);
            REQUIRE(ext->is_first_fragment() == false);
        }
    }
}

TEST_CASE("TLM Extension 机制集成", "[extension][tlm]") {
    GIVEN("一个 payload 和多个交易上下文") {
        tlm::tlm_generic_payload payload1;
        tlm::tlm_generic_payload payload2;

        WHEN("为不同 payload 设置独立上下文") {
            auto* ext1 = create_transaction_context(&payload1, 100, 0, 0, 1);
            auto* ext2 = create_transaction_context(&payload2, 200, 100, 0, 4);

            THEN("两个 payload 独立管理") {
                auto* retrieved1 = get_transaction_context(&payload1);
                auto* retrieved2 = get_transaction_context(&payload2);

                REQUIRE(retrieved1 != nullptr);
                REQUIRE(retrieved2 != nullptr);
                REQUIRE(retrieved1->transaction_id != retrieved2->transaction_id);
                REQUIRE(retrieved1->parent_id != retrieved2->parent_id);

                // ext1 是根交易
                REQUIRE(retrieved1->is_root() == true);

                // ext2 是分片交易
                REQUIRE(retrieved2->is_root() == false);
                REQUIRE(retrieved2->is_fragmented() == true);
            }

            // 清理
        }
    }
}
