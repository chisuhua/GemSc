// test/test_valid_only.cc
#include "catch_amalgamated.hpp"
#include "mock_modules.hh"

TEST_CASE("ValidOnly Basic", "[valid][only]") {
    EventQueue eq;
    MockSim module("test_module", &eq);

    eq.run(5);

    REQUIRE(module.getName() == "test_module");
}

TEST_CASE("ValidOnlyTest LargeInputBuffer_NoBackpressure", "[valid][only]") {
    EventQueue eq;
    MockProducer producer("producer", &eq);
    MockConsumer consumer("consumer", &eq);

    // input_buffer_size=1024 → 几乎永不反压
    producer.getPortManager().addDownstreamPort(&producer, {4}, {0});
    consumer.getPortManager().addUpstreamPort(&consumer, {1024}, {0});

    new PortPair(
        producer.getPortManager().getDownstreamPorts()[0],
        consumer.getPortManager().getUpstreamPorts()[0]
    );

    // 发送 100 个包，应全部成功
    for (int i = 0; i < 100; ++i) {
        producer.sendPacket();
    }

    REQUIRE(producer.send_count == 100);
    REQUIRE(producer.fail_count == 0);
    REQUIRE(consumer.received_packets.size() == 100);
}