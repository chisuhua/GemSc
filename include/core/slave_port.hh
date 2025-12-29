// include/slave_port.hh
#ifndef SLAVE_PORT_HH
#define SLAVE_PORT_HH

#include "simple_port.hh"
#include "port_stats.hh"
#include <string>

// SlavePort：作为目标（Target），接收请求
class SlavePort : public SimplePort {
protected:
    std::string name;
    PortStats stats;

    void updateStats(Packet* pkt) {
        pkt->dst_cycle = getCurrentCycle();  // 记录接收时间
        // 更新延迟
        if (pkt->original_req && !pkt->isCredit()) {
            uint64_t delay = pkt->getDelayCycles();
            stats.total_delay += delay;
            if (delay < stats.min_delay) stats.min_delay = delay;
            if (delay > stats.max_delay) stats.max_delay = delay;
        }

        if (pkt->isRequest()) {
            stats.req_count++;
            stats.byte_count += pkt->payload ? pkt->payload->get_data_length() : 0;
        } else if (pkt->isStream()) {
            stats.req_count++;
        }
    }

public:
    explicit SlavePort(const std::string& n) : name(n) {}

    const std::string& getName() const { return name; }
    const PortStats& getStats() const { return stats; }
    void resetStats() { stats.reset(); }

    // 重写 recv：增加统计
    bool recv(Packet* pkt) final {
        updateStats(pkt);
        return recvReq(pkt);
    }

    // 接收请求（由子类实现）
    virtual bool recvReq(Packet* pkt) = 0;

    virtual void tick() = 0;

    // 发送响应
    bool sendResp(Packet* pkt) {
        stats.resp_count++;
        stats.byte_count += pkt->payload ? pkt->payload->get_data_length() : 0;
        return send(pkt);
    }

    virtual uint64_t getCurrentCycle() const = 0;  // 由子类实现
    virtual ~SlavePort() = default;
};

#endif // SLAVE_PORT_HH
