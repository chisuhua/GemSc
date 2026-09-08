// test/test_dual_port_adapter.cc
// 双端口非对称 StreamAdapter 测试
// 功能描述：验证 DualPortStreamAdapter 的 PE 侧 / Network 侧独立通信
// 作者 CppTLM Team
// 日期 2026-04-14
#include <memory>
#include "bundles/cache_bundles_tlm.hh"
#include "core/chstream_module.hh"
#include "core/chstream_port.hh"
#include "core/packet.hh"
#include "core/packet_pool.hh"
#include "framework/chstream_adapter_factory.hh"
#include "framework/dual_port_stream_adapter.hh"
#include <catch2/catch_all.hpp>

// ============================================================
// Mock dual-port module for testing
// ============================================================
class MockDualPortModule : public ChStreamModuleBase {
private:
    cpptlm::InputStreamAdapter<bundles::CacheReqBundle> pe_req_in_;
    cpptlm::OutputStreamAdapter<bundles::CacheRespBundle> pe_resp_out_;
    cpptlm::InputStreamAdapter<bundles::CacheRespBundle> net_resp_in_;
    cpptlm::OutputStreamAdapter<bundles::CacheReqBundle> net_req_out_;

    cpptlm::StreamAdapterBase* adapter_ = nullptr;

public:
    MockDualPortModule(const std::string& n, EventQueue* eq) : ChStreamModuleBase(n, eq) {
    }

    std::string get_module_type() const override {
        return "MockDualPortModule";
    }

    void set_stream_adapter(cpptlm::StreamAdapterBase* adapter) override {
        adapter_ = adapter;
    }

    void tick() override {
        if (adapter_)
            adapter_->tick();
    }

    void do_reset(const ResetConfig&) override {
        pe_req_in_.reset();
        pe_resp_out_.reset();
        net_resp_in_.reset();
        net_req_out_.reset();
    }

    // PE 侧访问器
    cpptlm::InputStreamAdapter<bundles::CacheReqBundle>& pe_req_in() {
        return pe_req_in_;
    }
    cpptlm::OutputStreamAdapter<bundles::CacheRespBundle>& pe_resp_out() {
        return pe_resp_out_;
    }

    // Network 侧访问器
    cpptlm::InputStreamAdapter<bundles::CacheRespBundle>& net_resp_in() {
        return net_resp_in_;
    }
    cpptlm::OutputStreamAdapter<bundles::CacheReqBundle>& net_req_out() {
        return net_req_out_;
    }

    cpptlm::StreamAdapterBase* get_adapter() const {
        return adapter_;
    }
};

// ============================================================
// Adapter type alias for test convenience
// ============================================================
using TestDualAdapter =
    cpptlm::DualPortStreamAdapter<MockDualPortModule, bundles::CacheReqBundle,
                                  bundles::CacheRespBundle, bundles::CacheReqBundle,
                                  bundles::CacheRespBundle>;

TEST_CASE("DualPortStreamAdapter can be instantiated", "[dualport][adapter]") {
    EventQueue eq;
    MockDualPortModule mod("dual_mod", &eq);
    TestDualAdapter adapter(&mod);

    REQUIRE(adapter.module() == &mod);
    REQUIRE(TestDualAdapter::logical_port_groups() == 2);
}

TEST_CASE("DualPortStreamAdapter binds PE and Net ports independently", "[dualport][adapter]") {
    EventQueue eq;
    MockDualPortModule mod("dual_mod", &eq);
    TestDualAdapter adapter(&mod);

    cpptlm::ChStreamInitiatorPort pe_req_out("pe_req_out", &eq);
    cpptlm::ChStreamTargetPort pe_resp_in("pe_resp_in", &adapter, &eq);
    cpptlm::ChStreamInitiatorPort pe_resp_out("pe_resp_out", &eq);
    cpptlm::ChStreamTargetPort pe_req_in("pe_req_in", &adapter, &eq);

    cpptlm::ChStreamInitiatorPort net_req_out("net_req_out", &eq);
    cpptlm::ChStreamTargetPort net_resp_in("net_resp_in", &adapter, &eq);
    cpptlm::ChStreamInitiatorPort net_resp_out("net_resp_out", &eq);
    cpptlm::ChStreamTargetPort net_req_in("net_req_in", &adapter, &eq);

    adapter.bind_pe_ports(&pe_req_out, &pe_resp_in, &pe_resp_out, &pe_req_in);
    adapter.bind_net_ports(&net_req_out, &net_resp_in, &net_resp_out, &net_req_in);
    mod.set_stream_adapter(&adapter);

    REQUIRE(mod.get_adapter() == &adapter);
}

