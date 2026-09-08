// test/test_simmodule_recursive_bug.cc
// D.4 + D.5 修复验证 + 3 层 JSON E2E 测试
// 修复前：findInternalPath 不递归，3 层 JSON exposed port 路径解析失败
// 修复后：递归到子 SimModule
// 作者: Sisyphus / 日期: 2026-06-19
// 参考: docs/superpowers/specs/2026-06-19-simmodule-complex-hierarchies-design.md §4.1
#include <string>
#include "chstream_register.hh"
#include "core/module_factory.hh"
#include "core/sim_module.hh"
#include "modules.hh"
#include "tlm/cluster/cpu_cluster.hh"
#include "utils/json_includer.hh"
#include <catch2/catch_all.hpp>

// 注册所有需要的模块类型 (CpuCluster + CPUTLM/CacheTLM/MemoryTLM 等)
static void registerSimModuleTypes() {
    static bool registered = false;
    if (!registered) {
        REGISTER_OBJECT;             // CPUSim 已退场 - 展开为 no-op
        REGISTER_CHSTREAM;           // CPUTLM/CacheTLM/MemoryTLM 等
        REGISTER_MODULE(CpuCluster); // CpuCluster + 4 GPU SimModule (via modules_cluster.hh)
        registered = true;
    }
}

TEST_CASE("D.4 findInternalPath recurses nested SimModule", "[simmodule][regression]") {
    // 验证 findInternalPath 递归到子 SimModule
    // 3 层 outer → mid → inner
    // outer.outputs: internal="mid.mid_cpu0_to_bus" (跨层引用)
    // mid.outputs:    internal="inner.inner_cpu0_to_bus"
    // inner.outputs:  internal="cpu0.req_out"
    // 期望 outer.findInternalPath("mid_cpu0_to_bus") 返回 "mid.inner.cpu0.req_out"

    EventQueue eq;
    auto* outer = new CpuCluster("outer", &eq);
    auto* mid = new CpuCluster("mid", &eq);
    auto* inner = new CpuCluster("inner", &eq);
    auto* cu0 = new CpuCluster("cu0_proxy", &eq); // 占位，实际是 CPUTLM
    cu0->setName("cu0");

    // 模拟 build hierarchy
    inner->addInternalInstance(cu0);
    mid->addInternalInstance(inner);
    outer->addInternalInstance(mid);

    // 注册 outputs
    inner->addOutputConfig("cpu0.req_out", "inner_cpu0_to_bus");
    mid->addOutputConfig("inner.inner_cpu0_to_bus", "mid_cpu0_to_bus");
    outer->addOutputConfig("mid.mid_cpu0_to_bus", "outer_cpu0_to_bus");

    REQUIRE(outer->findInternalPath("outer_cpu0_to_bus") == "mid.mid_cpu0_to_bus");
    REQUIRE(outer->findInternalPath("mid_cpu0_to_bus") == "mid.inner.inner_cpu0_to_bus");
    REQUIRE(outer->findInternalPath("inner_cpu0_to_bus") == "mid.inner.cpu0.req_out");

    // 仅 delete outer: outer->internal_factory 销毁时递归 delete mid/inner/cu0
    // (ModuleFactory::~ModuleFactory 迭代 instances 调用 delete)
    delete outer;
}

// D.5: SimModule::tick 应递归到所有子孙 SimObject
// 修复前: SimModule 无默认 tick 实现,继承 SimObject::tick()=0 纯虚
// 修复后: SimModule 基类提供默认递归 tick,所有未重写 tick 的 SimModule 子类自动获得
class TickCounter : public SimObject {
public:
    int tick_count = 0;
    explicit TickCounter(const std::string& n, EventQueue* eq) : SimObject(n, eq) {
    }
    void tick() override {
        ++tick_count;
    }
};

// Stub SimModule: 不重写 tick(),依赖 SimModule 默认实现
// 修复前 (Task 1.4 未实施): SimModule 无默认 tick,StubSimModule 继承 SimObject::tick()=0
//                          → 抽象类,无法实例化 → 此测试编译失败 (演示 D.5 bug)
// 修复后: SimModule 提供默认递归 tick
//         → StubSimModule 可实例化,outer->tick() 递归触发 mid->tick() → counter->tick()
class StubSimModule : public SimModule {
public:
    explicit StubSimModule(const std::string& n, EventQueue* eq) : SimModule(n, eq) {
    }
    // NOTE: no tick() override - relies on SimModule's default recursive behavior
};

TEST_CASE("D.5 SimModule default tick recurses descendants", "[simmodule][regression]") {
    // 3 层嵌套: outer (CpuCluster) → mid (StubSimModule) → counter (TickCounter)
    // outer 用 CpuCluster,因为 CpuCluster 自身已实现手动递归 tick (cpu_cluster.hh:49-55)
    // mid 用 StubSimModule,故意不重写 tick() 暴露 D.5 bug:
    //   - 修复前: 编译失败 (StubSimModule 抽象)
    //   - 修复后: SimModule 默认递归 tick 让 counter 被 tick
    EventQueue eq;
    auto* outer = new CpuCluster("outer", &eq);
    auto* mid = new StubSimModule("mid", &eq);
    auto* counter = new TickCounter("counter", &eq);

    mid->addInternalInstance(counter);
    outer->addInternalInstance(mid);

    // 修复后: outer.tick() 走 CpuCluster::tick 手动循环
    //         → 调到 mid->tick() (SimModule 默认递归,深度 2)
    //         → 调到 counter->tick() (TickCounter)
    outer->tick();
    REQUIRE(counter->tick_count == 1);

    outer->tick();
    REQUIRE(counter->tick_count == 2);

    // 仅 delete outer: outer->internal_factory 销毁时递归 delete mid → counter
    delete outer;
}

// P1-T1.5: 3 层 JSON 端到端回归 (D.4 修复验证)
// 验证 configs/example_simmodule_nested_3level_static.json 跑通 (outer→mid→inner 三层 CpuCluster
// 嵌套)
//
// JSON 暴露映射 (解析见 configs/example_simmodule_nested_3level_static.json):
//   outer.outputs: { internal: "mid.cpu0_to_bus",     external: "outer_to_bus" }
//   mid.outputs:   { internal: "inner.cpu0_to_bus",   external: "mid_to_bus"   }
//   inner.outputs: { internal: "cpu0.req_out",        external: "cpu0_to_bus"  }
//
// 注意: outer's 'outer_to_bus' 命中 outer 自己的 exposed map, 返回 verbatim
//       "mid.cpu0_to_bus" (不递归). 'cpu0_to_bus' 仅 inner 暴露, 触发 D.4
//       递归下钻 outer → mid → inner, 返回 "mid.inner.cpu0.req_out" (路径前缀拼接).
TEST_CASE("[regression] 3-level JSON config runs end-to-end", "[simmodule][regression]") {
    registerSimModuleTypes();
    EventQueue eq;
    ModuleFactory factory(&eq);
    auto config = JsonIncluder::loadAndInclude(
        std::string(CPPTLM_SOURCE_DIR) + "/configs/example_simmodule_nested_3level_static.json");
    REQUIRE_NOTHROW(factory.instantiateAll(config));

    auto* outer_sim = dynamic_cast<SimModule*>(factory.getInstance("outer"));
    REQUIRE(outer_sim != nullptr);

    REQUIRE(outer_sim->findInternalPath("outer_to_bus") == "mid.cpu0_to_bus");
    REQUIRE(outer_sim->findInternalPath("cpu0_to_bus") == "mid.inner.cpu0.req_out");
}
