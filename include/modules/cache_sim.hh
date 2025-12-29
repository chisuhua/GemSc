// include/modules/cache_sim.hh
#ifndef CACHE_SIM_HH
#define CACHE_SIM_HH

#include "../core/sim_object.hh"
#include "../core/packet.hh"
#include "../core/ext/packet_pool.hh"
#include <queue>

class CacheSim : public SimObject {
private:
    std::queue<Packet*> req_buffer;
    static const int MAX_BUFFER = 4;

    MasterPort* routeToDownstream(Packet* pkt) {
        auto& pm = getPortManager();
        if (pm.getDownstreamPorts().empty()) return nullptr;
        size_t port_idx = (pkt->payload->get_address() >> 8) % pm.getDownstreamPorts().size();
        return dynamic_cast<MasterPort*>(pm.getDownstreamPorts()[port_idx]);
    }

public:
    CacheSim(const std::string& n, EventQueue* eq) : SimObject(n, eq) {}

    bool handleUpstreamRequest(Packet* pkt, int src_id, const std::string& src_label) override {
        if (req_buffer.size() >= MAX_BUFFER) {
            PacketPool::get().release(pkt);
            return false;
        }

        uint64_t addr = pkt->payload->get_address();
        bool hit = (addr & 0x7) == 0;

        if (hit) {
            pkt->payload->set_response_status(tlm::TLM_OK_RESPONSE);
            Packet* resp = PacketPool::get().acquire();
            resp->payload = pkt->payload;
            resp->src_cycle = event_queue->getCurrentCycle();
            resp->type = PKT_RESP;
            resp->original_req = pkt;
            resp->vc_id = pkt->vc_id; // 保持相同的VC

            event_queue->schedule(new LambdaEvent([this, resp, src_id]() {
                getPortManager().getUpstreamPorts()[src_id]->sendResp(resp);
            }), 1);

            PacketPool::get().release(pkt);
        } else {
            req_buffer.push(pkt);
            scheduleForward(1);
        }
        return true;
    }

    bool handleDownstreamResponse(Packet* pkt, int src_id, const std::string& src_label) override {
        DPRINTF(CACHE, "[%s] Received response from %s\n", name.c_str(), src_label.c_str());
        Packet* orig_req = pkt->original_req;
        Packet* resp_to_upstream = PacketPool::get().acquire();
        resp_to_upstream->payload = orig_req->payload;
        resp_to_upstream->src_cycle = event_queue->getCurrentCycle();
        resp_to_upstream->type = PKT_RESP;
        resp_to_upstream->original_req = orig_req;
        resp_to_upstream->vc_id = pkt->vc_id; // 保持相同的VC

        int upstream_id = src_id % getPortManager().getUpstreamPorts().size();
        event_queue->schedule(new LambdaEvent([this, resp_to_upstream, upstream_id]() {
            getPortManager().getUpstreamPorts()[upstream_id]->sendResp(resp_to_upstream);
        }), 1);

        PacketPool::get().release(pkt);
        // 不要释放 orig_req，因为它仍在使用中，会在上游模块中被释放
        return true;
    }

    void tick() override {
        tryForward();
    }

private:
    bool tryForward() {
        if (req_buffer.empty()) return true;
        Packet* pkt = req_buffer.front();
        MasterPort* dst = routeToDownstream(pkt);
        if (dst && dst->sendReq(pkt)) {
            req_buffer.pop();
            return true;
        }
        return false;
    }

    void scheduleForward(int delay) {
        event_queue->schedule(new LambdaEvent([this]() { tryForward(); }), delay);
    }
};

#endif // CACHE_SIM_HH