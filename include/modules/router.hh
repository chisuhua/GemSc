// include/modules/router.hh
#ifndef ROUTER_HH
#define ROUTER_HH

#include "../core/sim_object.hh"
#include "../core/packet.hh"
#include "../core/ext/packet_pool.hh"

class Router : public SimObject {
private:
    int routeByAddress(uint64_t addr) {
        if (addr >= 0x80000000ULL) return 1;  // DMA
        if (addr >= 0xF0000000ULL) return 2;  // MMIO
        return 0;  // 默认内存
    }

public:
    Router(const std::string& n, EventQueue* eq) : SimObject(n, eq) {}

    bool handleUpstreamRequest(Packet* pkt, int src_id, const std::string& src_label) override {
        int dst = routeByAddress(pkt->payload->get_address());
        auto& pm = getPortManager();
        if (dst < (int)pm.getDownstreamPorts().size()) {
            pm.getDownstreamPorts()[dst]->sendReq(pkt);
            DPRINTF(ROUTER, "[%s] Route 0x%" PRIx64 " → out[%d]\n", name.c_str(), pkt->payload->get_address(), dst);
        } else {
            PacketPool::get().release(pkt);
        }
        return true;
    }

    bool handleDownstreamResponse(Packet* pkt, int src_id, const std::string& src_label) override {
        if (pkt->original_req && src_id < (int)getPortManager().getUpstreamPorts().size()) {
            getPortManager().getUpstreamPorts()[src_id]->sendResp(pkt);
        } else {
            PacketPool::get().release(pkt);
        }
        return true;
    }

    void tick() override {}
};

#endif // ROUTER_HH