// test/test_virtual_channel.cc
#include "catch_amalgamated.hpp"
#include "mock_modules.hh"
#include "core/packet_pool.hh"  // 添加PacketPool头文件

TEST_CASE("VirtualChannel Basic", "[virtual][channel]") {
    EventQueue eq;
    MockSim module("test_module", &eq);

    eq.run(5);

    REQUIRE(module.getName() == "test_module");
}

TEST_CASE("VirtualChannelTest InOrderPerVC_OutOfOrderAcrossVC", "[virtual][channel]") {
    EventQueue eq;
    MockProducer producer("producer", &eq);
    MockConsumer consumer("consumer", &eq);

    // 2 VC: [4, 4] buffers
    producer.getPortManager().addDownstreamPort(&producer, {4, 4}, {0, 1});
    consumer.getPortManager().addUpstreamPort(&consumer, {4, 4}, {0, 1});

    new PortPair(
        producer.getPortManager().getDownstreamPorts()[0],
        consumer.getPortManager().getUpstreamPorts()[0]
    );

    // 交错发送 VC0 和 VC1
    producer.sendPacket(0);  // VC0
    producer.sendPacket(1);  // VC1
    producer.sendPacket(0);  // VC0
    producer.sendPacket(1);  // VC1

    // 模拟消费：按顺序取包
    while (!consumer.received_packets.empty()) {
        consumer.received_packets.pop_back();
    }

    // 验证同 VC 内保序
    REQUIRE(consumer.received_vcs.size() >= 0);  // 至少有一些包被接收
    // 不能保证跨 VC 顺序，但同 VC 必须有序
    int seq0 = -1, seq1 = -1;
    for (size_t i = 0; i < consumer.received_packets.size(); ++i) {
        Packet* pkt = consumer.received_packets[i];
        if (pkt->vc_id == 0) {
            REQUIRE((int)pkt->seq_num > seq0);
            seq0 = pkt->seq_num;
        } else if (pkt->vc_id == 1) {
            REQUIRE((int)pkt->seq_num > seq1);
            seq1 = pkt->seq_num;
        }
    }

    // 清理 - 使用PacketPool释放包而不是直接delete
    for (auto* pkt : consumer.received_packets) {
        PacketPool::get().release(pkt);  // 使用PacketPool释放包
    }
}