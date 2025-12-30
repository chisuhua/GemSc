// include/modules/traffic_gen.hh
#ifndef TRAFFIC_GEN_HH
#define TRAFFIC_GEN_HH

#include "../core/sim_object.hh"
#include "../core/packet.hh"
#include "../core/ext/packet_pool.hh"
#include <vector>
#include <random>

enum class GenMode {
    SEQUENTIAL,
    RANDOM,
    TRACE
};

struct TraceEntry {
    uint64_t addr;
    bool is_write;
};

class TrafficGenerator : public SimObject {
private:
    GenMode mode;
    uint64_t cur_addr;
    uint64_t start_addr, end_addr;
    int num_requests;
    int completed;

    std::vector<TraceEntry> trace;
    size_t trace_pos;

    std::mt19937 rng;
    std::uniform_int_distribution<uint64_t> addr_dist;

public:
    TrafficGenerator(const std::string& n, EventQueue* eq)
        : SimObject(n, eq), mode(GenMode::SEQUENTIAL),
          start_addr(0x1000), end_addr(0x2000), num_requests(20),
          completed(0), cur_addr(start_addr),
          rng(std::random_device{}()),
          addr_dist(start_addr, end_addr) {
        // 示例 trace
        trace = {
            {0x1000, false}, {0x1004, true}, {0x1008, false}, {0x100c, true}
        };
    }

    // 回调函数：处理响应
    bool handleDownstreamResponse(Packet* pkt, int src_id, const std::string& src_label) override {
        if (pkt->isResponse()) {
            DPRINTF(TG, "Received response for 0x%llx\n", (unsigned long long)pkt->payload->get_address());
            completed++;
            PacketPool::get().release(pkt);
            return true;
        }
        PacketPool::get().release(pkt);
        return false;
    }

    void tick() override {
        auto& pm = getPortManager();
        if (!pm.getDownstreamPorts().empty() && completed < num_requests) {
            // 1/10 概率发起请求
            if (rand() % 10 == 0) {
                issueRequest();
            }
        }
    }

private:
    void issueRequest() {
        auto* trans = new tlm::tlm_generic_payload();
        bool is_write = false;

        switch (mode) {
            case GenMode::SEQUENTIAL:
                is_write = (cur_addr % 8 == 0);
                trans->set_address(cur_addr);
                cur_addr += 4;
                if (cur_addr >= end_addr) cur_addr = start_addr;
                break;

            case GenMode::RANDOM:
                trans->set_address(addr_dist(rng));
                is_write = rng() % 2;
                break;

            case GenMode::TRACE:
                if (trace_pos >= trace.size()) return;
                auto& entry = trace[trace_pos++];
                trans->set_address(entry.addr);
                is_write = entry.is_write;
                break;
        }

        trans->set_command(is_write ? tlm::TLM_WRITE_COMMAND : tlm::TLM_READ_COMMAND);
        trans->set_data_length(4);
        trans->set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

        Packet* pkt = PacketPool::get().acquire();
        pkt->payload = trans;
        pkt->src_cycle = event_queue->getCurrentCycle();
        pkt->type = is_write ? PKT_REQ : PKT_REQ;

        MasterPort* port = getPortManager().getDownstreamPorts()[0];
        if (port->sendReq(pkt)) {
            DPRINTF(TG, "Generated request to 0x%llx%s\n", 
                    (unsigned long long)trans->get_address(), is_write ? " (write)" : "");
        } else {
            PacketPool::get().release(pkt);
        }
    }
};

#endif // TRAFFIC_GEN_HH