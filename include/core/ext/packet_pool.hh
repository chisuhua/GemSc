// packet_pool.hh
#ifndef PACKET_POOL_HH
#define PACKET_POOL_HH

#include "packet.hh"
#include <queue>
#include <mutex>

/**
 * @brief 对象池模式：高效复用 Packet 和 tlm_generic_payload
 * 支持多线程安全（可选）
 */
class PacketPool {
private:
    std::queue<Packet*> m_packet_freelist;
    std::queue<tlm::tlm_generic_payload*> m_payload_freelist;

    mutable std::mutex m_mutex;
    size_t m_max_size;
    size_t m_peak_usage;
    size_t m_current_usage;

    // 私有构造函数（单例）
    PacketPool(size_t max_size = 1024)
        : m_max_size(max_size)
        , m_peak_usage(0)
        , m_current_usage(0)
    {}

    // 创建新的 Packet（内部使用）
    Packet* new_packet() {
        auto* pkt = new Packet(nullptr, 0, PKT_REQ);
        pkt->ref_count = 0; // 初始化引用计数
        return pkt;
    }

    // 创建新的 payload
    tlm::tlm_generic_payload* new_payload() {
        return new tlm::tlm_generic_payload();
    }

    // 增加引用计数
    void add_ref(Packet* pkt) {
        if (pkt) {
            pkt->ref_count++;
        }
    }

    // 减少引用计数，如果为0则回收
    void remove_ref(Packet* pkt) {
        if (!pkt || pkt->ref_count <= 0) return;

        pkt->ref_count--;
        if (pkt->ref_count == 0) {
            // 当引用计数归零时，才真正释放资源
            release(pkt);
        }
    }

public:
    // --- 单例访问 ---
    static PacketPool& get() {
        static PacketPool instance;
        return instance;
    }

    // --- 获取资源 ---
    Packet* acquire() {
        std::lock_guard<std::mutex> lock(m_mutex);

        Packet* pkt = nullptr;
        if (!m_packet_freelist.empty()) {
            pkt = m_packet_freelist.front();
            m_packet_freelist.pop();
        } else {
            pkt = new_packet();
        }

        // 绑定 payload
        if (!m_payload_freelist.empty()) {
            pkt->payload = m_payload_freelist.front();
            m_payload_freelist.pop();
            pkt->payload->reset(); // 重置 TLM 状态
        } else {
            pkt->payload = new_payload();
        }

        pkt->reset(); // 重置 Packet 状态
        m_current_usage++;
        m_peak_usage = std::max(m_peak_usage, m_current_usage);

        return pkt;
    }

    // --- 便捷工厂方法已移除，因为返回 std::unique_ptr 会导致析构问题 ---
    // 使用 acquire() 和手动设置字段代替

    // --- 释放资源 ---
    void release(Packet* pkt) {
        if (!pkt) return;

        std::lock_guard<std::mutex> lock(m_mutex);

        // 首先处理原始请求的引用
        if (pkt->original_req != pkt) { // 不是自己
            remove_ref(pkt->original_req); // 减少对原始请求的引用
        }
        pkt->original_req = nullptr;

        // 处理 payload
        if (pkt->payload) {
            if (m_payload_freelist.size() < m_max_size) {
                m_payload_freelist.push(pkt->payload);
            } else {
                delete pkt->payload;
            }
            pkt->payload = nullptr;
        }

        // 重置并放回 freelist
        pkt->reset();
        if (m_packet_freelist.size() < m_max_size) {
            m_packet_freelist.push(pkt);
        } else {
            delete pkt;
        }

        m_current_usage--;
    }

    // --- 统计信息 ---
    size_t peak_usage() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_peak_usage;
    }

    size_t current_usage() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_current_usage;
    }

    void print_stats() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::cout << "[PacketPool] Current: " << m_current_usage
                  << ", Peak: " << m_peak_usage
                  << ", Freelist: Pkt=" << m_packet_freelist.size()
                  << ", Pay=" << m_payload_freelist.size()
                  << std::endl;
    }

    // --- 禁用拷贝 ---
    PacketPool(const PacketPool&) = delete;
    PacketPool& operator=(const PacketPool&) = delete;
};

#endif // PACKET_POOL_HH
