// packet_pool_test.cc
#include <gtest/gtest.h>
#include "event_queue.hh"
#include "packet.hh"
#include "ext/packet_pool.hh"
#include "cmd.hh" // 为了创建payload

// 辅助函数：为 Packet 创建一个简单的 payload
tlm::tlm_generic_payload* createSimplePayload() {
    tlm::tlm_generic_payload* payload = new tlm::tlm_generic_payload();
    ReadCmd cmd(0x1000, 8);
    payload->set_extension(new ReadCmdExt(cmd));
    return payload;
}

class PacketPoolTest : public ::testing::Test {
protected:
    EventQueue event_queue; // 为 SimObject 提供事件队列
    PacketPool& pool = PacketPool::get(); // 获取单例实例

    void SetUp() override {
        // 在每个测试开始前重置 Pool 状态（可选）
        // 这里不直接暴露 reset 方法，但可以确保测试独立
    }

    void TearDown() override {
        // 可以在这里打印统计信息以进行调试
        // pool.print_stats();
    }
};

// TEST 1: 验证基本的 acquire 和 release 功能
TEST_F(PacketPoolTest, AcquireAndRelease) {
    EXPECT_EQ(pool.current_usage(), 0);
    EXPECT_EQ(pool.peak_usage(), 0);

    Packet* pkt = pool.acquire();
    ASSERT_NE(pkt, nullptr);
    
    // 初始化必要的字段
    pkt->payload = createSimplePayload();
    pkt->src_cycle = 10;
    pkt->stream_id = 1;
    pkt->seq_num = 1;

    EXPECT_EQ(pool.current_usage(), 1);
    EXPECT_EQ(pool.peak_usage(), 1);

    pool.release(pkt);
    EXPECT_EQ(pool.current_usage(), 0);
    EXPECT_LE(pool.peak_usage(), 1); // peak_usage 不会减少
}

// TEST 2: 验证引用计数机制 - 场景1: Response 指向 Request
TEST_F(PacketPoolTest, ReferenceCounting_ResponseToRequest) {
    // 步骤1: 创建一个请求包 (Req)
    Packet* req_pkt = pool.acquire();
    req_pkt->payload = createSimplePayload();
    req_pkt->src_cycle = 100;
    req_pkt->stream_id = 5;
    req_pkt->seq_num = 10;
    req_pkt->type = PKT_REQ;
    req_pkt->original_req = req_pkt; // 请求自身是原始请求

    EXPECT_EQ(req_pkt->ref_count, 0); // 初始为0
    EXPECT_EQ(pool.current_usage(), 1);

    // 步骤2: 创建一个响应包 (Resp)，并让其 original_req 指向 Req
    Packet* resp_pkt = pool.acquire();
    resp_pkt->type = PKT_RESP;
    resp_pkt->stream_id = req_pkt->stream_id;
    resp_pkt->seq_num = req_pkt->seq_num;
    resp_pkt->original_req = req_pkt; // 关键：建立引用
    // 注意：在完善后的设计中，这应通过 factory 方法完成，并自动调用 add_ref

    EXPECT_EQ(req_pkt->ref_count, 1); // 因为 resp_pkt 持有引用
    EXPECT_EQ(pool.current_usage(), 2);

    // 步骤3: 释放 Resp 包
    pool.release(resp_pkt);
    EXPECT_EQ(pool.current_usage(), 1); // Resp 被回收，但 Req 仍在
    EXPECT_EQ(req_pkt->ref_count, 0); // Resp 释放时，减少了对 Req 的引用

    // 此时，req_pkt 仍然有效且可用
    EXPECT_EQ(req_pkt->src_cycle, 100);
    EXPECT_EQ(req_pkt->stream_id, 5);
    EXPECT_EQ(req_pkt->seq_num, 10);

    // 步骤4: 释放 Req 包
    pool.release(req_pkt);
    EXPECT_EQ(pool.current_usage(), 0); // 所有资源都已回收
}

// TEST 3: 验证引用计数机制 - 场景2: 多个 Response 指向同一个 Request
TEST_F(PacketPoolTest, ReferenceCounting_MultipleResponses) {
    // 创建一个请求包
    Packet* req_pkt = pool.acquire();
    req_pkt->payload = createSimplePayload();
    req_pkt->src_cycle = 200;
    req_pkt->stream_id = 3;
    req_pkt->seq_num = 7;
    req_pkt->type = PKT_REQ;
    req_pkt->original_req = req_pkt;

    EXPECT_EQ(req_pkt->ref_count, 0);
    EXPECT_EQ(pool.current_usage(), 1);

    // 创建三个响应包，都指向同一个请求
    std::vector<Packet*> responses;
    for (int i = 0; i < 3; ++i) {
        Packet* resp = pool.acquire();
        resp->type = PKT_RESP;
        resp->original_req = req_pkt;
        // ... 设置其他字段 ...
        responses.push_back(resp);
    }

    EXPECT_EQ(req_pkt->ref_count, 3); // 被三个响应持有
    EXPECT_EQ(pool.current_usage(), 4); // 1 Req + 3 Resp

    // 依次释放三个响应
    for (auto* resp : responses) {
        pool.release(resp);
    }
    EXPECT_EQ(pool.current_usage(), 1); // 只剩下 Req
    EXPECT_EQ(req_pkt->ref_count, 0); // 最后一个响应释放后，引用计数归零

    // Req 仍然有效
    EXPECT_EQ(req_pkt->src_cycle, 200);

    // 最后释放 Req
    pool.release(req_pkt);
    EXPECT_EQ(pool.current_usage(), 0);
}

// TEST 4: 验证 getEnd2EndCycles 计算正确性
TEST_F(PacketPoolTest, EndToEndDelayCalculation) {
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
    EXPECT_EQ(e2e_delay, 100); // 150 - 50 = 100

    // 清理
    pool.release(resp_pkt);
    pool.release(req_pkt);
}

// TEST 5: 验证空 original_req 时的延迟计算
TEST_F(PacketPoolTest, DelayCalculationWithoutOriginalReq) {
    Packet* orphaned_resp = pool.acquire();
    orphaned_resp->type = PKT_RESP;
    orphaned_resp->original_req = nullptr; // 没有原始请求

    orphaned_resp->src_cycle = 10;
    orphaned_resp->dst_cycle = 25;

    uint64_t delay = orphaned_resp->getDelayCycles();
    uint64_t e2e_delay = orphaned_resp->getEnd2EndCycles();

    EXPECT_EQ(delay, 15);
    EXPECT_EQ(e2e_delay, 15); // fallback to getDelayCycles()

    pool.release(orphaned_resp);
}

// TEST 6: 验证 PortStats 中 E2E 延迟的累加
TEST_F(PacketPoolTest, MasterPortStats_EndToEndDelay) {
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
    EXPECT_EQ(stats.resp_count, 1);
    EXPECT_EQ(stats.total_delay, 10); // E2E: 100 - 90 = 10
    EXPECT_EQ(stats.min_delay, 10);
    EXPECT_EQ(stats.max_delay, 10);

    // 清理所有 Packet
    pool.release(resp_pkt);
    pool.release(dummy_req);
}
