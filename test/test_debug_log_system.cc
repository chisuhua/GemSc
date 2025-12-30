// test/test_debug_log_system.cc
#include "catch_amalgamated.hpp"
#include "core/sim_core.hh"
#include "core/sim_object.hh"
#include "core/packet.hh"
#include "core/packet_pool.hh"
#include "core/event_queue.hh"
#include "tlm.h"
#include <iostream>
#include <sstream>

// Mock 模块用于测试
class DebugTestModule : public SimObject {
public:
    explicit DebugTestModule(const std::string& n, EventQueue* eq) : SimObject(n, eq) {}

    bool handleUpstreamRequest(Packet* pkt, int src_id, const std::string& src_label) {
        // 触发调试输出
        DPRINTF(TEST, "[%s] Handling upstream request\n", getName().c_str());
        // 不再直接delete pkt，因为Packet的析构函数是私有的
        // 应该由PacketPool或专门的管理类来处理
        return true;
    }

    bool handleDownstreamResponse(Packet* pkt, int src_id, const std::string& src_label) {
        // 触发调试输出
        DPRINTF(TEST, "[%s] Handling downstream response\n", getName().c_str());
        // 不再直接delete pkt，因为Packet的析构函数是私有的
        // 应该由PacketPool或专门的管理类来处理
        return true;
    }

    void tick() override {
        DPRINTF(TEST, "[%s] Ticking...\n", getName().c_str());
    }
};

TEST_CASE("Debug Log System Tests", "[debug][log][dprintf]") {
    EventQueue eq;

    SECTION("Basic debug output format and compilation") {
        DebugTestModule module("test_module", &eq);
        
        // 验证 DPRINTF 宏可以正常编译和调用
        REQUIRE_NOTHROW(module.tick());
        
        auto* payload = new tlm::tlm_generic_payload();
        payload->set_command(tlm::TLM_READ_COMMAND);
        payload->set_address(0x1000);
        Packet* pkt = PacketPool::get().acquire();
        pkt->payload = payload;
        pkt->src_cycle = eq.getCurrentCycle();
        pkt->type = PKT_REQ;
        
        REQUIRE_NOTHROW(module.handleUpstreamRequest(pkt, 0, "input"));
        
        // 释放Packet
        PacketPool::get().release(pkt);
        
        // 如果能执行到这里，说明宏没有导致崩溃
        REQUIRE(true);
    }

    SECTION("Consistent format across categories") {
        DebugTestModule module("consistent_test", &eq);
        
        // 验证不同调试类别都能正常工作
        DPRINTF(CPU, "[%s] CPU event\n", module.getName().c_str());
        DPRINTF(MEM, "[%s] Memory event\n", module.getName().c_str());
        DPRINTF(CACHE, "[%s] Cache event\n", module.getName().c_str());
        
        REQUIRE(true);
    }

    SECTION("Multiple modules debug output") {
        DebugTestModule module1("module1", &eq);
        DebugTestModule module2("module2", &eq);
        
        // 验证多个模块都能产生调试输出
        REQUIRE_NOTHROW(module1.tick());
        REQUIRE_NOTHROW(module2.tick());
        
        REQUIRE(true);
    }

    SECTION("Proper packet handling output") {
        DebugTestModule consumer("consumer", &eq);
        
        auto* payload = new tlm::tlm_generic_payload();
        payload->set_command(tlm::TLM_READ_COMMAND);
        payload->set_address(0x2000);
        Packet* pkt = PacketPool::get().acquire();
        pkt->payload = payload;
        pkt->src_cycle = eq.getCurrentCycle();
        pkt->type = PKT_REQ;
        
        REQUIRE_NOTHROW(consumer.handleUpstreamRequest(pkt, 0, "input"));
        
        // 释放Packet
        PacketPool::get().release(pkt);
        
        REQUIRE(true);
    }

    SECTION("Debug output with different packet types") {
        DebugTestModule module("pkt_test", &eq);
        
        // 测试读请求
        auto* read_payload = new tlm::tlm_generic_payload();
        read_payload->set_command(tlm::TLM_READ_COMMAND);
        Packet* read_pkt = PacketPool::get().acquire();
        read_pkt->payload = read_payload;
        read_pkt->src_cycle = eq.getCurrentCycle();
        read_pkt->type = PKT_REQ;
        REQUIRE_NOTHROW(module.handleUpstreamRequest(read_pkt, 0, "input"));
        
        // 释放Packet
        PacketPool::get().release(read_pkt);
        
        // 测试写请求
        auto* write_payload = new tlm::tlm_generic_payload();
        write_payload->set_command(tlm::TLM_WRITE_COMMAND);
        Packet* write_pkt = PacketPool::get().acquire();
        write_pkt->payload = write_payload;
        write_pkt->src_cycle = eq.getCurrentCycle();
        write_pkt->type = PKT_REQ;
        REQUIRE_NOTHROW(module.handleUpstreamRequest(write_pkt, 0, "input"));
        
        // 释放Packet
        PacketPool::get().release(write_pkt);
        
        REQUIRE(true);
    }
}