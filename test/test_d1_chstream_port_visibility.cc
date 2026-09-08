// test/test_d1_chstream_port_visibility.cc
// P0 D.1 修复验证: SimModule::getInternalOutputPort 对 ChStream 模块返回非空
// 作者: CppTLM Team / 日期: 2026-06-19
#include <memory>
#include "chstream_register.hh"
#include "core/event_queue.hh"
#include "core/module_factory.hh"
#include "core/sim_module.hh"
#include "core/sim_object.hh"
#include "tlm/cache_tlm.hh"
#include "tlm/cpu_tlm.hh"
#include "tlm/crossbar_tlm.hh"
#include "tlm/memory_tlm.hh"
#include <catch2/catch_all.hpp>

using json = nlohmann::json;

namespace {

    // 注册所有需要的模块类型 (C++ Catch2 TEST_CASE 间共享)
    static void registerD1Types() {
        static bool registered = false;
        if (!registered) {
            REGISTER_OBJECT;             // CPUSim 已退场 - 展开为 no-op
            REGISTER_CHSTREAM;           // CPUTLM/CacheTLM/MemoryTLM/CrossbarTLM
            REGISTER_MODULE(CpuCluster); // SimModule 容器
            registered = true;
        }
    }

    // 工具: 创建嵌套 SimModule (3 层) 用于端口可见性测试
    struct NestedFixture {
        EventQueue eq;
        CpuCluster root;

        explicit NestedFixture(const std::string& json_str) : root("root", &eq) {
            // parsePortConfigs 由 simulate_instantiate 内部触发
            json cfg = json::parse(json_str);
            root.simulate_instantiate(cfg);
        }
    };

} // namespace

// =====================================================================
// Case 1: 基础 ChStream 模块端口可见性 (CPUTLM)
// =====================================================================
TEST_CASE("D.1: getInternalOutputPort returns non-null for CPUTLM.req_out",
          "[d1_port_visibility]") {
    registerD1Types();
    NestedFixture fix(R"({
        "modules": [
            {"name": "cpu0", "type": "CPUTLM", "n_ports": 1}
        ],
        "connections": []
    })");

    auto* root = dynamic_cast<SimModule*>(&fix.root);
    REQUIRE(root != nullptr);
    auto* port = root->getInternalOutputPort("cpu0.req_out");
    REQUIRE(port != nullptr); // ← 当前 FAIL (返回 nullptr, D.1 修复后 PASS)
    REQUIRE(port->getName() == "cpu0.req_out");
}

// =====================================================================
// Case 2: CacheTLM/MemoryTLM/CrossbarTLM 都验证
// =====================================================================
TEST_CASE("D.1: getInternalOutputPort works for all ChStream module types",
          "[d1_port_visibility]") {
    registerD1Types();
    NestedFixture fix(R"({
        "modules": [
            {"name": "cache0", "type": "CacheTLM", "n_ports": 1},
            {"name": "mem0",   "type": "MemoryTLM", "n_ports": 1},
            {"name": "xbar",   "type": "CrossbarTLM", "n_ports": 4}
        ],
        "connections": []
    })");

    auto* root = dynamic_cast<SimModule*>(&fix.root);
    REQUIRE(root != nullptr);

    // CacheTLM
    REQUIRE(root->getInternalOutputPort("cache0.req_out") != nullptr);
    REQUIRE(root->getInternalInputPort("cache0.resp_in") != nullptr);

    // MemoryTLM
    REQUIRE(root->getInternalOutputPort("mem0.resp_out") != nullptr);
    REQUIRE(root->getInternalInputPort("mem0.req_in") != nullptr);

    // CrossbarTLM (multi-port 带 [i] 后缀)
    REQUIRE(root->getInternalOutputPort("xbar.req_out[0]") != nullptr);
    REQUIRE(root->getInternalOutputPort("xbar.req_out[2]") != nullptr);
}

// =====================================================================
// Case 3: NICTLM 双端口 (DualPort) 两组端口都可见
//
// NICTLM 通过 registerDualPortAdapter (chstream_register.hh:60-62) 注册,
// Step 7 (module_factory.cc:607-650) 为其创建 2 组异构端口:
// 端口 [0] = PE 侧 (CacheReq/Resp), 端口 [1] = Network 侧 (NoCFlit)。
// 与 CrossbarTLM N 路同构端口 (Case 2) 形成对比, 覆盖 DualPortStreamAdapter 路径。
// =====================================================================
TEST_CASE("D.1: NICTLM dual-port both groups visible (PE+Net)", "[d1_port_visibility]") {
    registerD1Types();
    NestedFixture fix(R"({
        "modules": [
            {
                "name": "nic0",
                "type": "NICTLM",
                "params": {"node_id": 0, "mesh_x": 2, "mesh_y": 2}
            }
        ],
        "connections": []
    })");

    auto* root = dynamic_cast<SimModule*>(&fix.root);
    REQUIRE(root != nullptr);
    REQUIRE(root->getInternalOutputPort("nic0.req_out[0]") != nullptr);
    REQUIRE(root->getInternalOutputPort("nic0.req_out[1]") != nullptr);
    REQUIRE(root->getInternalInputPort("nic0.resp_in[0]") != nullptr);
    REQUIRE(root->getInternalInputPort("nic0.resp_in[1]") != nullptr);
}

// =====================================================================
// Case 4: negative - nonexistent 端口仍返回 nullptr (行为不变)
// =====================================================================
TEST_CASE("D.1: getInternalOutputPort returns nullptr for nonexistent port",
          "[d1_port_visibility]") {
    registerD1Types();
    NestedFixture fix(R"({
        "modules": [
            {"name": "cpu0", "type": "CPUTLM", "n_ports": 1}
        ],
        "connections": []
    })");

    auto* root = dynamic_cast<SimModule*>(&fix.root);
    REQUIRE(root != nullptr);
    REQUIRE(root->getInternalOutputPort("cpu0.nonexistent_port") == nullptr);
    REQUIRE(root->getInternalOutputPort("nonexistent_module.req_out") == nullptr);
}