// test/test_axi4_mapper_integration_with_pcie.cc
// Axi4Mapper 与 PcieEndpointIP 集成测试 (T-P6-4)
// 功能：验证 JSON axi4_mapper_inject: true 时 mapper 注入 PcieEndpointIP 数据路径
//       端到端读写事务经 mapper 正确路由与完成
//       axi4_mapper_inject 缺省/false 时不注入（无回归）
// 作者 CppTLM Team / 日期 2026-12-22
// 参考: openspec/changes/2026-12-22-cpptlm-dgpu-axi4-mapper/tasks.md T-P6-4

#include "bundles/axi4_bundles_tlm.hh"
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "framework/axi4_bundle_to_signal.hh"
#include "framework/axi4_mapper.hh"
#include "framework/axi4_signal_to_bundle.hh"
#include "tlm/pcie/pcie_axi_adapter_tlm.hh"
#include "tlm/pcie/pcie_endpoint_ip.hh"

#include <cstdint>
#include <nlohmann/json.hpp>

using namespace bundles;
using namespace cpptlm;
using namespace tlm::pcie;

static nlohmann::json mapper_inject_cfg(bool inject) {
    return nlohmann::json{{"axi_adapter",
                           {{"data_width", 512},
                            {"address_width", 64},
                            {"axi4_mapper_inject", inject},
                            {"ports",
                             {{"axi_master_out", {{"enabled", true}, {"type", "axi4"}}},
                              {"axi_slave_in", {{"enabled", true}, {"type", "axi4"}}},
                              {"cfg_slave_in", {{"enabled", true}, {"type", "axi4lite"}}}}}}}};
}

TEST_CASE("Axi4Mapper integration: axi4_mapper_inject=true injects into data path",
          "[axi][mapper][integration][pcie]") {
    EventQueue eq;
    PcieEndpointIP ep("pcie_ep_axi4_mapper", &eq);
    ep.init();
    ep.set_config(mapper_inject_cfg(true));
    ep.on_config_loaded();

    PcieAxiAdapter* adapter = PcieAxiAdapter::for_endpoint("pcie_ep_axi4_mapper");
    REQUIRE(adapter != nullptr);
    Axi4Mapper* mapper = adapter->axi_mapper();
    REQUIRE(mapper != nullptr);
    REQUIRE(mapper->capacity() == 16);

    Axi4Bundle wreq;
    wreq.awid.write(0x1000);
    wreq.awaddr.write(0x1000);
    wreq.awlen.write(0);
    wreq.awsize.write(3);
    wreq.awburst.write(1);

    REQUIRE(mapper->issue_write(wreq) == true);
    REQUIRE(mapper->outstanding_wr() == 1);
    REQUIRE(mapper->complete_write(0x1000) == true);
    REQUIRE(mapper->outstanding_wr() == 0);
}

TEST_CASE("Axi4Mapper integration: read transaction through mapper",
          "[axi][mapper][integration][pcie]") {
    EventQueue eq;
    PcieEndpointIP ep("pcie_ep_axi4_mapper_rd", &eq);
    ep.init();
    ep.set_config(mapper_inject_cfg(true));
    ep.on_config_loaded();

    PcieAxiAdapter* adapter = PcieAxiAdapter::for_endpoint("pcie_ep_axi4_mapper_rd");
    REQUIRE(adapter != nullptr);
    Axi4Mapper* mapper = adapter->axi_mapper();
    REQUIRE(mapper != nullptr);

    Axi4Bundle rreq;
    rreq.arid.write(0x2000);
    rreq.araddr.write(0x2000);
    rreq.arlen.write(0);
    rreq.arsize.write(3);
    rreq.arburst.write(1);

    REQUIRE(mapper->issue_read(rreq) == true);
    REQUIRE(mapper->outstanding_rd() == 1);
    REQUIRE(mapper->complete_read(0x2000, 0xDEADBEEF, true) == true);
    REQUIRE(mapper->outstanding_rd() == 0);
    REQUIRE(mapper->read_data(0x2000) == 0xDEADBEEF);
}

TEST_CASE("Axi4Mapper integration: OOO completion through PcieAxiAdapter",
          "[axi][mapper][integration][pcie]") {
    EventQueue eq;
    PcieEndpointIP ep("pcie_ep_axi4_mapper_ooo", &eq);
    ep.init();
    ep.set_config(mapper_inject_cfg(true));
    ep.on_config_loaded();

    PcieAxiAdapter* adapter = PcieAxiAdapter::for_endpoint("pcie_ep_axi4_mapper_ooo");
    REQUIRE(adapter != nullptr);
    Axi4Mapper* mapper = adapter->axi_mapper();
    REQUIRE(mapper != nullptr);

    Axi4Bundle rreq;
    rreq.arid.write(0xAAAA);
    rreq.araddr.write(0xAAAA);
    rreq.arlen.write(0);
    rreq.arsize.write(3);
    rreq.arburst.write(1);
    REQUIRE(mapper->issue_read(rreq) == true);

    rreq.arid.write(0xBBBB);
    rreq.araddr.write(0xBBBB);
    REQUIRE(mapper->issue_read(rreq) == true);

    rreq.arid.write(0xCCCC);
    rreq.araddr.write(0xCCCC);
    REQUIRE(mapper->issue_read(rreq) == true);
    REQUIRE(mapper->outstanding_rd() == 3);

    REQUIRE(mapper->complete_read(0xCCCC, 0xCCCC0001, true) == true);
    REQUIRE(mapper->outstanding_rd() == 2);
    REQUIRE(mapper->complete_read(0xAAAA, 0xAAAA0001, true) == true);
    REQUIRE(mapper->outstanding_rd() == 1);
    REQUIRE(mapper->complete_read(0xBBBB, 0xBBBB0001, true) == true);
    REQUIRE(mapper->outstanding_rd() == 0);
}

