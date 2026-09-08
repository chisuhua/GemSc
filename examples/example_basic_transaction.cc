// examples/example_basic_transaction.cc
// 基础交易追踪示例
// 演示：TransactionContextExt + TransactionTracker

#include <iostream>
#include "core/packet.hh"
#include "core/packet_pool.hh"
#include "core/sim_object.hh"
#include "ext/transaction_context_ext.hh"
#include "framework/transaction_tracker.hh"

/**
 * 示例目标:
 * 1. 初始化 TransactionTracker
 * 2. 创建交易
 * 3. 设置 Packet transaction_id
 * 4. 记录hop延迟
 * 5. 完成交易
 * 6. 查询交易记录
 */

int main() {
    std::cout << "=== CppTLM v2.0 基础交易追踪示例 ===" << std::endl;

    // ========== 1. 初始化 TransactionTracker ==========
    auto& tracker = TransactionTracker::instance();
    tracker.initialize();

    std::cout << "[1] TransactionTracker 初始化完成" << std::endl;

    // ========== 2. 创建交易 ==========
    tlm::tlm_generic_payload payload;
    uint64_t tid = tracker.create_transaction(&payload, "cpu_0", "READ");

    std::cout << "[2] 创建交易 ID: " << tid << std::endl;

    // ========== 3. 设置 Packet ==========
    // Packet 构造函数为 private，仅 PacketPool (friend) 可创建。
    // 正确用法：acquire() 获取，release() 归还。
    Packet* pkt = PacketPool::get().acquire();
    // 将 tracker 创建的 payload 与 Packet 关联（payload 字段为 public）
    pkt->payload = &payload;
    pkt->set_transaction_id(tid);

    std::cout << "[3] Packet 设置 transaction_id: " << pkt->get_transaction_id() << std::endl;

    // ========== 4. 记录 hop 延迟 ==========
    tracker.record_hop(tid, "crossbar", 1, "hopped");
    tracker.record_hop(tid, "cache", 5, "hit");

    std::cout << "[4] 记录 2 次 hop (crossbar: 1 cycle, cache: 5 cycles)" << std::endl;

    // ========== 5. 完成交易 ==========
    tracker.complete_transaction(tid);

    std::cout << "[5] 交易完成" << std::endl;

    // ========== 6. 查询交易记录 ==========
    const auto* record = tracker.get_transaction(tid);
    if (record) {
        std::cout << "[6] 交易记录:" << std::endl;
        std::cout << "    源模块：" << record->source_module << std::endl;
        std::cout << "    类型：" << record->type << std::endl;
        std::cout << "    状态：" << (record->is_complete ? "完成" : "活跃") << std::endl;
        std::cout << "    Hop 日志:" << std::endl;
        for (const auto& [module, latency] : record->hop_log) {
            std::cout << "      - " << module << ": " << latency << " cycles" << std::endl;
        }
    }

    // ========== 7. 统计信息 ==========
    std::cout << "[7] 统计信息:" << std::endl;
    std::cout << "    活跃交易：" << tracker.active_count() << std::endl;
    std::cout << "    完成交易：" << tracker.completed_count() << std::endl;

    // ========== 清理 ==========
    PacketPool::get().release(pkt);

    std::cout << "\n=== 示例完成 ===" << std::endl;
    return 0;
}
