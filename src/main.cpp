/**
 * @file main.cpp
 * @brief CppTLM 主仿真入口
 *
 * @author CppTLM Development Team
 * @date 2026-04-22
 */

#include "chstream_register.hh"
#include "event_queue.hh"
#include "metrics/streaming_reporter.hh"
#include "module_factory.hh"
#include "modules.hh"
#include "sim_core.hh"
#include "tlm/crossbar_tlm.hh"
// HSK-8 Phase 2 Step 4: memory_bridge.hh + ptx_emu_driver.hh 已物理删除
// (HSK-6 deprecate by 369cf71). PTX-EMU 集成走 IComputeDevice 接口 (per SM 重构).
// Task 13: PipelineTLM/ScoreboardTLM/TensorCoreTLM + cudart 物理删除 (功能内化到 SM 子模块).
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include "utils/json_includer.hh"
#include "utils/topology_dumper.hh"

using namespace tlm;

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " <config.json> [options]\n"
              << "Options:\n"
              << "  --stream-stats           Enable streaming statistics\n"
              << "  --stream-interval <N>    Report interval in cycles (default: 10000)\n"
              << "  --stream-path <path>     Output path for stats stream (default: "
                 "output/stats_stream.jsonl)\n"
              << "  --cycles <N>             Number of simulation cycles (default: 10000)\n"
              << "  --f12b-ld                Enable F12b-LD MemoryBridge (PTX-EMU bridge)\n"
              << "  --debug-config           Enable verbose config parsing output\n"
              << "  --help, -h               Show this help message\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    // 解析命令行参数
    std::string config_path;
    bool stream_stats = false;
    bool debug_config = false;
    bool f12b_ld = false;
    uint64_t stream_interval = 10000;
    std::string stream_path = "output/stats_stream.jsonl";
    uint64_t sim_cycles = 10000;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--debug-config") == 0) {
            debug_config = true;
        } else if (strcmp(argv[i], "--stream-stats") == 0) {
            stream_stats = true;
        } else if (strcmp(argv[i], "--stream-interval") == 0 && i + 1 < argc) {
            stream_interval = std::strtoull(argv[++i], nullptr, 10);
        } else if (strcmp(argv[i], "--stream-path") == 0 && i + 1 < argc) {
            stream_path = argv[++i];
        } else if (strcmp(argv[i], "--cycles") == 0 && i + 1 < argc) {
            sim_cycles = std::strtoull(argv[++i], nullptr, 10);
        } else if (strcmp(argv[i], "--f12b-ld") == 0) {
            f12b_ld = true;
        } else if (config_path.empty()) {
            config_path = argv[i];
        }
    }

    if (config_path.empty()) {
        std::cerr << "Error: config.json path required\n";
        print_usage(argv[0]);
        return 1;
    }

    // 创建输出目录
    std::filesystem::create_directories(std::filesystem::path(stream_path).parent_path());

    EventQueue eq;

    REGISTER_ALL
    REGISTER_MODULE(CpuCluster);
    ModuleFactory::listRegisteredTypes();

    // 加载配置文件
    json config = JsonIncluder::loadAndInclude(config_path);

    // 构建系统
    ModuleFactory factory(&eq);
    if (debug_config) {
        ModuleFactory::set_debug_config(true);
        std::cout << "[DEBUG] Config debug mode enabled\n";
    }
    if (!factory.instantiateAll(config)) {
        std::cerr << "[ERROR] Configuration validation failed\n";
        return 1;
    }
    factory.startAllTicks();

    // HSK-8 Phase 2 Step 4: --f12b-ld wiring block disabled (MemoryBridge +
    // g_ptx_emu_driver 物理删除, commit 369cf71).
    // 当前契约: --f12b-ld 启用直接报错,禁止路径使用;默认 disabled (zero regression).
    // (替代路径走 PtxEmuSubmoduleMVP facade + IPtxEmuDevice::attach_timing,
    // 见 docs/soc_arch/architecture/11-cdna-real-isa-integration.md 阶段 B.)
    if (f12b_ld) {
        std::cerr << "[ERROR] --f12b-ld disabled (HSK-8 Phase 2 Step 4 removed "
                     "MemoryBridge + g_ptx_emu_driver; PTX-EMU 集成走 "
                     "IComputeDevice::set_instr_descriptor_buf per HSK-9)\n";
        return 1;
    } else {
        std::cout << "[INFO] --f12b-ld: MemoryBridge disabled (zero regression)\n";
    }

    TopologyDumper::dumpToDot(factory, config, "topology.dot");

    // 流式统计报告器
    std::unique_ptr<tlm_stats::StreamingReporter> reporter;

    if (stream_stats) {
        reporter = std::make_unique<tlm_stats::StreamingReporter>(stream_path);
        reporter->set_interval(stream_interval);
        reporter->start();
        std::cout << "[INFO] Streaming stats enabled (interval: " << stream_interval
                  << " cycles, output: " << stream_path << ")\n";
    }

    // 运行仿真
    std::cout << "[INFO] Running simulation for " << sim_cycles << " cycles...\n";

    uint64_t current_cycle = 0;

    if (stream_stats && reporter) {
        // 分批运行，每批 interval 周期，批间输出统计
        while (current_cycle < sim_cycles) {
            uint64_t remaining = sim_cycles - current_cycle;
            uint64_t batch = (stream_interval < remaining) ? stream_interval : remaining;

            eq.run(batch);
            current_cycle += batch;

            // 输出本批统计
            reporter->set_current_cycle(current_cycle);
            reporter->enqueue_all();
        }
    } else {
        // 无流式统计，直接运行
        eq.run(sim_cycles);
        current_cycle = sim_cycles;
    }

    // 停止流式统计
    if (reporter) {
        reporter->stop();
        std::cout << "[INFO] Stats stream written to " << stream_path << "\n";
    }

    std::cout << "\n[INFO] Simulation finished.\n";
    return 0;
}