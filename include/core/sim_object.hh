// include/sim_object.hh
#ifndef SIM_OBJECT_HH
#define SIM_OBJECT_HH

#include "event_queue.hh"
#include "port_manager.hh"
#include <memory>

struct LayoutInfo {
    double x = -1, y = -1;
    bool valid() const { return x >= 0 && y >= 0; }
};

class SimObject {
protected:
    std::string name;
    EventQueue* event_queue;
    std::unique_ptr<PortManager> port_manager;
    LayoutInfo layout;

public:
    SimObject(const std::string& n, EventQueue* eq) : name(n), event_queue(eq) {}

    virtual void tick() = 0;
    virtual ~SimObject() = default;

    void initiate_tick() {
        event_queue->schedule(new TickEvent(this), 1);
    }

    const std::string& getName() const { return name; }
    EventQueue* getEventQueue() const { return event_queue; }

    PortManager& getPortManager() {
        if (!port_manager) port_manager = std::make_unique<PortManager>();
        return *port_manager;
    }
    bool hasPortManager() const { return port_manager != nullptr; }

    void setLayout(double x, double y) {
        layout.x = x; layout.y = y;
    }
    const LayoutInfo& getLayout() const { return layout; }

    uint64_t getCurrentCycle() const {
        return event_queue->getCurrentCycle();
    }

    // 更新函数签名，增加 label 参数
    virtual bool handleDownstreamResponse(Packet* pkt, int src_id, const std::string& src_label) {
        DPRINTF(MODULE, "[WARNING] Unhandled downstream response in %s\n", name.c_str());
        delete pkt;
        return false;
    }
    
    // 更新函数签名，增加 label 参数
    virtual bool handleUpstreamRequest(Packet* pkt, int src_id, const std::string& src_label) {
        DPRINTF(MODULE, "[WARNING] Unhandled upstream request in %s\n", name.c_str());
        delete pkt;
        return false;
    }
};

inline void TickEvent::process() {
    obj->tick();
    obj->initiate_tick();
}

#endif // SIM_OBJECT_HH