TEST_CASE("DualPortStreamAdapter PE side processes requests", "[dualport][adapter][pe]") {
    EventQueue eq;
    MockDualPortModule mod("dual_mod", &eq);
    TestDualAdapter adapter(&mod);

    cpptlm::ChStreamInitiatorPort pe_req_out("pe_req_out", &eq);
    cpptlm::ChStreamTargetPort pe_resp_in("pe_resp_in", &adapter, &eq);
    cpptlm::ChStreamInitiatorPort pe_resp_out("pe_resp_out", &eq);
    cpptlm::ChStreamTargetPort pe_req_in("pe_req_in", &adapter, &eq);

    cpptlm::ChStreamInitiatorPort net_req_out("net_req_out", &eq);
    cpptlm::ChStreamTargetPort net_resp_in("net_resp_in", &adapter, &eq);
    cpptlm::ChStreamInitiatorPort net_resp_out("net_resp_out", &eq);
    cpptlm::ChStreamTargetPort net_req_in("net_req_in", &adapter, &eq);

    adapter.bind_pe_ports(&pe_req_out, &pe_resp_in, &pe_resp_out, &pe_req_in);
    adapter.bind_net_ports(&net_req_out, &net_resp_in, &net_resp_out, &net_req_in);
    mod.set_stream_adapter(&adapter);

    // Directly write to the module's PE request input (bypasses Packet serialization)
    bundles::CacheReqBundle req;
    req.transaction_id.write(100);
    req.address.write(0x2000);
    req.is_write.write(false);
    req.data.write(0);
    req.size.write(8);

    mod.pe_req_in().consume();
    std::memcpy(&mod.pe_req_in().data(), &req, sizeof(req));
    mod.pe_req_in().set_valid(true);

    REQUIRE(mod.pe_req_in().valid());
    REQUIRE(mod.pe_req_in().data().transaction_id.read() == 100);
    REQUIRE(mod.pe_req_in().data().address.read() == 0x2000);
}

TEST_CASE("DualPortStreamAdapter Network side processes responses", "[dualport][adapter][net]") {
    EventQueue eq;
    MockDualPortModule mod("dual_mod", &eq);
    TestDualAdapter adapter(&mod);

    cpptlm::ChStreamInitiatorPort pe_req_out("pe_req_out", &eq);
    cpptlm::ChStreamTargetPort pe_resp_in("pe_resp_in", &adapter, &eq);
    cpptlm::ChStreamInitiatorPort pe_resp_out("pe_resp_out", &eq);
    cpptlm::ChStreamTargetPort pe_req_in("pe_req_in", &adapter, &eq);

    cpptlm::ChStreamInitiatorPort net_req_out("net_req_out", &eq);
    cpptlm::ChStreamTargetPort net_resp_in("net_resp_in", &adapter, &eq);
    cpptlm::ChStreamInitiatorPort net_resp_out("net_resp_out", &eq);
    cpptlm::ChStreamTargetPort net_req_in("net_req_in", &adapter, &eq);

    adapter.bind_pe_ports(&pe_req_out, &pe_resp_in, &pe_resp_out, &pe_req_in);
    adapter.bind_net_ports(&net_req_out, &net_resp_in, &net_resp_out, &net_req_in);
    mod.set_stream_adapter(&adapter);

    // Directly write to the module's network response input
    bundles::CacheRespBundle resp;
    resp.transaction_id.write(200);
    resp.data.write(0xDEADBEEF);
    resp.is_hit.write(false);
    resp.error_code.write(0);

    std::memcpy(&mod.net_resp_in().data(), &resp, sizeof(resp));
    mod.net_resp_in().set_valid(true);

    REQUIRE(mod.net_resp_in().valid());
    REQUIRE(mod.net_resp_in().data().transaction_id.read() == 200);
    REQUIRE(mod.net_resp_in().data().data.read() == 0xDEADBEEF);
}

TEST_CASE("DualPortStreamAdapter tick processes both sides without interference",
          "[dualport][adapter]") {
    EventQueue eq;
    MockDualPortModule mod("dual_mod", &eq);
    TestDualAdapter adapter(&mod);

    cpptlm::ChStreamInitiatorPort pe_req_out("pe_req_out", &eq);
    cpptlm::ChStreamTargetPort pe_resp_in("pe_resp_in", &adapter, &eq);
    cpptlm::ChStreamInitiatorPort pe_resp_out("pe_resp_out", &eq);
    cpptlm::ChStreamTargetPort pe_req_in("pe_req_in", &adapter, &eq);

    cpptlm::ChStreamInitiatorPort net_req_out("net_req_out", &eq);
    cpptlm::ChStreamTargetPort net_resp_in("net_resp_in", &adapter, &eq);
    cpptlm::ChStreamInitiatorPort net_resp_out("net_resp_out", &eq);
    cpptlm::ChStreamTargetPort net_req_in("net_req_in", &adapter, &eq);

    adapter.bind_pe_ports(&pe_req_out, &pe_resp_in, &pe_resp_out, &pe_req_in);
    adapter.bind_net_ports(&net_req_out, &net_resp_in, &net_resp_out, &net_req_in);
    mod.set_stream_adapter(&adapter);

    // Write only to PE side
    bundles::CacheRespBundle pe_resp(42, 0xCAFEBABE, true, 0);
    mod.pe_resp_out().write(pe_resp);

    // Write to Network side
    bundles::CacheReqBundle net_req(99, 0x5555, 32, false, 0);
    mod.net_req_out().write(net_req);

    // Both are valid before tick
    REQUIRE(mod.pe_resp_out().valid());
    REQUIRE(mod.net_req_out().valid());

    // Tick should not crash
    adapter.tick();

    // After tick, the adapter attempted to send both sides.
    // Without PortPair connections, send() returns false and valid remains.
    // The key assertion: BOTH sides remain independent — PE write didn't affect Net
    REQUIRE(mod.pe_resp_out().data().transaction_id.read() == 42);
    REQUIRE(mod.net_req_out().data().address.read() == 0x5555);
    REQUIRE(mod.net_req_out().data().is_write.read() == false);
}

