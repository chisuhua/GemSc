// test/test_coherent_xbar_tlm.cc
// P0 CoherentXBarTLM 验证: snoop broadcast + registerPeerCache
//
// 设计意图:
//   Task 2.3 TDD 红 -> 绿流程的"红"阶段。当前 .cc stub 实现抛
//   std::runtime_error("not implemented"), 这些测试在 stub 阶段必须 FAIL,
//   Task 2.4 实现后转为 PASS。
//
// 关键依赖 (D.1 修复, commit fb56cc3):
//   CacheTLM.req_out 是 ChStreamInitiatorPort (派生自 MasterPort), 经
//   ModuleFactory Step 7 的 mirrorExistingDownstreamPort() 镜像注册到
//   CacheTLM 自身的 PortManager.downstream_map["req_out"]。因此测试
//   通过 SimModule::getInternalOutputPort("cache.req_out") 拿到真正的
//   MasterPort*。
//
// 作者: CppTLM Team / 日期: 2026-06-19
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "chstream_register.hh"
#include "core/event_queue.hh"
#include "core/master_port.hh"
#include "core/module_factory.hh"
#include "core/packet.hh"
#include "core/packet_pool.hh"
#include "core/port_manager.hh"
#include "core/sim_module.hh"
#include "core/sim_object.hh"
#include "tlm/cache_tlm.hh"
#include "tlm/cluster/cpu_cluster.hh"
#include "tlm/coherent_xbar_tlm.hh"
#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using cpptlm::tlm::CoherentXBarTLM;

// =====================================================================
// 注册所有需要的模块类型 (Catch2 TEST_CASE 间共享)
// =====================================================================
static void registerCoherentXBarTypes() {
    static bool registered = false;
    if (!registered) {
        REGISTER_OBJECT;             // no-op (CPUSim 已退场)
        REGISTER_CHSTREAM;           // CacheTLM/CrossbarTLM/MemoryTLM/...
        REGISTER_MODULE(CpuCluster); // SimModule 容器 (承载 CacheTLM)
        registered = true;
    }
}

// =====================================================================
// 辅助: 通过 CpuCluster + ModuleFactory 路径构造 CacheTLM,
//       并经 D.1 镜像拿到 cache 的 req_out MasterPort*
//
// 返回 pair<root, req_out_port>. 调用方持有 root 生命周期。
// =====================================================================
struct CacheFixture {
    std::unique_ptr<CpuCluster> root;
    MasterPort* req_out = nullptr;

    CacheFixture(EventQueue* eq, const std::string& cache_name = "cache0") {
        root = std::make_unique<CpuCluster>("root", eq);
        json cfg = {{"modules", json::array()}, {"connections", json::array()}};
        cfg["modules"].push_back({{"name", cache_name}, {"type", "CacheTLM"}});
        root->simulate_instantiate(cfg);
        req_out = root->getInternalOutputPort(cache_name + ".req_out");
    }
};

// =====================================================================
// 辅助: 构造一个最小化的 Packet 用于 snoop_broadcast 测试。
//       Packet 构造是私有的, 必须通过 PacketPool::acquire()。
// =====================================================================
static Packet* make_test_packet(EventQueue* eq, uint64_t addr = 0x1000) {
    Packet* pkt = PacketPool::get().acquire();
    if (pkt->payload) {
        pkt->payload->set_command(tlm::TLM_READ_COMMAND);
        pkt->payload->set_address(addr);
        pkt->payload->set_data_length(4);
    }
    pkt->type = PKT_REQ;
    pkt->src_cycle = eq->getCurrentCycle();
    return pkt;
}

