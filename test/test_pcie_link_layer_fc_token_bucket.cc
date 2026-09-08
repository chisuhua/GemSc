// test_pcie_link_layer_fc_token_bucket.cc
// FcTokenBucket + FcEngine: FC Token Bucket 单元测试 (per Q2, 无自动 refill)
// Author: CppTLM Team
// Date: 2026-09-08
// 参考: openspec/changes/2026-09-01-cpptlm-dgpu-pcie-ip-microarch/decisions.md §Q2
//       specs/link-layer-and-fc/spec.md Scenario "FC Token Bucket 基础行为"

#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "tlm/pcie/pcie_flow_control_token_bucket.hh"

using namespace tlm::pcie;

TEST_CASE("FcTokenBucket: can_send true with default capacity", "[pcie][fc][token]") {
    FcTokenBucket bucket;
    REQUIRE(bucket.can_send(FcTokenBucket::Type::Posted) == true);
    REQUIRE(bucket.can_send(FcTokenBucket::Type::NonPosted) == true);
    REQUIRE(bucket.can_send(FcTokenBucket::Type::Completion) == true);
}

TEST_CASE("FcTokenBucket: consume deducts tokens when sufficient", "[pcie][fc][token]") {
    FcTokenBucket bucket;
    // 默认 capacity=256 → 初始 token = capacity
    const bool ok = bucket.consume(FcTokenBucket::Type::Posted);
    REQUIRE(ok == true);
    // 消耗后 token 减少: can_send 仍 true (剩余 255), 再 consume 验证抵扣
    REQUIRE(bucket.consume(FcTokenBucket::Type::Posted) == true);
    REQUIRE(bucket.consume(FcTokenBucket::Type::NonPosted) == true);
    REQUIRE(bucket.consume(FcTokenBucket::Type::Completion) == true);
}

TEST_CASE("FcTokenBucket: over-consume returns false and does not deduct", "[pcie][fc][token]") {
    // 构造低容量桶验证 over-consume 行为
    FcTokenBucket low_bucket(/*capacity=*/2);
    REQUIRE(low_bucket.can_send(FcTokenBucket::Type::Posted) == true);
    REQUIRE(low_bucket.consume(FcTokenBucket::Type::Posted) == true);
    REQUIRE(low_bucket.consume(FcTokenBucket::Type::Posted) == true);
    // 第 3 次 consume: token 0 < weight 1 → false, 不扣减
    REQUIRE(low_bucket.consume(FcTokenBucket::Type::Posted) == false);
    REQUIRE(low_bucket.can_send(FcTokenBucket::Type::Posted) == false);
}

TEST_CASE("FcTokenBucket: update() only increases credits (monotonic, per Q2)",
          "[pcie][fc][token]") {
    // weight=1, capacity=8
    FcTokenBucket bucket(/*capacity=*/8);
    REQUIRE(bucket.consume(FcTokenBucket::Type::Posted) == true); // 7 剩余
    REQUIRE(bucket.consume(FcTokenBucket::Type::Posted) == true); // 6 剩余

    bucket.update(FcTokenBucket::Type::Posted, 3); // 6+3=9 → cap at 8
    REQUIRE(bucket.can_send(FcTokenBucket::Type::Posted) == true);
    // 单调非减: update 不减少 token
    const auto before = bucket.token_count(FcTokenBucket::Type::Posted);
    bucket.update(FcTokenBucket::Type::Posted, 0);
    REQUIRE(bucket.token_count(FcTokenBucket::Type::Posted) == before);
    // 不超 capacity: 连续 update 不超 cap
    bucket.update(FcTokenBucket::Type::Posted, 100);
    REQUIRE(bucket.token_count(FcTokenBucket::Type::Posted) == 8u);
}

TEST_CASE("FcTokenBucket: no auto-refill — update only path (per Q2)", "[pcie][fc][token]") {
    FcTokenBucket bucket(/*capacity=*/2);
    bucket.consume(FcTokenBucket::Type::Completion);
    bucket.consume(FcTokenBucket::Type::Completion);
    REQUIRE(bucket.can_send(FcTokenBucket::Type::Completion) == false);
    // 无自动 refill: 不调用 update 则一直阻塞
    REQUIRE(bucket.can_send(FcTokenBucket::Type::Completion) == false);
    // UpdateFC 到达后恢复
    bucket.update(FcTokenBucket::Type::Completion, 1);
    REQUIRE(bucket.can_send(FcTokenBucket::Type::Completion) == true);
    // 死锁防护: 耗尽 → 阻塞 → UpdateFC → 恢复 (非自动 refill 恢复)
    REQUIRE(bucket.consume(FcTokenBucket::Type::Completion) == true);
    REQUIRE(bucket.can_send(FcTokenBucket::Type::Completion) == false);
}

