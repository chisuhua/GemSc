// examples/example_error_handling.cc
// 错误处理示例
// 演示：ErrorContextExt + DebugTracker

#include <iostream>
#include "core/error_category.hh"
#include "core/ext/packet_pool.hh"
#include "core/packet.hh"
#include "core/sim_object.hh"
#include "ext/error_context_ext.hh"
#include "ext/transaction_context_ext.hh"
#include "framework/debug_tracker.hh"

/**
 * 示例目标:
 * 1. 初始化 DebugTracker
 * 2. 模拟错误检测
 * 3. 记录错误
 * 4. 查询错误
 * 5. 状态历史追踪
 */

class MemoryWithError : public SimObject {
public:
    MemoryWithError(const std::string& n, EventQueue* eq, uint64_t size)
        : SimObject(n, eq), memory_size(size) {
    }

    void tick() override {
    }
    std::string get_module_type() const override {
        return "MemoryWithError";
    }

    bool handleRequest(Packet* pkt) {
        uint64_t addr = pkt->payload ? pkt->payload->get_address() : 0;

        if (addr >= memory_size) {
            // 地址错误
            pkt->set_error_code(ErrorCode::TRANSPORT_INVALID_ADDRESS);

            ErrorContextExt* ext = nullptr;
            pkt->payload->get_extension(ext);
            if (!ext) {
                ext = create_error_context(pkt->payload, ErrorCode::TRANSPORT_INVALID_ADDRESS,
                                           "Address out of range: " + std::to_string(addr), name);
            } else {
                ext->error_code = ErrorCode::TRANSPORT_INVALID_ADDRESS;
                ext->error_category = ErrorCategory::TRANSPORT;
                ext->error_message = "Address out of range";
                ext->source_module = name;
                ext->set_context_data("address", addr);
                ext->set_context_data("max_address", memory_size - 1);
            }

            // 记录到 DebugTracker
            DebugTracker::instance().record_error(
                pkt->payload, ErrorCode::TRANSPORT_INVALID_ADDRESS,
                "Address " + std::to_string(addr) + " >= " + std::to_string(memory_size), name);

            return false;
        }

        return true;
    }

private:
    uint64_t memory_size;
};

int main() {
    std::cout << "=== CppTLM v2.0 错误处理示例 ===" << std::endl;

    // ========== 1. 初始化 DebugTracker ==========
    auto& tracker = DebugTracker::instance();
    tracker.initialize(true, true, false);

    std::cout << "[1] DebugTracker 初始化完成" << std::endl;

    // ========== 2. 模拟正常请求 ==========
    EventQueue eq;
    MemoryWithError memory("memory", &eq, 0x10000); // 64KB

    // 通过 PacketPool 获取 Packet(私有构造函数，仅 friend PacketPool 可访问)
    Packet* pkt1 = PacketPool::get().acquire();
    pkt1->payload->set_address(0x1000); // 有效地址

    bool ok1 = memory.handleRequest(pkt1);
    std::cout << "[2] 正常请求 (addr=0x1000): " << (ok1 ? "成功" : "失败") << std::endl;

    // ========== 3. 模拟错误请求 ==========
    Packet* pkt2 = PacketPool::get().acquire();
    pkt2->payload->set_address(0x20000); // 超出范围

    bool ok2 = memory.handleRequest(pkt2);
    std::cout << "[3] 错误请求 (addr=0x20000): " << (ok2 ? "成功" : "失败") << std::endl;

    // ========== 4. 查询错误记录 ==========
    std::cout << "[4] 错误统计:" << std::endl;
    std::cout << "    错误数量：" << tracker.error_count() << std::endl;

    auto errors = tracker.get_errors_by_category(ErrorCategory::TRANSPORT);
    std::cout << "    TRANSPORT 错误：" << errors.size() << std::endl;

    // ========== 5. 查看错误详情 ==========
    if (!errors.empty()) {
        const auto& err = errors[0];
        std::cout << "[5] 错误详情:" << std::endl;
        std::cout << "    错误码：" << error_code_to_string(err.error_code) << std::endl;
        std::cout << "    消息：" << err.error_message << std::endl;
        std::cout << "    模块：" << err.source_module << std::endl;
        std::cout << "    严重：" << (err.is_fatal ? "是" : "否") << std::endl;
    }

    // ========== 6. 状态历史示例 ==========
    tracker.record_state_transition(0x1000, CoherenceState::INVALID, CoherenceState::SHARED,
                                    "read_req", 1);
    tracker.record_state_transition(0x1000, CoherenceState::SHARED, CoherenceState::EXCLUSIVE,
                                    "upgrade", 2);

    auto history = tracker.get_state_history(0x1000);
    std::cout << "[6] 地址 0x1000 状态历史:" << std::endl;
    for (const auto& snap : history) {
        std::cout << "    " << coherence_state_to_string(snap.from_state) << " -> "
                  << coherence_state_to_string(snap.to_state) << " (event: " << snap.event << ")"
                  << std::endl;
    }

    // ========== 清理 ==========
    PacketPool::get().release(pkt1);
    PacketPool::get().release(pkt2);

    std::cout << "\n=== 示例完成 ===" << std::endl;
    return 0;
}
