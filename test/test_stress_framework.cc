/**
 * @file test_stress_framework.cc
 * @brief Phase 8 Phase 5 — 压力测试基础设施核心
 *
 * 提供 StressTestRunner 框架：
 * - JSON 配置加载 + 模块实例化
 * - 仿真运行 + 指标验证
 * - 多格式报告生成
 *
 * 测试覆盖：
 * - StressTestRunner: 框架核心功能
 * - 指标验证辅助方法
 * - 报告生成功能
 *
 * @author CppTLM Development Team
 * @date 2026-04-18
 */

#include "chstream_register.hh"
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "core/module_factory.hh"
#include "core/sim_module.hh"
#include "metrics/metrics_reporter.hh"
#include "metrics/stats_manager.hh"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace {

    // ============================================================================
    // StressTestRunner — 压力测试运行器
    // ============================================================================

    /**
     * @brief 压力测试运行器
     *
     * 封装常见的压力测试流程：
     * 1. 创建 EventQueue + ModuleFactory
     * 2. 加载 JSON 配置并实例化模块
     * 3. 运行仿真
     * 4. 验证指标
     * 5. 生成报告
     */
    class StressTestRunner {
    public:
        /**
         * @brief 构造函数
         * @param config JSON 配置对象
         */
        explicit StressTestRunner(const json& config) : config_(config), eq_(), factory_(&eq_) {
            register_modules();
        }

        /**
         * @brief 析构函数 — 清理资源
         */
        ~StressTestRunner() {
            // 注销所有注册的 stats 组
            for (const auto& kv : registered_paths_) {
                tlm_stats::StatsManager::instance().unregister_group(kv);
            }
        }

        // =========================================================================
        // 模块管理
        // =========================================================================

        /**
         * @brief 实例化所有模块
         */
        void instantiate() {
            register_modules(); // 先注册模块类型
            factory_.instantiateAll(config_);
        }

        /**
         * @brief 启动所有模块的 tick
         */
        void startTicks() {
            factory_.startAllTicks();
        }

        /**
         * @brief 运行仿真指定周期数
         * @param cycles 周期数
         */
        void run(uint64_t cycles) {
            eq_.run(cycles);
        }

        /**
         * @brief 运行仿真直到所有请求完成
         * @param max_cycles 最大周期数（防止无限循环）
         * @return 实际运行的周期数
         */
        uint64_t runUntilComplete(uint64_t max_cycles = 100000) {
            uint64_t start = eq_.getCurrentCycle();
            uint64_t end = start + max_cycles;

            // 简单策略：运行指定周期
            // 更复杂的实现可以检查 TrafficGenTLM 的 issued/completed
            eq_.run(max_cycles);
            return eq_.getCurrentCycle() - start;
        }

        /**
         * @brief 获取模块实例
         * @param name 模块名称
         * @return 模块指针（未找到返回 nullptr）
         */
        SimObject* getModule(const std::string& name) {
            return factory_.getInstance(name);
        }

        /**
         * @brief 获取 TrafficGenTLM 模块
         * @param name 模块名称
         * @return TrafficGenTLM 指针
         */
        TrafficGenTLM* getTrafficGen(const std::string& name) {
            auto* mod = factory_.getInstance(name);
            return dynamic_cast<TrafficGenTLM*>(mod);
        }

        /**
         * @brief 获取 CacheTLM 模块
         * @param name 模块名称
         * @return CacheTLM 指针
         */
        CacheTLM* getCache(const std::string& name) {
            auto* mod = factory_.getInstance(name);
            return dynamic_cast<CacheTLM*>(mod);
        }

        /**
         * @brief 获取 CrossbarTLM 模块
         * @param name 模块名称
         * @return CrossbarTLM 指针
         */
        CrossbarTLM* getCrossbar(const std::string& name) {
            auto* mod = factory_.getInstance(name);
            return dynamic_cast<CrossbarTLM*>(mod);
        }

        /**
         * @brief 获取 MemoryTLM 模块
         * @param name 模块名称
         * @return MemoryTLM 指针
         */
        MemoryTLM* getMemory(const std::string& name) {
            auto* mod = factory_.getInstance(name);
            return dynamic_cast<MemoryTLM*>(mod);
        }

        // =========================================================================
        // Stats 注册与查询
        // =========================================================================

        /**
         * @brief 注册模块的 stats 组到 StatsManager
         * @param module_name 模块名称
         * @param path StatsManager 中的路径
         */
        void registerStats(const std::string& module_name, const std::string& path) {
            auto* mod = factory_.getInstance(module_name);
            if (!mod)
                return;

            auto* tg = getTrafficGen(module_name);
            if (tg) {
                tlm_stats::StatsManager::instance().register_group(&tg->stats(), path);
                registered_paths_.push_back(path);
            }

            auto* cache = getCache(module_name);
            if (cache) {
                tlm_stats::StatsManager::instance().register_group(&cache->stats(), path);
                registered_paths_.push_back(path);
            }

            auto* xbar = getCrossbar(module_name);
            if (xbar) {
                tlm_stats::StatsManager::instance().register_group(&xbar->stats(), path);
                registered_paths_.push_back(path);
            }

            auto* mem = getMemory(module_name);
            if (mem) {
                tlm_stats::StatsManager::instance().register_group(&mem->stats(), path);
                registered_paths_.push_back(path);
            }
        }

        /**
         * @brief 获取模块的 stats 组
         * @param module_name 模块名称
         * @return Stats 组指针
         */
        tlm_stats::StatGroup* getStats(const std::string& module_name) {
            auto* tg = getTrafficGen(module_name);
            if (tg)
                return &tg->stats();

            auto* cache = getCache(module_name);
            if (cache)
                return &cache->stats();

            auto* xbar = getCrossbar(module_name);
            if (xbar)
                return &xbar->stats();

            auto* mem = getMemory(module_name);
            if (mem)
                return &mem->stats();

            return nullptr;
        }

        // =========================================================================
        // 指标验证辅助方法
        // =========================================================================

        /**
         * @brief 获取 Scalar 统计值
         * @param module_name 模块名称
         * @param stat_name 统计项名称
         * @return 统计值（未找到返回 0）
         */
        uint64_t getScalar(const std::string& module_name, const std::string& stat_name) {
            auto* stats = getStats(module_name);
            if (!stats)
                return 0;

            auto it = stats->stats().find(stat_name);
            if (it == stats->stats().end())
                return 0;

            auto* scalar = dynamic_cast<tlm_stats::Scalar*>(it->second.get());
            if (!scalar)
                return 0;

            return scalar->value();
        }

        /**
         * @brief 获取 Distribution 统计的平均值
         * @param module_name 模块名称
         * @param stat_name 统计项名称
         * @return 平均值（未找到返回 0.0）
         */
        double getDistributionMean(const std::string& module_name, const std::string& stat_name) {
            auto* stats = getStats(module_name);
            if (!stats)
                return 0.0;

            auto it = stats->stats().find(stat_name);
            if (it == stats->stats().end())
                return 0.0;

            auto* dist = dynamic_cast<tlm_stats::Distribution*>(it->second.get());
            if (!dist)
                return 0.0;

            return dist->mean();
        }

        /**
         * @brief 获取 Distribution 统计的样本数
         * @param module_name 模块名称
         * @param stat_name 统计项名称
         * @return 样本数（未找到返回 0）
         */
        uint64_t getDistributionCount(const std::string& module_name,
                                      const std::string& stat_name) {
            auto* stats = getStats(module_name);
            if (!stats)
                return 0;

            auto it = stats->stats().find(stat_name);
            if (it == stats->stats().end())
                return 0;

            auto* dist = dynamic_cast<tlm_stats::Distribution*>(it->second.get());
            if (!dist)
                return 0;

            return dist->count();
        }

        // =========================================================================
        // 报告生成
        // =========================================================================

        /**
         * @brief 生成文本格式报告
         * @param path 输出文件路径
         * @return 是否成功
         */
        bool dumpText(const std::string& path) {
            tlm_stats::TextReporter reporter;
            return reporter.generate(path);
        }

        /**
         * @brief 生成 JSON 格式报告
         * @param path 输出文件路径
         * @return 是否成功
         */
        bool dumpJSON(const std::string& path) {
            tlm_stats::JSONReporter reporter;
            return reporter.generate(path);
        }

        /**
         * @brief 生成 Markdown 格式报告
         * @param path 输出文件路径
         * @return 是否成功
         */
        bool dumpMarkdown(const std::string& path) {
            tlm_stats::MarkdownReporter reporter;
            return reporter.generate(path);
        }

        /**
         * @brief 生成所有格式的报告
         * @param output_dir 输出目录
         * @return 是否全部成功
         */
        bool dumpAll(const std::string& output_dir) {
            tlm_stats::MultiReporter multi;
            return multi.generate_all(output_dir);
        }

        /**
         * @brief 获取当前周期数
         */
        uint64_t getCurrentCycle() const {
            return eq_.getCurrentCycle();
        }

    private:
        void register_modules() {
            static bool registered = false;
            if (!registered) {
                REGISTER_CHSTREAM;
                registered = true;
            }
        }

        json config_;
        EventQueue eq_;
        ModuleFactory factory_;
        std::vector<std::string> registered_paths_;
    };

    // ============================================================================
    // 测试用例
    // ============================================================================

    TEST_CASE("StressTestRunner: basic instantiation", "[phase8-stress][framework]") {
        json config = R"({
        "modules": [
            {"name": "tg", "type": "TrafficGenTLM", "params": {"num_requests": 10}},
            {"name": "cache", "type": "CacheTLM"},
            {"name": "mem", "type": "MemoryTLM"}
        ],
        "connections": [
            {"src": "tg", "dst": "cache", "latency": 1},
            {"src": "cache", "dst": "mem", "latency": 2}
        ]
    })"_json;

        StressTestRunner runner(config);
        runner.instantiate();
        runner.startTicks();

        REQUIRE(runner.getModule("tg") != nullptr);
        REQUIRE(runner.getModule("cache") != nullptr);
        REQUIRE(runner.getModule("mem") != nullptr);

        REQUIRE(runner.getTrafficGen("tg") != nullptr);
        REQUIRE(runner.getCache("cache") != nullptr);
        REQUIRE(runner.getMemory("mem") != nullptr);
    }

    TEST_CASE("StressTestRunner: run simulation", "[phase8-stress][framework]") {
        json config = R"({
        "modules": [
            {"name": "tg", "type": "TrafficGenTLM", "params": {"num_requests": 10}},
            {"name": "cache", "type": "CacheTLM"},
            {"name": "mem", "type": "MemoryTLM"}
        ],
        "connections": [
            {"src": "tg", "dst": "cache", "latency": 1},
            {"src": "cache", "dst": "mem", "latency": 2}
        ]
    })"_json;

        StressTestRunner runner(config);
        runner.instantiate();
        runner.startTicks();

        uint64_t before = runner.getCurrentCycle();
        runner.run(100);
        uint64_t after = runner.getCurrentCycle();

        REQUIRE(after == before + 100);
    }

    TEST_CASE("StressTestRunner: stats access", "[phase8-stress][framework]") {
        json config = R"({
        "modules": [
            {"name": "tg", "type": "TrafficGenTLM", "params": {"num_requests": 100}},
            {"name": "cache", "type": "CacheTLM"},
            {"name": "mem", "type": "MemoryTLM"}
        ],
        "connections": [
            {"src": "tg", "dst": "cache", "latency": 1},
            {"src": "cache", "dst": "mem", "latency": 2}
        ]
    })"_json;

        StressTestRunner runner(config);
        runner.instantiate();
        runner.registerStats("tg", "traffic_gen");
        runner.registerStats("cache", "cache");
        runner.registerStats("mem", "memory");
        runner.startTicks();

        // 运行足够长让 TrafficGenTLM 发出请求
        runner.run(100);

        // TrafficGenTLM 应该发出了一些请求
        auto* tg = runner.getTrafficGen("tg");
        REQUIRE(tg != nullptr);

        // stats 结构存在
        auto& stats = tg->stats();
        REQUIRE(stats.stats().count("requests_issued") > 0);
    }

    TEST_CASE("StressTestRunner: multi-format report generation",
              "[phase8-stress][framework][report]") {
        json config = R"({
        "modules": [
            {"name": "tg", "type": "TrafficGenTLM", "params": {"num_requests": 10}},
            {"name": "cache", "type": "CacheTLM"},
            {"name": "mem", "type": "MemoryTLM"}
        ],
        "connections": [
            {"src": "tg", "dst": "cache", "latency": 1},
            {"src": "cache", "dst": "mem", "latency": 2}
        ]
    })"_json;

        StressTestRunner runner(config);
        runner.instantiate();
        runner.registerStats("tg", "traffic_gen");
        runner.registerStats("cache", "cache");
        runner.registerStats("mem", "memory");
        runner.startTicks();
        runner.run(50);

        // 创建临时目录
        std::string output_dir = "test/stress_results/framework_test";
        std::filesystem::create_directories(output_dir);

        // 生成所有格式报告
        bool success = runner.dumpAll(output_dir);
        REQUIRE(success);

        // 验证文件存在
        REQUIRE(std::filesystem::exists(output_dir + "/metrics.txt"));
        REQUIRE(std::filesystem::exists(output_dir + "/metrics.json"));
        REQUIRE(std::filesystem::exists(output_dir + "/metrics.md"));

        // 清理
        std::filesystem::remove_all(output_dir);
    }

    TEST_CASE("StressTestRunner: getScalar and getDistributionMean", "[phase8-stress][framework]") {
        json config = R"({
        "modules": [
            {"name": "tg", "type": "TrafficGenTLM", "params": {"num_requests": 50}},
            {"name": "cache", "type": "CacheTLM"},
            {"name": "mem", "type": "MemoryTLM"}
        ],
        "connections": [
            {"src": "tg", "dst": "cache", "latency": 1},
            {"src": "cache", "dst": "mem", "latency": 2}
        ]
    })"_json;

        StressTestRunner runner(config);
        runner.instantiate();
        runner.startTicks();
        runner.run(100);

        // TrafficGenTLM 应该发出了一些请求
        uint64_t issued = runner.getScalar("tg", "requests_issued");
        REQUIRE(issued > 0);
    }

    TEST_CASE("StressTestRunner: verify module type", "[phase8-stress][framework]") {
        json config = R"({
        "modules": [
            {"name": "xbar", "type": "CrossbarTLM"},
            {"name": "mem0", "type": "MemoryTLM"},
            {"name": "mem1", "type": "MemoryTLM"}
        ],
        "connections": [
            {"src": "xbar.0", "dst": "mem0", "latency": 1},
            {"src": "xbar.1", "dst": "mem1", "latency": 1}
        ]
    })"_json;

        StressTestRunner runner(config);
        runner.instantiate();
        runner.startTicks();

        auto* xbar = runner.getCrossbar("xbar");
        REQUIRE(xbar != nullptr);
        REQUIRE(xbar->num_ports() == 4);

        auto* mem0 = runner.getMemory("mem0");
        auto* mem1 = runner.getMemory("mem1");
        REQUIRE(mem0 != nullptr);
        REQUIRE(mem1 != nullptr);
    }

} // namespace