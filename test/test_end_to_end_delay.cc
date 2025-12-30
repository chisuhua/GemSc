// test/test_end_to_end_delay.cc
#include "catch_amalgamated.hpp"
#include "mock_modules.hh"
#include "core/packet.hh"
#include "core/packet_pool.hh"

class DelayConsumer : public SimObject {
public:
    explicit DelayConsumer(const std::string& n, EventQueue* eq) : SimObject(n, eq) {}

    bool handleUpstreamRequest(Packet* pkt, int src_id, const std::string& label) {
        event_queue->schedule(new LambdaEvent([this, pkt]() {
            // 使用 PacketPool::acquire 而不是 alloc，因为alloc方法不存在
            Packet* resp = PacketPool::get().acquire();
            resp->payload = pkt->payload;
            resp->src_cycle = pkt->src_cycle;
            resp->type = PKT_RESP;
            resp->original_req = pkt;

            auto& pm = getPortManager();
            if (!pm.getUpstreamPorts().empty()) {
                pm.getUpstreamPorts()[0]->sendResp(resp);
            } else {
                PacketPool::get().release(resp);
            }
            PacketPool::get().release(pkt);
        }), 5);

        return true;
    }

    void tick() override {}
};

TEST_CASE("EndToEndDelayTest FiveCycleProcessingPlusLinkLatency", "[end][to][end][delay]") {
    EventQueue eq;
    MockProducer producer("producer", &eq);
    DelayConsumer consumer("consumer", &eq);

    auto* src_port = producer.getPortManager().addDownstreamPort(&producer, {4}, {0});
    auto* dst_port = consumer.getPortManager().addUpstreamPort(&consumer, {4}, {0});

    // 注入 2-cycle 链路延迟
    static_cast<MasterPort*>(src_port)->setDelay(2);

    new PortPair(src_port, dst_port);

    uint64_t req_cycle = 100;
    eq.schedule(new LambdaEvent([&]() {
        producer.sendPacket();
    }), req_cycle);

    eq.run(120);

    auto stats = producer.getPortManager().getDownstreamStats();  // 修复：接收值而不是引用
    REQUIRE(stats.resp_count == 1);
    REQUIRE(stats.total_delay == 7);
}