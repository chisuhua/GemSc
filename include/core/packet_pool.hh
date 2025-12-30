#ifndef PACKET_POOL_HH
#define PACKET_POOL_HH

#include "packet.hh"
#include <queue>
#include <mutex>
#include <memory>

class PacketPool {
public:
    static PacketPool& get() {
        static PacketPool instance;
        return instance;
    }

    // 获取一个可用的Packet
    Packet* acquire() {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        if (pool_.empty()) {
            // 如果池中没有可用Packet，则创建一个新的
            auto* payload = new tlm::tlm_generic_payload();
            return new Packet(payload, 0, PKT_REQ);
        } else {
            // 从池中取出一个可用的Packet
            Packet* pkt = pool_.front();
            pool_.pop();
            pkt->reset(); // 重置状态
            return pkt;
        }
    }

    // 释放一个Packet，使其可以被重用
    void release(Packet* pkt) {
        if (!pkt) return;
        
        std::lock_guard<std::mutex> lock(pool_mutex_);
        // 清理payload，如果是响应包的话
        if (pkt->payload && !pkt->isCredit()) {
            delete pkt->payload;
            pkt->payload = nullptr;
        }
        
        // 如果有原始请求，减少引用计数
        if (pkt->original_req) {
            // 在实际实现中，可能需要处理引用计数
        }
        
        // 将Packet放回池中以供重用
        pool_.push(pkt);
    }

private:
    PacketPool() = default;
    ~PacketPool() {
        // 清理池中所有Packet
        while (!pool_.empty()) {
            Packet* pkt = pool_.front();
            pool_.pop();
            delete pkt;
        }
    }

    std::queue<Packet*> pool_;
    std::mutex pool_mutex_;
};

#endif // PACKET_POOL_HH