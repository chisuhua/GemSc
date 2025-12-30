#include "catch_amalgamated.hpp"
#include "ext/credit_stream.hh"
#include "core/event_queue.hh"
#include "core/packet.hh"
#include "ext/packet_pool.hh"
#include <iostream>

TEST_CASE("Credit Stream Tests", "[credit][stream]") {
    EventQueue event_queue; // 为 SimObject 提供事件队列
    PacketPool& pool = PacketPool::get(); // 获取单例实例

    SECTION("CreditStream_SendReceive - Verify basic credit-based flow control") {
        CreditStream cs(5); // 初始5个信用值
        REQUIRE(cs.getCredit() == 5);

        // 发送一个数据包，消耗一个信用值
        Packet* pkt = pool.acquire();
        REQUIRE(cs.send(pkt));
        REQUIRE(cs.getCredit() == 4);

        // 发送信用返回包
        cs.returnCredit(1);
        REQUIRE(cs.getCredit() == 5);

        pool.release(pkt);
    }

    SECTION("CreditStream_ExhaustedCredit - Verify flow control when credit is exhausted") {
        CreditStream cs(1); // 初始1个信用值
        REQUIRE(cs.getCredit() == 1);

        // 发送一个数据包，消耗一个信用值
        Packet* pkt1 = pool.acquire();
        REQUIRE(cs.send(pkt1));
        REQUIRE(cs.getCredit() == 0);

        // 尝试发送第二个数据包，应该失败
        Packet* pkt2 = pool.acquire();
        REQUIRE(!cs.send(pkt2)); // 没有信用值，发送失败

        // 返回一个信用值
        cs.returnCredit(1);
        REQUIRE(cs.getCredit() == 1);
        REQUIRE(cs.send(pkt2)); // 现在可以发送了
        REQUIRE(cs.getCredit() == 0);

        pool.release(pkt1);
        pool.release(pkt2);
    }

    SECTION("CreditStream_OverflowProtection - Verify credit count doesn't exceed initial value") {
        CreditStream cs(5); // 初始5个信用值
        REQUIRE(cs.getCredit() == 5);

        // 尝试返回过多的信用值
        cs.returnCredit(10);
        REQUIRE(cs.getCredit() == 5); // 信用值不应超过初始值
    }

    SECTION("CreditStream_LargeReturn - Verify credit return with multiple credits") {
        CreditStream cs(10); // 初始10个信用值
        // 先消耗所有信用值
        for (int i = 0; i < 10; i++) {
            Packet* pkt = pool.acquire();
            REQUIRE(cs.send(pkt));
            pool.release(pkt);
        }
        REQUIRE(cs.getCredit() == 0);

        // 一次性返回多个信用值
        cs.returnCredit(5);
        REQUIRE(cs.getCredit() == 5);

        // 再次发送5个数据包
        for (int i = 0; i < 5; i++) {
            Packet* pkt = pool.acquire();
            REQUIRE(cs.send(pkt));
            pool.release(pkt);
        }
        REQUIRE(cs.getCredit() == 0);
    }
}