// packet_pool_test.cc
#include "catch_amalgamated.hpp"
#include "event_queue.hh"
#include "packet.hh"
#include "ext/packet_pool.hh"
#include "cmd.hh" // 为了创建payload
#include <iostream>

// 辅助函数：为 Packet 创建一个简单的 payload
tlm::tlm_generic_payload* createSimplePayload() {
    tlm::tlm_generic_payload* payload = new tlm::tlm_generic_payload();
    ReadCmd cmd(0x1000, 8);
    payload->set_extension(new ReadCmdExt(cmd));
    return payload;
}

TEST_CASE("Packet Pool Tests", "[packet][pool]") {
    EventQueue event_queue; // 为 SimObject 提供事件队列
    PacketPool& pool = PacketPool::get(); // 获取单例实例

    SECTION("AcquireAndRelease - Verify basic acquire and release functionality") {
        REQUIRE(pool.current_usage() == 0);
        REQUIRE(pool.peak_usage() == 0);

        Packet* pkt = pool.acquire();
        REQUIRE(pkt != nullptr);
        
        // 初始化必要的字段
        pkt->payload = createSimplePayload();
        pkt->src_cycle = 10;
        pkt->stream_id = 1;
        pkt->seq_num = 1;

        REQUIRE(pool.current_usage() == 1);
        REQUIRE(pool.peak_usage() == 1);

        pool.release(pkt);
        REQUIRE(pool.current_usage() == 0);
        REQUIRE(pool.peak_usage() <= 1); // peak_usage 不会减少
    }

    SECTION("ReferenceCounting_ResponseToRequest - Verify reference counting mechanism: Response points to Request") {
        // 步骤1: 创建一个请求包 (Req)
        Packet* req_pkt = pool.acquire();
        req_pkt->payload = createSimplePayload();
        req_pkt->src_cycle = 100;
        req_pkt->stream_id = 5;
        req_pkt->seq_num = 10;
        req_pkt->type = PKT_REQ;
        req_pkt->original_req = req_pkt; // 请求自身是原始请求

        REQUIRE(req_pkt->ref_count == 0); // 初始为0
        REQUIRE(pool.current_usage() == 1);

        // 步骤2: 创建一个响应包 (Resp)，并让其 original_req 指向 Req
        Packet* resp_pkt = pool.acquire();
        resp_pkt->type = PKT_RESP;
        resp_pkt->stream_id = req_pkt->stream_id;
        resp_pkt->seq_num = req_pkt->seq_num;
        resp_pkt->original_req = req_pkt; // 关键：建立引用
        // 注意：在完善后的设计中，这应通过 factory 方法完成，并自动调用 add_ref

        REQUIRE(req_pkt->ref_count == 1); // 因为 resp_pkt 持有引用
        REQUIRE(pool.current_usage() == 2);

        // 步骤3: 释放 Resp 包
        pool.release(resp_pkt);
        REQUIRE(pool.current_usage() == 1); // Resp 被回收，但 Req 仍在
        REQUIRE(req_pkt->ref_count == 0); // Resp 释放时，减少了对 Req 的引用

        // 此时，req_pkt 仍然有效且可用
        REQUIRE(req_pkt->src_cycle == 100);
        REQUIRE(req_pkt->stream_id == 5);
        REQUIRE(req_pkt->seq_num == 10);

        // 步骤4: 释放 Req 包
        pool.release(req_pkt);
        REQUIRE(pool.current_usage() == 0); // 所有资源都已回收
    }

    SECTION("ReferenceCounting_MultipleResponses - Verify reference counting mechanism: Multiple Responses point to the same Request") {
        // 创建一个请求包
        Packet* req_pkt = pool.acquire();
        req_pkt->payload = createSimplePayload();
        req_pkt->src_cycle = 200;
        req_pkt->stream_id = 3;
        req_pkt->seq_num = 7;
        req_pkt->type = PKT_REQ;
        req_pkt->original_req = req_pkt;

        REQUIRE(req_pkt->ref_count == 0);
        REQUIRE(pool.current_usage() == 1);

        // 创建三个响应包，都指向同一个请求
        std::vector<Packet*> responses;
        for (int i = 0; i < 3; ++i) {
            Packet* resp = pool.acquire();
            resp->type = PKT_RESP;
            resp->original_req = req_pkt;
            // ... 设置其他字段 ...
            responses.push_back(resp);
        }

        REQUIRE(req_pkt->ref_count == 3); // 被三个响应持有
        REQUIRE(pool.current_usage() == 4); // 1 Req + 3 Resp

        // 依次释放三个响应
        for (auto* resp : responses) {
            pool.release(resp);
        }
        REQUIRE(pool.current_usage() == 1); // 只剩下 Req
        REQUIRE(req_pkt->ref_count == 0); // 最后一个响应释放后，引用计数归零

        // Req 仍然有效
        REQUIRE(req_pkt->src_cycle == 200);

        // 最后释放 Req
        pool.release(req_pkt);
        REQUIRE(pool.current_usage() == 0);
    }

    SECTION("EndToEndDelayCalculation - Verify getEnd2EndCycles calculation correctness") {
        // 创建请求
        Packet* req_pkt = pool.acquire();
        req_pkt->src_cycle = 50;
        req_pkt->type = PKT_REQ;
        req_pkt->original_req = req_pkt;

        // 创建响应
        Packet* resp_pkt = pool.acquire();
        resp_pkt->type = PKT_RESP;
        resp_pkt->original_req = req_pkt;

        // 响应在第 150 个周期到达
        resp_pkt->dst_cycle = 150;

        uint64_t e2e_delay = resp_pkt->getEnd2EndCycles();
        REQUIRE(e2e_delay == 100); // 150 - 50 = 100

        // 清理
        pool.release(resp_pkt);
        pool.release(req_pkt);
    }

    SECTION("DelayCalculationWithoutOriginalReq - Verify delay calculation when original_req is null") {
        Packet* orphaned_resp = pool.acquire();
        orphaned_resp->type = PKT_RESP;
        orphaned_resp->original_req = nullptr; // 没有原始请求

        orphaned_resp->src_cycle = 10;
        orphaned_resp->dst_cycle = 25;

        uint64_t delay = orphaned_resp->getDelayCycles();
        uint64_t e2e_delay = orphaned_resp->getEnd2EndCycles();

        REQUIRE(delay == 15);
        REQUIRE(e2e_delay == 15); // fallback to getDelayCycles()

        pool.release(orphaned_resp);
    }

    SECTION("MasterPortStats_EndToEndDelay - Verify E2E delay accumulation in PortStats") {
        // 创建一个模拟的 MasterPort 子类用于测试
        class TestMasterPort : public MasterPort {
        public:
            explicit TestMasterPort(const std::string& n) : MasterPort(n) {}
            
            bool recvResp(Packet* pkt) override { 
                // 在测试中，我们只关心 stats 更新
                return true; 
            }
            
            uint64_t getCurrentCycle() const override { 
                return 100; // 返回一个固定的当前周期
            }
        };

        TestMasterPort test_port("test_port");

        // 创建一个响应包
        Packet* resp_pkt = pool.acquire();
        resp_pkt->type = PKT_RESP;
        // 设置一个虚拟的原始请求来触发 E2E 统计
        Packet* dummy_req = pool.acquire();
        dummy_req->src_cycle = 90; // 发起于周期90
        resp_pkt->original_req = dummy_req;

        // 响应在周期100被接收（由 getCurrentCycle() 定义）
        // 这会触发 MasterPort::updateStats
        test_port.recv(resp_pkt);

        // 检查统计
        auto stats = test_port.getStats();
        REQUIRE(stats.resp_count == 1);
        REQUIRE(stats.total_delay == 10); // E2E: 100 - 90 = 10
        REQUIRE(stats.min_delay == 10);
        REQUIRE(stats.max_delay == 10);

        // 清理所有 Packet
        pool.release(resp_pkt);
        pool.release(dummy_req);
    }
}