TEST_CASE("FcTokenBucket: deadlock protection — credit exhausted then UpdateFC recovery",
          "[pcie][fc][token]") {
    FcTokenBucket bucket(/*capacity=*/1, /*weight=*/1);
    REQUIRE(bucket.consume(FcTokenBucket::Type::Posted) == true);
    REQUIRE(bucket.can_send(FcTokenBucket::Type::Posted) == false);
    // UpdateFC 到达 → token 恢复 → 可继续发送 (spec: "不会死锁")
    bucket.update(FcTokenBucket::Type::Posted, 1);
    REQUIRE(bucket.can_send(FcTokenBucket::Type::Posted) == true);
}

TEST_CASE("FcTokenBucket: weights per type honored", "[pcie][fc][token]") {
    // weight: P=2, NP=1, Cpl=1, capacity=4（每 Type 独立桶）
    FcTokenBucket bucket(/*capacity=*/4, /*weight_p=*/2, /*weight_np=*/1, /*weight_cpl=*/1);
    REQUIRE(bucket.can_send(FcTokenBucket::Type::Posted) == true);
    REQUIRE(bucket.consume(FcTokenBucket::Type::Posted) == true);
    REQUIRE(bucket.consume(FcTokenBucket::Type::Posted) == true);
    REQUIRE(bucket.consume(FcTokenBucket::Type::Posted) == false);
    // NP/Cpl 桶独立于 P 桶（per spec "P/NP/Cpl 各一个桶"）
    REQUIRE(bucket.can_send(FcTokenBucket::Type::NonPosted) == true);
    REQUIRE(bucket.can_send(FcTokenBucket::Type::Completion) == true);
    REQUIRE(bucket.consume(FcTokenBucket::Type::NonPosted) == true);
    REQUIRE(bucket.consume(FcTokenBucket::Type::Completion) == true);
}

TEST_CASE("FcEngine: per-VF bucket isolation", "[pcie][fc][token][vf]") {
    EventQueue eq;
    FcEngine engine(&eq);

    // VF0 桶与 VF1 桶独立
    auto& vf0 = engine.bucket(0);
    auto& vf1 = engine.bucket(1);

    REQUIRE(&vf0 != &vf1);

    // VF0 耗尽 → VF1 不受影响
    FcTokenBucket small_vf0(/*capacity=*/1);
    engine.install_bucket(0, std::move(small_vf0));
    auto& vf0b = engine.bucket(0);
    REQUIRE(vf0b.can_send(FcTokenBucket::Type::Posted) == true);
    REQUIRE(vf0b.consume(FcTokenBucket::Type::Posted) == true);
    REQUIRE(vf0b.can_send(FcTokenBucket::Type::Posted) == false); // 已耗尽

    auto& vf1b = engine.bucket(1);
    REQUIRE(vf1b.can_send(FcTokenBucket::Type::Posted) == true); // 不受影响
    REQUIRE(vf1b.consume(FcTokenBucket::Type::Posted) == true);
    REQUIRE(vf1b.can_send(FcTokenBucket::Type::Posted) == true); // 默认大容量
}

TEST_CASE("FcEngine: convenience consume/update/can_send delegate per-VF",
          "[pcie][fc][token][vf]") {
    EventQueue eq;
    FcEngine engine(&eq);

    engine.install_bucket(2, FcTokenBucket(/*capacity=*/1));
    REQUIRE(engine.can_send(2, FcTokenBucket::Type::Posted) == true);
    REQUIRE(engine.consume(2, FcTokenBucket::Type::Posted) == true);
    REQUIRE(engine.can_send(2, FcTokenBucket::Type::Posted) == false);

    engine.update(2, FcTokenBucket::Type::Posted, 1);
    REQUIRE(engine.can_send(2, FcTokenBucket::Type::Posted) == true);
}