#include "catch_amalgamated.hpp"
#include "mock_modules.hh"

TEST_CASE("LatencyTest InjectedDelayPropagates", "[latency][injection]") {
    EventQueue eq;
    MockProducer producer("producer", &eq);
    MockConsumer consumer("consumer", &eq);

    producer.getPortManager().addDownstreamPort(&producer, {4}, {0});
    consumer.getPortManager().addUpstreamPort(&consumer, {4}, {0});

    auto* port_a = producer.getPortManager().getDownstreamPorts()[0];
    auto* port_b = consumer.getPortManager().getUpstreamPorts()[0];

    // 设置 5 cycle 延迟
    new PortPair(port_a, port_b);

    port_a->setDelay(5);

    uint64_t send_cycle = 100;
    eq.schedule(new LambdaEvent([&]() {
        producer.sendPacket();
    }), send_cycle);

    eq.run(110);

    REQUIRE_FALSE(consumer.received_packets.empty());
    Packet* pkt = consumer.received_packets[0];

    REQUIRE(pkt->src_cycle == send_cycle);
    REQUIRE(pkt->dst_cycle == send_cycle + 5);
}