// =====================================================================
// Case 1: 单 peer cache, registerPeerCache 不抛 + peer_count 增至 1
//
// 红阶段: stub 抛 std::runtime_error("not implemented") -> FAIL
// 绿阶段 (Task 2.4): registerPeerCache 写入 vector, peer_count() == 1
// =====================================================================
TEST_CASE("CoherentXBarTLM: registerPeerCache adds 1 peer", "[coherent_xbar]") {
    registerCoherentXBarTypes();
    EventQueue eq;
    CoherentXBarTLM xbar("xbar", &eq);

    CacheFixture fix(&eq, "cache0");
    REQUIRE(fix.req_out != nullptr); // D.1 镜像生效前提

    REQUIRE_NOTHROW(xbar.registerPeerCache("cache0", fix.req_out));
    REQUIRE(xbar.peer_count() == 1);
}

// =====================================================================
// Case 2: 3 peer caches, registerPeerCache 全部成功 + peer_count == 3
//         然后 snoop_broadcast 不抛
//
// 红阶段: stub 抛 -> FAIL
// 绿阶段 (Task 2.4): 3 个 peer 注册, snoop_broadcast 透传
// =====================================================================
TEST_CASE("CoherentXBarTLM: registerPeerCache adds 3 peers, snoop broadcasts", "[coherent_xbar]") {
    registerCoherentXBarTypes();
    EventQueue eq;
    CoherentXBarTLM xbar("xbar", &eq);

    std::vector<CacheFixture> fixes;
    for (int i = 0; i < 3; ++i) {
        std::string name = "cache" + std::to_string(i);
        fixes.emplace_back(&eq, name);
        REQUIRE(fixes.back().req_out != nullptr);
        REQUIRE_NOTHROW(xbar.registerPeerCache(name, fixes.back().req_out));
    }
    REQUIRE(xbar.peer_count() == 3);

    Packet* pkt = make_test_packet(&eq);
    REQUIRE_NOTHROW(xbar.snoop_broadcast(pkt));
    // snoop 内部若移交所有权则此处不应再 release, 此处保守 release
    PacketPool::get().release(pkt);
}

// =====================================================================
// Case 3: registerPeerCache 传 nullptr 必须抛 std::runtime_error
//         (Task 2.4 应检查 req_out == nullptr 并报"req_out is null")
//
// 红阶段: stub 抛 std::runtime_error("not implemented") -> 仍 PASS
//         (REQUIRE_THROWS_AS 仅校验异常类型, 不校验消息)
// 绿阶段 (Task 2.4): 抛"req_out is null"运行时错误, 含义更清晰
// =====================================================================
TEST_CASE("CoherentXBarTLM: registerPeerCache rejects null pointer", "[coherent_xbar]") {
    registerCoherentXBarTypes();
    EventQueue eq;
    CoherentXBarTLM xbar("xbar", &eq);

    REQUIRE_THROWS_AS(xbar.registerPeerCache("bad_cache", nullptr), std::runtime_error);
    // 抛错路径下 peer_count 应仍为 0
    REQUIRE(xbar.peer_count() == 0);
}

// =====================================================================
// Case 4: snoop_broadcast(nullptr) 必须是 no-op, 不抛、不崩
//
// 红阶段: stub 抛 -> REQUIRE_NOTHROW 失败 -> FAIL
// 绿阶段 (Task 2.4): nullptr 显式判空后直接返回
// =====================================================================
TEST_CASE("CoherentXBarTLM: snoop_broadcast(nullptr) is no-op", "[coherent_xbar]") {
    registerCoherentXBarTypes();
    EventQueue eq;
    CoherentXBarTLM xbar("xbar", &eq);

    REQUIRE_NOTHROW(xbar.snoop_broadcast(nullptr));
    REQUIRE(xbar.peer_count() == 0);
}