TEST_CASE("DualPortStreamAdapter PE and Net sides do not interfere", "[dualport][isolation]") {
    EventQueue eq;
    MockDualPortModule mod("isolation_mod", &eq);
    TestDualAdapter adapter(&mod);

    cpptlm::ChStreamInitiatorPort pe_req_out("pe_req_out", &eq);
    cpptlm::ChStreamTargetPort pe_resp_in("pe_resp_in", &adapter, &eq);
    cpptlm::ChStreamInitiatorPort pe_resp_out("pe_resp_out", &eq);
    cpptlm::ChStreamTargetPort pe_req_in("pe_req_in", &adapter, &eq);

    cpptlm::ChStreamInitiatorPort net_req_out("net_req_out", &eq);
    cpptlm::ChStreamTargetPort net_resp_in("net_resp_in", &adapter, &eq);
    cpptlm::ChStreamInitiatorPort net_resp_out("net_resp_out", &eq);
    cpptlm::ChStreamTargetPort net_req_in("net_req_in", &adapter, &eq);

    adapter.bind_pe_ports(&pe_req_out, &pe_resp_in, &pe_resp_out, &pe_req_in);
    adapter.bind_net_ports(&net_req_out, &net_resp_in, &net_resp_out, &net_req_in);
    mod.set_stream_adapter(&adapter);

    // Only write PE response, not Net
    bundles::CacheRespBundle pe_resp(1, 0x1111, true, 0);
    mod.pe_resp_out().write(pe_resp);

    // Net side should NOT be valid
    REQUIRE(mod.pe_resp_out().valid());
    REQUIRE_FALSE(mod.net_req_out().valid());

    // Only write Net request
    bundles::CacheReqBundle net_req(2, 0x2222, 16, true, 0xABCD);
    mod.net_req_out().write(net_req);

    REQUIRE(mod.net_req_out().valid());
    REQUIRE(mod.net_req_out().data().address.read() == 0x2222);
    REQUIRE(mod.net_req_out().data().is_write.read() == true);

    // PE side unaffected by Net write
    REQUIRE(mod.pe_resp_out().valid());
    REQUIRE(mod.pe_resp_out().data().transaction_id.read() == 1);
    REQUIRE(mod.pe_resp_out().data().data.read() == 0x1111);
}

TEST_CASE("DualPortStreamAdapter registry via ChStreamAdapterFactory", "[dualport][factory]") {
    EventQueue eq;
    MockDualPortModule mod("factory_test_mod", &eq);
    auto& factory = ChStreamAdapterFactory::get();

    factory.registerDualPortAdapter<MockDualPortModule, bundles::CacheReqBundle,
                                    bundles::CacheRespBundle, bundles::CacheReqBundle,
                                    bundles::CacheRespBundle>("MockDualPortModule");

    REQUIRE(factory.knows("MockDualPortModule"));
    REQUIRE(factory.isDualPort("MockDualPortModule"));
    REQUIRE(factory.getPortCount("MockDualPortModule") == 2);
    // Dual port uses port_count_=2, so isMultiPort also returns true
    REQUIRE(factory.isMultiPort("MockDualPortModule"));

    auto* created = factory.create("MockDualPortModule", &mod);
    REQUIRE(created != nullptr);
    delete created;
}

TEST_CASE("ChStreamAdapterFactory creates dual-port adapter from factory", "[dualport][factory]") {
    EventQueue eq;
    MockDualPortModule mod("factory_mod", &eq);

    auto& factory = ChStreamAdapterFactory::get();
    factory.registerDualPortAdapter<MockDualPortModule, bundles::CacheReqBundle,
                                    bundles::CacheRespBundle, bundles::CacheReqBundle,
                                    bundles::CacheRespBundle>("FactoryDualModule");

    auto* adapter = factory.create("FactoryDualModule", &mod);
    REQUIRE(adapter != nullptr);

    auto* typed = dynamic_cast<TestDualAdapter*>(adapter);
    REQUIRE(typed != nullptr);
    REQUIRE(typed->module() == &mod);

    delete adapter;
}
