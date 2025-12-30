// test/mock_modules.hh
#ifndef MOCK_MODULES_HH
#define MOCK_MODULES_HH

#include "core/sim_object.hh"
#include "core/packet.hh"
#include "core/packet_pool.hh"  // 添加PacketPool的头文件
#include "tlm.h"

// 添加缺失的枚举定义
enum PacketTypeExt {
    PKT_REQ_READ = PKT_REQ,
    PKT_REQ_WRITE = PKT_REQ,
};

// Mock 模块用于测试
class MockSim : public SimObject {
public:
    explicit MockSim(const std::string& n, EventQueue* eq) : SimObject(n, eq) {}

    bool handleUpstreamRequest(Packet* pkt, int src_id, const std::string& src_label) override {
        // 使用PacketPool来释放Packet
        PacketPool::get().release(pkt);
        return true;
    }

    bool handleDownstreamResponse(Packet* pkt, int src_id, const std::string& src_label) override {
        // 使用PacketPool来释放Packet
        PacketPool::get().release(pkt);
        return true;
    }

    void tick() override {
        // 模拟时钟周期处理
    }
};

// 简化版 Producer，用于发送请求
class MockProducer : public SimObject {
public:
    Packet* last_sent = nullptr;
    int send_count = 0;
    int fail_count = 0;

    explicit MockProducer(const std::string& n, EventQueue* eq) : SimObject(n, eq) {}

    bool handleDownstreamResponse(Packet* pkt, int src_id, const std::string& src_label) override {
        PacketPool::get().release(pkt);
        return true;
    }

    void sendPacket(int vc_id = 0) {
        auto* trans = new tlm::tlm_generic_payload();
        trans->set_command(tlm::TLM_READ_COMMAND);
        trans->set_address(0x1000 + send_count * 4);
        trans->set_data_length(4);

        Packet* pkt = PacketPool::get().acquire();
        pkt->payload = trans;
        pkt->src_cycle = event_queue->getCurrentCycle();
        pkt->type = PKT_REQ;
        pkt->vc_id = vc_id;
        pkt->seq_num = send_count;

        if (getPortManager().getDownstreamPorts().empty()) {
            PacketPool::get().release(pkt);
            return;
        }

        MasterPort* port = getPortManager().getDownstreamPorts()[0];
        if (port->sendReq(pkt)) {
            last_sent = pkt;
            send_count++;
        } else {
            fail_count++;
            PacketPool::get().release(pkt);
        }
    }

    void tick() override {}
};

// 简化版 Consumer，用于接收请求
class MockConsumer : public SimObject {
public:
    std::vector<Packet*> received_packets;
    std::vector<int> received_vcs;

    explicit MockConsumer(const std::string& n, EventQueue* eq) : SimObject(n, eq) {}

    bool handleUpstreamRequest(Packet* pkt, int src_id, const std::string& src_label) override {
        received_packets.push_back(pkt);
        received_vcs.push_back(pkt->vc_id);
        return true;
    }

    void tick() override {}
};

#endif // MOCK_MODULES_HH