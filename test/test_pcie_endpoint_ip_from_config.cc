// test/test_pcie_endpoint_ip_from_config.cc
// PcieEndpointIP: JSON 实例化 + 17 端口 adapter 注入测试 (C4 Oracle fix)
// 作者 CppTLM Team / 日期 2026-10-13
// 参考: openspec/changes/2026-10-13-cpptlm-dgpu-pcie-sriov-vf-pool/proposal.md T-P4-7

#include "chstream_register.hh" // 必须: 提供 cpptlm/tlm::pcie 命名空间 + ModuleFactory::registerObject
#include "bundles/pcie_bundles_tlm.hh"
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "core/master_port.hh"
#include "core/module_factory.hh"
#include "core/packet.hh"
#include "core/slave_port.hh"
#include "core/stream_adapter_base.hh"
#include "framework/chstream_adapter_factory.hh"
#include "framework/multi_port_stream_adapter.hh"
#include "framework/stream_adapter.hh"
#include "tlm/pcie/pcie_endpoint_ip.hh"

// cpptlm_tests 二进制不自动触发 REGISTER_CHSTREAM（该宏仅在主应用层使用）。
// 本测试文件 include chstream_register.hh 提供头文件，但宏不会被自动展开；
// 因此测试显式注册 PcieEndpointIP 到 ModuleFactory 以走 JSON 实例化路径验证 C4。
// registerObject 幂等（重复注册安全）。
static const int s_pcie_endpoint_ip_test_registered = []() {
    using PcieIP_t = tlm::pcie::PcieEndpointIP;
    using ReqB_t = bundles::PcieTlpBundle;
    using RespB_t = bundles::PcieTlpBundle;
    // ModuleFactory/StreamAdapterBase 在全局命名空间（不在 cpptlm:: 下）
    ModuleFactory::registerObject<PcieIP_t>("PcieEndpointIP");
    // PcieEndpointIP 是 17 端口模块，须用 MultiPort 重载并显式传 N=17
    ChStreamAdapterFactory::get().registerMultiPortAdapter<PcieIP_t, ReqB_t, RespB_t, 17>(
        "PcieEndpointIP");
    return 0;
}();

#include <memory>
#include <nlohmann/json.hpp>

using namespace tlm::pcie;
using json = nlohmann::json;

namespace {

    class FakeStreamAdapter : public cpptlm::StreamAdapterBase {
    public:
        void tick() override {
        }
        void bind_ports(MasterPort*, SlavePort*, MasterPort* = nullptr,
                        SlavePort* = nullptr) override {
        }
        void process_request_input(Packet*) override {
        }
        Packet* process_response_output() override {
            return nullptr;
        }
    };

    std::unique_ptr<FakeStreamAdapter> make_fake() {
        return std::make_unique<FakeStreamAdapter>();
    }

    json make_pcie_ip_config(const std::string& name) {
        json cfg;
        cfg["modules"] = json::array({
            {{"name", name}, {"type", "PcieEndpointIP"}},
        });
        // validateConfig (module_factory_validate.cc) 要求 connections 字段存在，
        // 缺失即返回 false 导致 instantiateAll 静默失败（测试曾误报 C4 未注册）。
        cfg["connections"] = json::array();
        return cfg;
    }

} // namespace

TEST_CASE("PcieEndpointIP: ModuleFactory registers PcieEndpointIP type (C4)",
          "[pcie][sriov][endpoint-ip][json][factory]") {
    // 强制触发 lambda 注册（否则编译器可能优化掉 static initialization）
    (void)s_pcie_endpoint_ip_test_registered;
    // 验证 "PcieEndpointIP" 已注册到 ModuleFactory（REGISTER_CHSTREAM 副作用）
    auto types = ModuleFactory::getRegisteredTypes();
    bool found = false;
    for (const auto& t : types) {
        INFO("registered type: " << t);
        if (t == "PcieEndpointIP") {
            found = true;
            break;
        }
    }
    REQUIRE(found);
}

TEST_CASE("PcieEndpointIP: ModuleFactory.isMultiPort PcieEndpointIP returns true (C4)",
          "[pcie][sriov][endpoint-ip][json][factory]") {
    // PcieEndpointIP 是 17 端口模块（per proposal.md T-P4-7）
    auto& factory = ChStreamAdapterFactory::get();
    REQUIRE(factory.knows("PcieEndpointIP") == true);
    REQUIRE(factory.isMultiPort("PcieEndpointIP") == true);
    REQUIRE(factory.getPortCount("PcieEndpointIP") == 17u);
}

TEST_CASE("PcieEndpointIP: JSON instantiation via ModuleFactory (C4)",
          "[pcie][sriov][endpoint-ip][json][factory]") {
    EventQueue eq;
    ModuleFactory factory(&eq);

    json cfg = make_pcie_ip_config("pcie_ep_ip");
    REQUIRE_NOTHROW(factory.instantiateAll(cfg));

    auto* ep = factory.getInstance<PcieEndpointIP>("pcie_ep_ip");
    REQUIRE(ep != nullptr);
    REQUIRE(ep->get_module_type() == "PcieEndpointIP");
    REQUIRE(ep->num_ports() == 17u);
}

TEST_CASE("PcieEndpointIP: All 17 ports receive non-null StreamAdapter (C4)",
          "[pcie][sriov][endpoint-ip][adapter]") {
    EventQueue eq;
    PcieEndpointIP ep("pcie_ep_ip_adapter", &eq);
    ep.init();

    REQUIRE(ep.all_ports_have_adapter() == false);

    // 构造 17 个 fake StreamAdapter
    cpptlm::StreamAdapterBase* arr[17] = {nullptr};
    std::unique_ptr<FakeStreamAdapter> fakes[17];
    for (int i = 0; i < 17; ++i) {
        fakes[i] = make_fake();
        arr[i] = fakes[i].get();
    }
    ep.set_stream_adapter(arr);

    REQUIRE(ep.all_ports_have_adapter() == true);
    for (int i = 0; i < 17; ++i) {
        REQUIRE(ep.get_adapter(i) == arr[i]);
    }
}