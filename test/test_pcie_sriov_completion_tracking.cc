// test/test_pcie_sriov_completion_tracking.cc
// SR-IOV Completion Tracking 单元测试 (T-P4-6, per Q12)
// 功能：NP 请求 (trans_id=N) → CplD (同 trans_id) → 匹配并返回；
//       多 outstanding NP 请求各自独立匹配；
//       溢出 (outstanding map 容量上限 N)：N+1 即拒绝新发出。
// 作者 CppTLM Team / 日期 2026-10-13
// 参考: openspec/changes/2026-10-13-cpptlm-dgpu-pcie-sriov-vf-pool/proposal.md T-P4-6
//       decisions.md §Q12 (Completion Timeout out-of-scope + trans_id 关联)
#include "bundles/pcie_bundles_tlm.hh"
#include "catch_amalgamated.hpp"
#include "tlm/pcie/pcie_completion_tracker_tlm.hh"
#include "tlm/pcie/pcie_sriov_vf_pool_tlm.hh"

using namespace tlm::pcie;

TEST_CASE("CompletionTracker: NP request register + CplD match returns data",
          "[pcie][sriov][completion][tracking]") {
    CompletionTracker ct;
    ct.init();

    // NP 请求 trans_id=0x100 → 注册 (挂起)
    REQUIRE(ct.register_np(1, 0x100u) == true);

    // CplD 同 trans_id 到达 → 匹配并返回数据
    CompletionTracker::CplData data{0xCAFEBABE, 4};
    REQUIRE(ct.complete(1, 0x100u, data) == true);
    REQUIRE(ct.completed_count(1) == 1u);
    REQUIRE(ct.outstanding_count(1) == 0u);
}

TEST_CASE("CompletionTracker: per-VF isolation (PF vs VF0)",
          "[pcie][sriov][completion][tracking]") {
    CompletionTracker ct;
    ct.init();

    REQUIRE(ct.register_np(0, 0x100u) == true); // PF
    REQUIRE(ct.register_np(1, 0x100u) == true); // VF0 同 trans_id, 独立注册

    CompletionTracker::CplData data{0x11111111, 4};
    // VF0 complete 只匹配 VF0 的注册
    REQUIRE(ct.complete(1, 0x100u, data) == true);
    REQUIRE(ct.outstanding_count(0) == 1u); // PF 的仍挂起
    REQUIRE(ct.outstanding_count(1) == 0u);

    // PF 完成也独立
    REQUIRE(ct.complete(0, 0x100u, data) == true);
    REQUIRE(ct.outstanding_count(0) == 0u);
}

TEST_CASE("CompletionTracker: multiple outstanding NP requests match independently",
          "[pcie][sriov][completion][tracking]") {
    CompletionTracker ct;
    ct.init();

    REQUIRE(ct.register_np(1, 0x100u) == true);
    REQUIRE(ct.register_np(1, 0x101u) == true);
    REQUIRE(ct.register_np(1, 0x102u) == true);
    REQUIRE(ct.outstanding_count(1) == 3u);

    // 乱序完成: 0x102 先回, 0x100 后回
    CompletionTracker::CplData d2{0x22222222, 4};
    REQUIRE(ct.complete(1, 0x102u, d2) == true);
    REQUIRE(ct.outstanding_count(1) == 2u);

    CompletionTracker::CplData d0{0x00000000, 4};
    REQUIRE(ct.complete(1, 0x100u, d0) == true);
    REQUIRE(ct.outstanding_count(1) == 1u);

    CompletionTracker::CplData d1{0x11111111, 4};
    REQUIRE(ct.complete(1, 0x101u, d1) == true);
    REQUIRE(ct.outstanding_count(1) == 0u);
    REQUIRE(ct.completed_count(1) == 3u);
}

TEST_CASE("CompletionTracker: unmatched CplD returns false",
          "[pcie][sriov][completion][tracking]") {
    CompletionTracker ct;
    ct.init();

    // 未注册的 trans_id 完成 → 不匹配
    CompletionTracker::CplData data{0xDEADBEEF, 4};
    REQUIRE(ct.complete(1, 0x9999u, data) == false);
    REQUIRE(ct.outstanding_count(1) == 0u);
}

