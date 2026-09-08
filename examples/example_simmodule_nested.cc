// examples/example_simmodule_nested.cc
// SimModule 多层嵌套示例 (Stage 4, openspec/json-nested-simmodule)
//
// 功能描述:
//   加载 JSON 配置, 通过 ModuleFactory 实例化嵌套 CpuCluster 拓扑,
//   打印拓扑结构 + 端口接线, 启动 tick 并运行 10000 cycles.
//
//   演示内容:
//   1. REGISTER_ALL + REGISTER_MODULE 一键注册所有 SimObject/SimModule 类型
//   2. JsonIncluder::loadAndInclude 处理 JSON include 链
//   3. factory.instantiateAll 触发顶层 CpuCluster 递归激活 (Step 4.5)
//   4. 打印每层 CpuCluster 的内部实例数 + 暴露端口
//   5. factory.startAllTicks 启动所有顶层模块 tick
//   6. EventQueue.run(10000) 跑 10000 cycles
//
// 用法:
//   ./build/bin/example_simmodule_nested <config.json>
//
// 作者: CppTLM Team
// 日期: 2026-06-18

#include "chstream_register.hh"
#include "core/event_queue.hh"
#include "core/module_factory.hh"
#include "core/sim_module.hh"
#include "modules.hh"
#include "tlm/cluster/cpu_cluster.hh"
#include "utils/json_includer.hh"

#include <cstdlib>
#include <iostream>
#include <string>

using json = nlohmann::json;

namespace {

    // 递归打印 SimModule 嵌套拓扑 (按 JSON 层级)
    // 每个 CpuCluster 节点打印: 名称 / num_cpus / cluster_id / 内部实例数 / 暴露端口数
    void printTopology(const CpuCluster* cluster, int indent) {
        if (!cluster)
            return;
        std::string pad(indent * 2, ' ');
        const auto& factory = cluster->getInternalFactory();
        std::cout << pad << "CpuCluster '" << cluster->getName()
                  << "' num_cpus=" << cluster->num_cpus();
        if (!cluster->cluster_id().empty()) {
            std::cout << " cluster_id=\"" << cluster->cluster_id() << "\"";
        }
        std::cout << " internal_instances=" << factory.getAllInstances().size()
                  << " outputs=" << cluster->getOutputConfigs().size()
                  << " inputs=" << cluster->getInputConfigs().size() << "\n";

        for (const auto& kv : factory.getAllInstances()) {
            auto* child = dynamic_cast<CpuCluster*>(kv.second.get());
            if (child) {
                printTopology(child, indent + 1);
            }
        }
    }

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <config.json>\n"
                  << "Example: " << argv[0] << " ../configs/example_simmodule_nested_2level.json\n";
        return 1;
    }

    const std::string config_path = argv[1];
    std::cout << "[example_simmodule_nested] Loading config: " << config_path << "\n";

    // 1. 注册所有模块类型 (CPUTLM/CacheTLM/MemoryTLM + 5 个 SimModule 派生类)
    REGISTER_ALL;
    REGISTER_MODULE(CpuCluster);
    ModuleFactory::listRegisteredTypes();

    // 2. 加载 JSON 配置 (含 $include 处理)
    json config;
    try {
        config = JsonIncluder::loadAndInclude(config_path);
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Failed to load config: " << e.what() << "\n";
        return 1;
    }

    // 3. 构建系统: EventQueue + ModuleFactory
    EventQueue eq;
    ModuleFactory factory(&eq);

    // 4. 实例化 (触发 Step 4.5 顶层 CpuCluster 递归激活)
    std::cout << "[example_simmodule_nested] Instantiating modules...\n";
    if (!factory.instantiateAll(config)) {
        std::cerr << "[ERROR] Configuration validation failed\n";
        return 1;
    }

    // 5. 打印拓扑结构 (递归遍历嵌套 CpuCluster)
    std::cout << "\n=== Topology ===\n";
    int cluster_count = 0;
    int max_depth = 0;
    for (const auto& kv : factory.getAllInstances()) {
        auto* cluster = dynamic_cast<CpuCluster*>(kv.second.get());
        if (cluster) {
            ++cluster_count;
            printTopology(cluster, 1);
        }
    }
    std::cout << "Total top-level CpuCluster instances: " << cluster_count << "\n";

    for (const auto& kv : factory.getAllInstances()) {
        auto* cluster = dynamic_cast<CpuCluster*>(kv.second.get());
        if (cluster) {
            std::cout << "Cluster '" << cluster->getName() << "' has "
                      << cluster->getInternalFactory().getAllInstances().size()
                      << " internal modules\n";
        }
    }

    // 6. 启动所有顶层模块的 tick 循环
    std::cout << "\n[example_simmodule_nested] Starting ticks...\n";
    factory.startAllTicks();

    // 7. 运行 10000 cycles 仿真
    constexpr uint64_t kSimCycles = 10000;
    std::cout << "[example_simmodule_nested] Running " << kSimCycles << " cycles...\n";
    eq.run(kSimCycles);

    std::cout << "\n[example_simmodule_nested] " << kSimCycles << " cycles completed, "
              << factory.getAllInstances().size() << " instances active\n";

    // 8. 验证深度护栏 (SimModule::MAX_DEPTH)
    std::cout << "[example_simmodule_nested] SimModule::MAX_DEPTH = " << SimModule::getMaxDepth()
              << " (compile-time constant)\n";
    std::cout << "[example_simmodule_nested] SimModule::getCurrentDepth() = "
              << SimModule::getCurrentDepth() << " (post-simulation)\n";

    return 0;
}
