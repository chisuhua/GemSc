// include/modules/cpu_sim.hh
#ifndef CPU_SIM_HH
#define CPU_SIM_HH

#include "../core/sim_object.hh"
#include "../core/packet.hh"
#include "../core/ext/packet_pool.hh"
#include <unordered_map>
#include <vector>
#include <cstdlib>

class CPUSim : public SimObject {
private:
    std::unordered_map<uint64_t, Packet*> inflight_reqs;
    uint64_t next_addr = 0x1000;

public:
    CPUSim(const std::string& n, EventQueue* eq) : SimObject(n, eq) {}

    // 更新方法签名以匹配父类
    bool handleDownstreamResponse(Packet* pkt, int src_id, const std::string& src_label) override {
        if (pkt->isResponse()) {
            uint64_t addr = pkt->original_req->payload->get_address();
            DPRINTF(CPU, "Received response for 0x%" PRIx64 "\n", addr);
            inflight_reqs.erase(addr);
            PacketPool::get().release(pkt);
            return true;
        }
        PacketPool::get().release(pkt);
        return false;
    }

    void tick() override {
        auto& pm = getPortManager();
        if (pm.getDownstreamPorts().empty()) return;

        if (inflight_reqs.size() < 4 && rand() % 20 == 0) {
            auto* trans = new tlm::tlm_generic_payload();
            trans->set_command(tlm::TLM_READ_COMMAND);
            trans->set_address(next_addr);
            trans->set_data_length(4);
            trans->set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

            Packet* pkt = PacketPool::get().acquire();
            pkt->payload = trans;
            pkt->src_cycle = event_queue->getCurrentCycle();
            pkt->type = PKT_REQ;
            MasterPort* port = pm.getDownstreamPorts()[next_addr % pm.getDownstreamPorts().size()];
            pkt->vc_id = 0; // 设置VC ID

            if (port->sendReq(pkt)) {
                inflight_reqs[next_addr] = pkt;
                DPRINTF(CPU, "Sent to downstream[%zu]\n", (next_addr % pm.getDownstreamPorts().size()));
                next_addr += 4;
            } else {
                PacketPool::get().release(pkt);
            }
        }
    }
};

#endif // CPU_SIM_HH