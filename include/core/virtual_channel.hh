// include/virtual_channel.hh
#ifndef VIRTUAL_CHANNEL_HH
#define VIRTUAL_CHANNEL_HH

#include "packet.hh"
#include "master_port.hh"
#include <queue>
#include <vector>

// 输入虚拟通道（用于 UpstreamPort）
struct InputVC {
    int id;
    size_t priority;  // 数值越小，优先级越高
    std::queue<Packet*> buffer;
    size_t capacity;

    struct Stats {
        uint64_t enqueued = 0;
        uint64_t dropped = 0;
        uint64_t processed = 0;
    } stats;

    explicit InputVC(int vid, size_t cap, size_t pri = 0)
        : id(vid), priority(pri), capacity(cap) {}

    bool enqueue(Packet* pkt) {
        if (buffer.size() >= capacity) {
            return false;
        }
        buffer.push(pkt);
        stats.enqueued++;
        return true;
    }

    Packet* front() const {
        return buffer.empty() ? nullptr : buffer.front();
    }

    void pop() {
        if (!buffer.empty()) {
            stats.processed++;
            buffer.pop();
        }
    }

    bool empty() const { return buffer.empty(); }
    size_t size() const { return buffer.size(); }
    bool full() const { return size() >= capacity; }

    void resetStats() { stats = {}; }
};

// 输出虚拟通道（用于 DownstreamPort）
struct OutputVC {
    int id;
    size_t priority;
    std::queue<Packet*> buffer;
    size_t capacity;

    struct Stats {
        uint64_t enqueued = 0;
        uint64_t forwarded = 0;
        uint64_t dropped = 0;
    } stats;

    explicit OutputVC(int vid, size_t cap, size_t pri = 0)
        : id(vid), priority(pri), capacity(cap) {}

    bool enqueue(Packet* pkt) {
        if (buffer.size() >= capacity) {
            return false;
        }
        buffer.push(pkt);
        stats.enqueued++;
        return true;
    }

    Packet* front() const {
        return buffer.empty() ? nullptr : buffer.front();
    }

    bool trySend(MasterPort* port) {
        if (buffer.empty()) return true;

        Packet* pkt = buffer.front();
        if (port->sendReq(pkt)) {
            buffer.pop();
            delete pkt;
            stats.forwarded++;
            return true;
        }
        return false;
    }

    bool empty() const { return buffer.empty(); }
    size_t size() const { return buffer.size(); }
    bool full() const { return size() >= capacity; }

    void resetStats() { stats = {}; }
};

#endif // VIRTUAL_CHANNEL_HH
