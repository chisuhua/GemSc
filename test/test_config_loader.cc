// test/test_config_loader.cc
#include "catch_amalgamated.hpp"
#include "../include/module_factory.hh"
#include "../include/sim_object.hh"
#include "../include/packet.hh"

// Mock 模块用于测试
class MockSim : public SimObject {
public:
    explicit MockSim(const std::string& n, EventQueue* eq) : SimObject(n, eq) {}

    bool handleUpstreamRequest(Packet* pkt, int src_id) {
        return true;
    }

    bool handleDownstreamResponse(Packet* pkt, int src_id) {
        return true;
    }

    void tick() override {}
};

// 为测试注册类型
namespace {
auto register_mock = []() {
    ModuleFactory::registerType<MockSim>("MockSim");
    return 0;
}();
}

TEST_CASE("Config Loader Tests", "[config][factory]") {
    EventQueue eq;

    SECTION("ParseAndInstantiateWithVCAndLatency") {
        // JSON 配置：包含 VC、优先级、标签、延迟
        json config = R"({
            "modules": [
                { "name": "src", "type": "MockSim" },
                { "name": "dst", "type": "MockSim" }
            ],
            "connections": [
                {
                    "src": "src",
                    "dst": "dst",
                    "input_buffer_sizes": [8, 4],
                    "output_buffer_sizes": [8, 4],
                    "vc_priorities": [0, 2],
                    "latency": 3,
                    "label": "high_speed_link"
                }
            ]
        })"_json;

        ModuleFactory factory(&eq);
        factory.instantiateAll(config);

        SimObject* src_obj = factory.getInstance("src");
        SimObject* dst_obj = factory.getInstance("dst");

        REQUIRE(src_obj != nullptr);
        REQUIRE(dst_obj != nullptr);

        auto& src_pm = src_obj->getPortManager();
        auto& dst_pm = dst_obj->getPortManager();

        // 验证端口数量
        CHECK(src_pm.getDownstreamPorts().size() == 1);
        CHECK(dst_pm.getUpstreamPorts().size() == 1);

        MasterPort* out_port = src_pm.getDownstreamPorts()[0];
        SlavePort* in_port = dst_pm.getUpstreamPorts()[0];

        // 验证标签
        CHECK(out_port->getName() == "downstream[0]");
        // 标签用于连接标识，不直接暴露

        // 验证 DownstreamPort 的 VC 配置
        auto* down_port = dynamic_cast<DownstreamPort<MockSim>*>(out_port);
        REQUIRE(down_port != nullptr);
        CHECK(down_port->getOutputVCs().size() == 2);  // 2 VCs

        const auto& out_vc0 = down_port->getOutputVCs()[0];
        const auto& out_vc1 = down_port->getOutputVCs()[1];

        CHECK(out_vc0.capacity == 8);
        CHECK(out_vc1.capacity == 4);
        CHECK(out_vc0.priority == 0);
        CHECK(out_vc1.priority == 2);

        // 验证 UpstreamPort 的 VC 配置
        auto* up_port = dynamic_cast<UpstreamPort<MockSim>*>(in_port);
        REQUIRE(up_port != nullptr);
        CHECK(up_port->getInputVCs().size() == 2);

        const auto& in_vc0 = up_port->getInputVCs()[0];
        const auto& in_vc1 = up_port->getInputVCs()[1];

        CHECK(in_vc0.capacity == 8);
        CHECK(in_vc1.capacity == 4);
        CHECK(in_vc0.priority == 0);
        CHECK(in_vc1.priority == 2);

        // 验证延迟注入（在 PortPair 之后）
        CHECK(out_port->getDelay() == 3);
        CHECK(in_port->getDelay() == 0);  // 只有发送端注入

        // 验证连接可工作
        Packet* pkt = new Packet(nullptr, 0, PKT_REQ_READ);
        pkt->vc_id = 0;
        bool sent = out_port->sendReq(pkt);
        CHECK(sent == true);  // 应能入队或直发
        delete pkt;
    }

    SECTION("MultipleConnections_PerConnectionVCConfig") {
        json config = R"({
            "modules": [
                { "name": "cpu", "type": "MockSim" },
                { "name": "l1", "type": "MockSim" }
            ],
            "connections": [
                {
                    "src": "cpu",
                    "dst": "l1",
                    "input_buffer_sizes": [4],
                    "output_buffer_sizes": [4],
                    "latency": 1
                },
                {
                    "src": "l1",
                    "dst": "cpu",
                    "input_buffer_sizes": [2],
                    "output_buffer_sizes": [2],
                    "latency": 1
                }
            ]
        })"_json;

        ModuleFactory factory(&eq);
        factory.instantiateAll(config);

        auto* cpu = factory.getInstance("cpu");
        auto* l1 = factory.getInstance("l1");

        REQUIRE(cpu != nullptr);
        REQUIRE(l1 != nullptr);

        // cpu -> l1 (request path)
        auto* req_out = cpu->getPortManager().getDownstreamPorts()[0];
        auto* req_in  = l1->getPortManager().getUpstreamPorts()[0];
        CHECK(dynamic_cast<DownstreamPort<MockSim>*>(req_out)->getOutputVCs()[0].capacity == 4);
        CHECK(req_out->getDelay() == 1);

        // l1 -> cpu (response path)
        auto* resp_out = l1->getPortManager().getDownstreamPorts()[0];
        auto* resp_in  = cpu->getPortManager().getUpstreamPorts()[0];
        CHECK(dynamic_cast<DownstreamPort<MockSim>*>(resp_out)->getOutputVCs()[0].capacity == 2);
        CHECK(resp_out->getDelay() == 1);
    }
}
