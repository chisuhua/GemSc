// include/master_port.hh
#ifndef MASTER_PORT_HH
#define MASTER_PORT_HH

#include "simple_port.hh"
#include "port_stats.hh"
#include <string>

// MasterPort：作为发起者（Initiator），发送请求
class MasterPort : public SimplePort {
protected:
    std::string name;
    PortStats stats;

    void updateStats(Packet* pkt) {
        pkt->dst_cycle = getCurrentCycle();
        if (pkt->isResponse()) {
            stats.resp_count++;
            if (pkt->payload) {
                stats.byte_count += pkt->payload->get_data_length();
            }
            // 记录响应延迟
            if (pkt->original_req) {
                uint64_t e2e_delay = pkt->dst_cycle - pkt->original_req->src_cycle;
                stats.total_delay += e2e_delay;
                if (e2e_delay < stats.min_delay) stats.min_delay = e2e_delay;
                if (e2e_delay > stats.max_delay) stats.max_delay = e2e_delay;
            }
        } else if (pkt->isCredit()) {
            stats.credit_received++;
        }
    }

public:
    explicit MasterPort(const std::string& n) : name(n) {}

    const std::string& getName() const { return name; }
    const PortStats& getStats() const { return stats; }
    void resetStats() { stats.reset(); }

    bool recv(Packet* pkt) final {
        updateStats(pkt);
        return recvResp(pkt);
    }

    virtual void tick() = 0;

    // 接收响应（由子类实现）
    virtual bool recvResp(Packet* pkt) = 0;

    // 发起请求
    virtual bool sendReq(Packet* pkt) {
        return send(pkt);
    }

    // 重写 send：记录请求
    bool send(Packet* pkt) override {
        if (pkt->isRequest()) {
            stats.req_count++;
            stats.byte_count += pkt->payload ? pkt->payload->get_data_length() : 0;
        } else if (pkt->isCredit()) {
            stats.credit_sent++;
        }
        return SimplePort::send(pkt);
    }

    virtual uint64_t getCurrentCycle() const = 0;  // 由子类实现

    virtual ~MasterPort() = default;
};

#endif // MASTER_PORT_HH
