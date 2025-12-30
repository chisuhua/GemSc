// test/test_response_path.cc
#include "catch_amalgamated.hpp"
#include "mock_modules.hh"

TEST_CASE("ResponsePathTest RequestReceivedByConsumer", "[response][path]") {
    EventQueue eq;
    MockProducer producer("producer", &eq);
    MockConsumer consumer("consumer", &eq);

    // 直接创建连接
    producer.getPortManager().addDownstreamPort(&producer, {4}, {0});
    consumer.getPortManager().addUpstreamPort(&consumer, {4}, {0});

    new PortPair(
        producer.getPortManager().getDownstreamPorts()[0],
        consumer.getPortManager().getUpstreamPorts()[0]
    );

    producer.sendPacket();
    eq.run(1);

    REQUIRE(consumer.received_packets.size() == 1);
    REQUIRE(consumer.received_packets[0]->seq_num == 0);
}