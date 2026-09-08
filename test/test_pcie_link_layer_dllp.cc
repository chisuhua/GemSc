// test_pcie_link_layer_dllp.cc
// PcieLinkLayer DLLP gen/parse/dispatch 测试
// Author: CppTLM Team
// Date: 2026-09-08
// 参考: openspec/changes/2026-09-01-cpptlm-dgpu-pcie-ip-microarch/design.md §4/§6
//       specs/link-layer-and-fc/spec.md Scenario "DLLP gen/parse"

#include "bundles/pcie_bundles_tlm.hh"
#include "bundles/pcie_dllp_bundles_tlm.hh"
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "tlm/pcie/pcie_link_layer_tlm.hh"

using namespace tlm::pcie;
using namespace bundles;

TEST_CASE("PcieLinkLayer: gen ACK/NAK DLLP", "[pcie][ll][dllp]") {
    EventQueue eq;
    PcieLinkLayer ll(&eq);

    auto ack = ll.make_ack(/*ack_seq=*/0x0123);
    REQUIRE(ack.is_ack() == true);
    REQUIRE(ack.kind.read() == PcieDllpBundle::ACK);
    REQUIRE(ack.vc_id.read() == 0u); // 单 VC0 (per Q11)
    REQUIRE(ack.seq_num_ack.read() == 0x0123u);
    REQUIRE(ack.seq_num.read() == 0u); // ACK 不携带发送 seq

    auto nak = ll.make_nak(/*nak_seq=*/0x0456);
    REQUIRE(nak.is_nak() == true);
    REQUIRE(nak.kind.read() == PcieDllpBundle::NAK);
    REQUIRE(nak.seq_num_ack.read() == 0x0456u);
}

TEST_CASE("PcieLinkLayer: gen InitFC1/InitFC2/UpdateFC DLLP", "[pcie][ll][dllp]") {
    EventQueue eq;
    PcieLinkLayer ll(&eq);

    auto fc1 = ll.make_init_fc1(/*p=*/0x00AA, /*np=*/0x00BB, /*cpl=*/0x00CC);
    REQUIRE(fc1.is_fc() == true);
    REQUIRE(fc1.is_init_fc() == true);
    REQUIRE(fc1.kind.read() == PcieDllpBundle::INIT_FC1);
    REQUIRE(fc1.credit_P.read() == 0x00AAu);
    REQUIRE(fc1.credit_NP.read() == 0x00BBu);
    REQUIRE(fc1.credit_Cpl.read() == 0x00CCu);

    auto fc2 = ll.make_init_fc2(0x0011, 0x0022, 0x0033);
    REQUIRE(fc2.kind.read() == PcieDllpBundle::INIT_FC2);
    REQUIRE(fc2.credit_P.read() == 0x0011u);
    REQUIRE(fc2.credit_NP.read() == 0x0022u);
    REQUIRE(fc2.credit_Cpl.read() == 0x0033u);

    auto ufc = ll.make_update_fc(0x0005, 0x0006, 0x0007);
    REQUIRE(ufc.is_update_fc() == true);
    REQUIRE(ufc.credit_P.read() == 0x0005u);
    REQUIRE(ufc.credit_NP.read() == 0x0006u);
    REQUIRE(ufc.credit_Cpl.read() == 0x0007u);
}

TEST_CASE("PcieLinkLayer: gen NOP + Vendor DLLP", "[pcie][ll][dllp]") {
    EventQueue eq;
    PcieLinkLayer ll(&eq);

    auto nop = ll.make_nop();
    REQUIRE(nop.is_nop() == true);
    REQUIRE(nop.kind.read() == PcieDllpBundle::NOP);

    auto vendor = ll.make_vendor();
    REQUIRE(vendor.is_vendor() == true);
    REQUIRE(vendor.kind.read() == PcieDllpBundle::VENDOR);
}

