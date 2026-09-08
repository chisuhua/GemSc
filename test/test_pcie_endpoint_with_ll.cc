// test_pcie_endpoint_with_ll.cc
// PcieEndpointTLM + PcieLinkLayer composition 集成测试（不修改 pcie_endpoint_tlm.h）
// Author: CppTLM Team
// Date: 2026-09-08
// 参考: openspec/changes/2026-09-08-cpptlm-dgpu-pcie-link-layer-and-fc/tasks.md §T-P1-7
//       design.md §9.3 (JSON params 扩展新字段, 类布局冻结)

#include "bundles/pcie_bundles_tlm.hh"
#include "bundles/pcie_dllp_bundles_tlm.hh"
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "tlm/gpu/pcie_endpoint_tlm.h"
#include "tlm/pcie/pcie_link_layer_tlm.hh"

#include <memory>
#include <nlohmann/json.hpp>

using namespace tlm::gpu;
using namespace tlm::pcie;
using namespace bundles;
using json = nlohmann::json;

namespace {

    // 构造一个 bar0 MMIO_WRITE 寄存器（side_effect none）→ resp_out[1] 同步响应
    void add_bar0_reg(PcieEndpointTLM& ep, uint32_t off) {
        ep.bar_router().add_register(off, "REG", PcieBarRouter::Access::RW,
                                     PcieBarRouter::SideEffect::NONE, 0);
    }

    PcieTlpBundle make_mmio_write(uint32_t off, uint64_t data) {
        PcieTlpBundle t;
        t.kind.write(PcieTlpBundle::MMIO_WRITE);
        t.offset.write(off);
        t.size.write(4);
        t.data.write(data);
        t.requester_id.write(0x0100);
        t.trans_id.write(1);
        return t;
    }

} // namespace

TEST_CASE("Endpoint+LL: enabled config attaches link layer to endpoint",
          "[pcie][endpoint][ll][compose]") {
    EventQueue eq;
    PcieEndpointTLM ep("pcie_ep_ll", &eq);
    ep.init();

    json cfg;
    cfg["link_layer"]["enabled"] = true;
    cfg["link_layer"]["fc_token_bucket_capacity"] = 16;
    cfg["link_layer"]["fc_initial_credit_p"] = 16;
    cfg["link_layer"]["fc_initial_credit_np"] = 16;
    cfg["link_layer"]["fc_initial_credit_cpl"] = 16;
    ep.set_config(cfg); // → on_config_loaded → attach

    REQUIRE(PcieLinkLayer::for_endpoint("pcie_ep_ll") != nullptr);
    // 类布局未变：num_ports 仍为 4（冻结）
    REQUIRE(ep.num_ports() == PcieEndpointTLM::NUM_PORTS);
}

TEST_CASE("Endpoint+LL: TLP processed through link layer generates ACK DLLP",
          "[pcie][endpoint][ll][compose]") {
    EventQueue eq;
    PcieEndpointTLM ep("pcie_ep_ll2", &eq);
    ep.init();

    json cfg;
    cfg["link_layer"]["enabled"] = true;
    cfg["link_layer"]["fc_token_bucket_capacity"] = 16;
    cfg["link_layer"]["fc_initial_credit_p"] = 16;
    cfg["link_layer"]["fc_initial_credit_np"] = 16;
    cfg["link_layer"]["fc_initial_credit_cpl"] = 16;
    ep.set_config(cfg);       // → on_config_loaded → bar_router init (regs 清空)
    add_bar0_reg(ep, 0x1000); // set_config 之后再注册

    // 注入 MMIO_WRITE
    ep.req_in[PcieEndpointTLM::PORT_SLAVE_IN].data() = make_mmio_write(0x1000, 0xAA);
    ep.req_in[PcieEndpointTLM::PORT_SLAVE_IN].set_valid(true);
    ep.tick();

    // 正常事务层响应（回归路径不受 LL 影响）
    REQUIRE(ep.resp_out[PcieEndpointTLM::PORT_MMIO_OUT].valid() == true);
    // LL 生成 ACK DLLP
    PcieDllpBundle ack;
    REQUIRE(PcieLinkLayer::for_endpoint("pcie_ep_ll2")->try_pop_tx_dllp(ack) == true);
    REQUIRE(ack.is_ack() == true);
}

