// test/test_valid_ready.cc
#include "catch_amalgamated.hpp"
#include "mock_modules.hh"

TEST_CASE("ValidReady Basic", "[valid][ready]") {
    EventQueue eq;
    MockSim module("test_module", &eq);

    eq.run(5);

    REQUIRE(module.getName() == "test_module");
}

TEST_CASE("ValidReadyTest NoBuffer_NoBypass", "[valid][ready]") {
    EventQueue eq;
    MockProducer producer("producer", &eq);
    MockConsumer consumer("consumer", &eq);

    // input_buffer_size=0 → 必须立即处理，否则反压
    producer.getPortManager().addDownstreamPort(&producer, {4}, {0});
    consumer.getPortManager().addUpstreamPort(&consumer, {0}, {0});  // no buffer

    new PortPair(
        producer.getPortManager().getDownstreamPorts()[0],
        consumer.getPortManager().getUpstreamPorts()[0]
    );

    // 第一次发送：成功（consumer 可处理）
    producer.sendPacket();
    REQUIRE(producer.send_count == 1);
    REQUIRE(consumer.received_packets.size() == 1);

    // 即使 consumer 不处理新包，producer 也不能绕过
    producer.sendPacket();
    REQUIRE(producer.send_count == 2);  // still succeeds (buffered in output)
    REQUIRE(consumer.received_packets.size() == 1);  // only one processed

    // 使用PacketPool来释放接收的包
    for (auto* pkt : consumer.received_packets) {
        PacketPool::get().release(pkt);
    }
    consumer.received_packets.clear();
}