TEST_CASE("Axi4Mapper integration: bundle/signal round-trip in adapter context",
          "[axi][mapper][integration][signal]") {
    EventQueue eq;
    PcieEndpointIP ep("pcie_ep_axi4_mapper_sig", &eq);
    ep.init();
    ep.set_config(mapper_inject_cfg(true));
    ep.on_config_loaded();

    PcieAxiAdapter* adapter = PcieAxiAdapter::for_endpoint("pcie_ep_axi4_mapper_sig");
    REQUIRE(adapter != nullptr);
    Axi4Mapper* mapper = adapter->axi_mapper();
    REQUIRE(mapper != nullptr);

    Axi4Bundle bundle;
    bundle.awaddr.write(0x12345678);
    bundle.awlen.write(0);
    bundle.awsize.write(3);
    bundle.awburst.write(1);
    bundle.awid.write(0xABCD);
    bundle.wdata.write(0x11223344);
    bundle.wstrb.write(0xFF);
    bundle.wlast.write(1);
    bundle.bid.write(0xABCD);
    bundle.bresp.write(0);
    bundle.araddr.write(0x87654321);
    bundle.arlen.write(0);
    bundle.arsize.write(3);
    bundle.arburst.write(1);
    bundle.arid.write(0xDCBA);
    bundle.rid.write(0xDCBA);
    bundle.rdata.write(0x55667788);
    bundle.rresp.write(0);
    bundle.rlast.write(1);

    AXI4Signals signals{};
    bundle_to_signal(bundle, signals);
    Axi4Bundle restored;
    signal_to_bundle(signals, restored);

    REQUIRE(restored.awaddr.read() == bundle.awaddr.read());
    REQUIRE(restored.awid.read() == bundle.awid.read());
    REQUIRE(restored.arid.read() == bundle.arid.read());
    REQUIRE(restored.rid.read() == bundle.rid.read());
    REQUIRE(restored.bid.read() == bundle.bid.read());
}

TEST_CASE("Axi4Mapper integration: no injection when axi4_mapper_inject=false",
          "[axi][mapper][integration][no-inject]") {
    EventQueue eq;
    PcieEndpointIP ep("pcie_ep_no_mapper", &eq);
    ep.init();
    ep.set_config(mapper_inject_cfg(false));
    ep.on_config_loaded();

    PcieAxiAdapter* adapter = PcieAxiAdapter::for_endpoint("pcie_ep_no_mapper");
    REQUIRE(adapter != nullptr);
    const Axi4Mapper* mapper = adapter->axi_mapper();
    REQUIRE(mapper == nullptr);
}

TEST_CASE("Axi4Mapper integration: no injection when axi_adapter absent",
          "[axi][mapper][integration][no-inject]") {
    EventQueue eq;
    PcieEndpointIP ep("pcie_ep_no_adapter", &eq);
    ep.init();
    ep.set_config(nlohmann::json{});
    ep.on_config_loaded();

    PcieAxiAdapter* adapter = PcieAxiAdapter::for_endpoint("pcie_ep_no_adapter");
    REQUIRE(adapter == nullptr);
}

TEST_CASE("Axi4Mapper integration: burst read with multiple beats",
          "[axi][mapper][integration][burst]") {
    EventQueue eq;
    PcieEndpointIP ep("pcie_ep_axi4_mapper_burst", &eq);
    ep.init();
    ep.set_config(mapper_inject_cfg(true));
    ep.on_config_loaded();

    PcieAxiAdapter* adapter = PcieAxiAdapter::for_endpoint("pcie_ep_axi4_mapper_burst");
    REQUIRE(adapter != nullptr);
    Axi4Mapper* mapper = adapter->axi_mapper();
    REQUIRE(mapper != nullptr);

    Axi4Bundle rreq;
    rreq.arid.write(0xB057);
    rreq.araddr.write(0x1000);
    rreq.arlen.write(3);
    rreq.arsize.write(3);
    rreq.arburst.write(1);
    REQUIRE(mapper->issue_read(rreq) == true);

    REQUIRE(mapper->complete_read(0xB057, 0x1111, false) == true);
    REQUIRE(mapper->outstanding_rd() == 1);
    REQUIRE(mapper->read_data(0xB057) == 0x1111);

    REQUIRE(mapper->complete_read(0xB057, 0x2222, false) == true);
    REQUIRE(mapper->read_data(0xB057) == 0x2222);

    REQUIRE(mapper->complete_read(0xB057, 0x3333, false) == true);
    REQUIRE(mapper->read_data(0xB057) == 0x3333);

    REQUIRE(mapper->complete_read(0xB057, 0x4444, true) == true);
    REQUIRE(mapper->outstanding_rd() == 0);
    REQUIRE(mapper->pending_read(0xB057) == nullptr);
}