TEST_CASE("Endpoint+LL: FC backpressure stalls TLP until UpdateFC (Q2)",
          "[pcie][endpoint][ll][compose][backpressure]") {
    EventQueue eq;
    PcieEndpointTLM ep("pcie_ep_ll3", &eq);
    ep.init();

    json cfg;
    cfg["link_layer"]["enabled"] = true;
    cfg["link_layer"]["fc_token_bucket_capacity"] = 2; // P credit = 2
    cfg["link_layer"]["fc_initial_credit_p"] = 2;
    cfg["link_layer"]["fc_initial_credit_np"] = 2;
    cfg["link_layer"]["fc_initial_credit_cpl"] = 2;
    ep.set_config(cfg);
    auto* ll = PcieLinkLayer::for_endpoint("pcie_ep_ll3");
    REQUIRE(ll != nullptr);
    add_bar0_reg(ep, 0x2000); // set_config 之后再注册（bar_router init 清空）

    // 第 1 个 write → 通过（P 2→1）
    ep.req_in[PcieEndpointTLM::PORT_SLAVE_IN].data() = make_mmio_write(0x2000, 1);
    ep.req_in[PcieEndpointTLM::PORT_SLAVE_IN].set_valid(true);
    ep.tick();
    REQUIRE(ep.req_in[PcieEndpointTLM::PORT_SLAVE_IN].valid() == false); // 已消费
    REQUIRE(ep.bar_router().mmio_read(0x2000) == 1u);
    REQUIRE(ep.resp_out[PcieEndpointTLM::PORT_MMIO_OUT].valid() == true);

    // 第 2 个 write → 通过（P 1→0）
    ep.req_in[PcieEndpointTLM::PORT_SLAVE_IN].data() = make_mmio_write(0x2000, 2);
    ep.req_in[PcieEndpointTLM::PORT_SLAVE_IN].set_valid(true);
    ep.tick();
    REQUIRE(ep.req_in[PcieEndpointTLM::PORT_SLAVE_IN].valid() == false);
    REQUIRE(ep.bar_router().mmio_read(0x2000) == 2u);

    // 第 3 个 write → P token 耗尽 → 反压：不消费，MMIO 不落
    ep.req_in[PcieEndpointTLM::PORT_SLAVE_IN].data() = make_mmio_write(0x2000, 3);
    ep.req_in[PcieEndpointTLM::PORT_SLAVE_IN].set_valid(true);
    ep.tick();
    REQUIRE(ep.req_in[PcieEndpointTLM::PORT_SLAVE_IN].valid() == true); // 仍 pending
    REQUIRE(ep.bar_router().mmio_read(0x2000) == 2u);                   // 未写入

    // UpdateFC DLLP 到达（host 侧注入）→ P +2 → 反压解除
    REQUIRE(ll->rx_dllp_from_host(ll->make_update_fc(2, 0, 0)) ==
            PcieLinkLayer::Dispatch::UPDATE_FC);
    ep.tick();
    REQUIRE(ep.req_in[PcieEndpointTLM::PORT_SLAVE_IN].valid() == false); // 已消费
    REQUIRE(ep.bar_router().mmio_read(0x2000) == 3u);
}

TEST_CASE("Endpoint+LL: without link_layer config no LL attached (regression)",
          "[pcie][endpoint][ll][compose][regression]") {
    EventQueue eq;
    PcieEndpointTLM ep("pcie_ep_plain", &eq);
    ep.init();
    add_bar0_reg(ep, 0x3000);

    // 无 link_layer 配置 → 不 attach
    REQUIRE(PcieLinkLayer::for_endpoint("pcie_ep_plain") == nullptr);

    // 回归：EP 正常处理 TLP
    ep.req_in[PcieEndpointTLM::PORT_SLAVE_IN].data() = make_mmio_write(0x3000, 0x55);
    ep.req_in[PcieEndpointTLM::PORT_SLAVE_IN].set_valid(true);
    ep.tick();
    REQUIRE(ep.req_in[PcieEndpointTLM::PORT_SLAVE_IN].valid() == false);
    REQUIRE(ep.resp_out[PcieEndpointTLM::PORT_MMIO_OUT].valid() == true);
    // MMIO_WRITE 响应不回显数据 → 用 bar_router 观察写是否落地
    REQUIRE(ep.bar_router().mmio_read(0x3000) == 0x55u);
}