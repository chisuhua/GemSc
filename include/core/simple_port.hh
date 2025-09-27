// include/simple_port.hh
#ifndef SIMPLE_PORT_HH
#define SIMPLE_PORT_HH

#include "packet.hh"
#include <string>

class PortPair;
class TopologyDumper;
class SimObject;

// 基础端口类：支持 send/recv
class SimplePort {
protected:
    PortPair* pair = nullptr;
    int my_side;
    int delay_cycles = 0; // wires delay

public:
    virtual void bind(PortPair* p, int side) {
        pair = p;
        my_side = side;
    }

    // 发送数据包
    virtual bool send(Packet* pkt);

    // 接收数据包（纯虚，由子类实现）
    virtual bool recv(Packet* pkt) = 0;
    virtual SimObject* getOwner() = 0;

    void setDelay(int d) { delay_cycles = d; }
    int getDelay() {return delay_cycles; }

    virtual ~SimplePort() = default;

friend class TopologyDumper;
};

// 双向连接对
class PortPair {
public:
    SimplePort* side_a;
    SimplePort* side_b;

    PortPair(SimplePort* a, SimplePort* b) : side_a(a), side_b(b) {
        side_a->bind(this, 0);
        side_b->bind(this, 1);
    }
};

inline bool SimplePort::send(Packet* pkt) {
    if (pair) {
        SimplePort* other = (my_side == 0) ? pair->side_b : pair->side_a;
        return other->recv(pkt);
    }
    return false;
}


#endif // SIMPLE_PORT_HH