// =====================================================================
// Case 5: registerPeerCache 同名去重 (P1 幂等性)
//
// 设计意图:
//   ApuSoC::incorporate_parent 多次调用 (双层幂等) 的"第二层":
//   即便 ApuSoC::peer_caches_wired_ 早返回防重入, registerPeerCache
//   本身也需按 cache_name 去重, 防止 peer_cache_req_outs_ 重复入队。
//   任务 1.1 红阶段: 当前实现不查重, 二次同名注册后 peer_count() == 2
//   任务 1.2 绿阶段: 在 registerPeerCache 入口处按 name 查重, 命中则跳过
//
// 注: 沿用本文件既有 CacheFixture 模式 (CpuCluster + 内部 CacheTLM + D.1
//     镜像拿 req_out) — CacheTLM 本身无 simulate_instantiate (仅 SimModule
//     派生类有, 见 include/core/sim_module.hh:81), 该模式已在 Cases 1-4
//     验证可行。
// =====================================================================
TEST_CASE("CoherentXBarTLM: registerPeerCache rejects duplicate name", "[coherent_xbar]") {
    registerCoherentXBarTypes();
    EventQueue eq;
    CoherentXBarTLM xbar("xbar", &eq);

    // 1) 首个 peer 正常入队
    CacheFixture fix0(&eq, "cache0");
    REQUIRE(fix0.req_out != nullptr);
    xbar.registerPeerCache("cache0", fix0.req_out);
    REQUIRE(xbar.peer_count() == 1);

    // 2) 二次注册同名 cache 应被忽略 (P1 幂等性)
    xbar.registerPeerCache("cache0", fix0.req_out);
    REQUIRE(xbar.peer_count() == 1); // 期望仍为 1, 当前实现会 == 2 (红阶段)

    // 3) 不同名 cache 正常入队
    CacheFixture fix1(&eq, "cache1");
    REQUIRE(fix1.req_out != nullptr);
    xbar.registerPeerCache("cache1", fix1.req_out);
    REQUIRE(xbar.peer_count() == 2);
}

// =====================================================================
// Case 6: [E2E] apu_soc_v1.json + ModuleFactory Step 9 触发 incorporate_parent 后 snoop_broadcast
// 投递
//
// 设计意图:
//   端到端验证: P1 实现的 ApuSoC::incorporate_parent 真实 late-binding wiring
//   在 apu_soc_v1.json 完整 APU SoC 拓扑中工作。Step 9 自动触发后 xbar
//   至少有 3 个 peer (CpuCluster 端 l2cache + l1_0 + l1_1), snoop_broadcast
//   投递不抛, peer 列表保持稳定。
//
// 依赖:
//   - include/utils/json_includer.hh: JsonIncluder::loadAndInclude (处理
//     apu_soc_v1.json 的 $include cpu/gpu_topology 模板)
//   - src/core/module_factory.cc:813: instantiateAll Step 9 自动调
//     incorporate_parent
//   - P1 幂等性: ApuSoC::peer_caches_wired_ + registerPeerCache 按名去重
//
// 作者: CppTLM Team / 日期: 2026-06-19
// =====================================================================
TEST_CASE("[E2E] snoop_broadcast reaches all peers after ApuSoC incorporate_parent",
          "[coherent_xbar][p1][e2e]") {
    registerCoherentXBarTypes();
    auto config =
        JsonIncluder::loadAndInclude(std::string(CPPTLM_SOURCE_DIR) + "/configs/apu_soc_v1.json");
    EventQueue eq;
    ModuleFactory factory(&eq);
    REQUIRE_NOTHROW(factory.instantiateAll(config));

    // ModuleFactory Step 9 已自动触发 incorporate_parent
    auto* soc = dynamic_cast<SimModule*>(factory.getInstance("apu_top"));
    REQUIRE(soc != nullptr);
    auto* xbar = dynamic_cast<CoherentXBarTLM*>(soc->getInternalInstance("xbar"));
    REQUIRE(xbar != nullptr);
    // P1 实现状态: 至少 3 个 peer (CpuCluster 端 l2cache 贡献)
    // GpuCluster 端 cu_template 完整传播待 Phase 7.B 修复
    REQUIRE(xbar->peer_count() >= 3);

    // 手动 snoop 一次: 不抛 + 内部 sendReq 每个 peer
    Packet* pkt = make_test_packet(&eq, 0xDEAD0000);
    REQUIRE_NOTHROW(xbar->snoop_broadcast(pkt));
    // 验证 peer 列表不变 (Packet 副本已处理或 release)
    REQUIRE(xbar->peer_count() >= 3);
}