TEST_CASE("CompletionTracker: overflow rejected at capacity+1 (per Q12)",
          "[pcie][sriov][completion][tracking]") {
    CompletionTracker ct;
    ct.init();
    REQUIRE(ct.capacity() == CompletionTracker::DEFAULT_CAPACITY);

    // 填满到 capacity
    for (uint32_t i = 0; i < CompletionTracker::DEFAULT_CAPACITY; ++i) {
        REQUIRE(ct.register_np(1, 0x1000u + i) == true);
    }
    REQUIRE(ct.outstanding_count(1) == CompletionTracker::DEFAULT_CAPACITY);

    // N+1 → 拒绝新发出
    REQUIRE(ct.register_np(1, 0xFFFFu) == false);
    REQUIRE(ct.outstanding_count(1) == CompletionTracker::DEFAULT_CAPACITY);

    // 完成一个后, 可再注册
    CompletionTracker::CplData data{1, 4};
    REQUIRE(ct.complete(1, 0x1000u, data) == true);
    REQUIRE(ct.register_np(1, 0xFFFFu) == true);
}

TEST_CASE("CompletionTracker: flr clears outstanding for one VF only",
          "[pcie][sriov][completion][tracking]") {
    CompletionTracker ct;
    ct.init();

    REQUIRE(ct.register_np(0, 0x100u) == true);
    REQUIRE(ct.register_np(1, 0x200u) == true);
    REQUIRE(ct.register_np(1, 0x201u) == true);
    REQUIRE(ct.outstanding_count(0) == 1u);
    REQUIRE(ct.outstanding_count(1) == 2u);

    ct.flr_vf(1);
    REQUIRE(ct.outstanding_count(1) == 0u);
    REQUIRE(ct.outstanding_count(0) == 1u); // PF 不受影响

    // flr_pf 清全部
    ct.flr_pf();
    REQUIRE(ct.outstanding_count(0) == 0u);
}

TEST_CASE("C5: PcieSriovVfPool production path wires dispatch_tlp -> CompletionTracker",
          "[pcie][sriov][completion][tracking][vf-pool]") {
    // C5 Oracle fix: CompletionTracker 必须经 VfPool 生产路径接线
    // (dispatch_tlp CFG_READ 登记 register_np, dispatch_completion 匹配 CplD),
    // 而非只在单元测试中直接使用 CompletionTracker 单体。
    PcieSriovVfPool pool;
    pool.init_all();

    // CFG_READ (NP 请求) → register_np 登记 outstanding
    bundles::PcieTlpBundle tlp_read(bundles::PcieTlpBundle::CFG_READ, 0, 0x04, 4, 0, 0x0000,
                                    0x100u);
    REQUIRE(pool.dispatch_tlp(1, tlp_read) == true);
    REQUIRE(pool.completions().outstanding_count(1) == 1u);

    // CplD 同 trans_id 到达 → dispatch_completion 匹配
    CompletionTracker::CplData data{0xCAFEBABE, 4};
    REQUIRE(pool.dispatch_completion(1, 0x100u, data) == true);
    REQUIRE(pool.completions().completed_count(1) == 1u);
    REQUIRE(pool.completions().outstanding_count(1) == 0u);

    // 未登记 trans_id → false
    REQUIRE(pool.dispatch_completion(1, 0x9999u, data) == false);
}

TEST_CASE("C5: FLR through VfPool clears tracked completions",
          "[pcie][sriov][completion][tracking][vf-pool]") {
    PcieSriovVfPool pool;
    pool.init_all();

    bundles::PcieTlpBundle tlp_read(bundles::PcieTlpBundle::CFG_READ, 0, 0x04, 4, 0, 0x0000,
                                    0x200u);
    REQUIRE(pool.dispatch_tlp(1, tlp_read) == true);
    REQUIRE(pool.dispatch_tlp(2, tlp_read) == true);
    REQUIRE(pool.completions().outstanding_count(1) == 1u);
    REQUIRE(pool.completions().outstanding_count(2) == 1u);

    pool.flr_vf(1);
    REQUIRE(pool.completions().outstanding_count(1) == 0u);
    REQUIRE(pool.completions().outstanding_count(2) == 1u); // VF1 不受影响

    pool.flr_pf();
    REQUIRE(pool.completions().outstanding_count(2) == 0u);
}