TEST_CASE("PcieLinkLayer: dispatch ACK/NAK DLLP to handlers", "[pcie][ll][dllp]") {
    EventQueue eq;
    PcieLinkLayer ll(&eq);

    // 发送 3 个 TLP → retry buffer 有 seq 0,1,2
    PcieTlpBundle t0(/*kind=*/PcieTlpBundle::MMIO_WRITE, 0, 0x1000, 4, 1, 0x0100, 1);
    PcieTlpBundle t1 = t0;
    t1.trans_id.write(2);
    PcieTlpBundle t2 = t0;
    t2.trans_id.write(3);
    REQUIRE(ll.tx_tlp(t0) == true);
    REQUIRE(ll.tx_tlp(t1) == true);
    REQUIRE(ll.tx_tlp(t2) == true);
    REQUIRE(ll.retry_buffer_size() == 3u);

    // ACK seq=1 → 累积清 ≤1, 剩 seq 2
    auto ack1 = ll.make_ack(1);
    const auto r = ll.rx_dllp(ack1);
    REQUIRE(r == PcieLinkLayer::Dispatch::ACK);
    REQUIRE(ll.retry_buffer_size() == 1u);

    // NAK seq=1 → retry buffer 重发 ≥1
    auto nak1 = ll.make_nak(1);
    REQUIRE(ll.rx_dllp(nak1) == PcieLinkLayer::Dispatch::NAK);
}

TEST_CASE("PcieLinkLayer: dispatch FC DLLP updates token bucket", "[pcie][ll][dllp]") {
    EventQueue eq;
    PcieLinkLayer ll(&eq);

    // InitFC1 初始化 credit（设 capacity 上限）→ PF0 桶 P=2
    auto fc1 = ll.make_init_fc1(2, 2, 2);
    REQUIRE(ll.rx_dllp(fc1) == PcieLinkLayer::Dispatch::INIT_FC1);

    REQUIRE(ll.can_send_fc(FcTokenBucket::Type::Posted) == true);
    REQUIRE(ll.consume_fc(FcTokenBucket::Type::Posted) == true);
    REQUIRE(ll.consume_fc(FcTokenBucket::Type::Posted) == true);
    REQUIRE(ll.can_send_fc(FcTokenBucket::Type::Posted) == false);

    auto ufc = ll.make_update_fc(1, 0, 0);
    REQUIRE(ll.rx_dllp(ufc) == PcieLinkLayer::Dispatch::UPDATE_FC);
    REQUIRE(ll.can_send_fc(FcTokenBucket::Type::Posted) == true);
}

TEST_CASE("PcieLinkLayer: dispatch NOP + Vendor no-op", "[pcie][ll][dllp]") {
    EventQueue eq;
    PcieLinkLayer ll(&eq);

    REQUIRE(ll.rx_dllp(ll.make_nop()) == PcieLinkLayer::Dispatch::NOP);
    REQUIRE(ll.rx_dllp(ll.make_vendor()) == PcieLinkLayer::Dispatch::VENDOR);
    REQUIRE(ll.rx_dllp(ll.make_init_fc2(0, 0, 0)) == PcieLinkLayer::Dispatch::INIT_FC2);
}

TEST_CASE("PcieLinkLayer: parse DLLP fields", "[pcie][ll][dllp]") {
    EventQueue eq;
    PcieLinkLayer ll(&eq);

    PcieDllpBundle dllp;
    dllp.kind.write(PcieDllpBundle::UPDATE_FC);
    dllp.vc_id.write(2);
    dllp.credit_P.write(0x0009);
    dllp.credit_NP.write(0x0008);
    dllp.credit_Cpl.write(0x0007);
    dllp.seq_num.write(0x0ABC);
    dllp.seq_num_ack.write(0x0123);
    dllp.trans_id.write(0x55);

    PcieLinkLayer::ParsedDllp parsed;
    REQUIRE(ll.parse_dllp(dllp, parsed) == true);
    REQUIRE(parsed.kind == PcieLinkLayer::Dispatch::UPDATE_FC);
    REQUIRE(parsed.vc_id == 2u);
    REQUIRE(parsed.credit_P == 0x0009u);
    REQUIRE(parsed.credit_NP == 0x0008u);
    REQUIRE(parsed.credit_Cpl == 0x0007u);
    REQUIRE(parsed.seq_num == 0x0ABCu);
    REQUIRE(parsed.seq_num_ack == 0x0123u);
    REQUIRE(parsed.trans_id == 0x55u);
}