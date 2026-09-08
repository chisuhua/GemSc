/**
 * @file test_streaming_reporter.cc
 * @brief StreamingReporter 单元测试
 *
 * 测试覆盖：
 * - StreamingReporter: 构造/析构、start/stop 生命周期
 * - enqueue_snapshot: JSON Lines 格式验证
 * - set_interval/set_current_cycle: 配置接口
 * - 多统计类型序列化: Scalar/Average/Distribution/PercentileHistogram
 * - groups() 遍历: StatsManager 集成
 * - 后台线程: 异步写入、强制刷新
 *
 * 验收标准：
 * - 使用 Catch2 标签 [streaming-reporter]
 * - 不影响现有 367 测试用例
 * - 线程安全验证
 *
 * @author CppTLM Development Team
 * @date 2026-04-22
 */

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>
#include "catch_amalgamated.hpp"

#include "metrics/stats.hh"
#include "metrics/stats_manager.hh"
#include "metrics/streaming_reporter.hh"

namespace {

    // ============================================================================
    // Helper: 读取文件内容
    // ============================================================================

    std::string read_file(const std::string& path) {
        std::ifstream ifs(path);
        if (!ifs.good())
            return "";
        std::string content((std::istreambuf_iterator<char>(ifs)),
                            std::istreambuf_iterator<char>());
        return content;
    }

    // ============================================================================
    // Helper: 统计 JSON Lines 行数
    // ============================================================================

    size_t count_jsonl_lines(const std::string& content) {
        if (content.empty())
            return 0;
        size_t count = 0;
        for (char c : content) {
            if (c == '\n')
                count++;
        }
        return count;
    }

    // ============================================================================
    // StreamingReporter Basic Tests
    // ============================================================================

    TEST_CASE("StreamingReporter.constructor", "[streaming-reporter]") {
        std::string path = "/tmp/test_stream_constructor.jsonl";

        // 清理可能存在的旧文件
        std::remove(path.c_str());

        // 构造时不应创建文件（仅 start() 时创建）
        {
            tlm_stats::StreamingReporter reporter(path);
            // 文件不应存在
            REQUIRE(!std::filesystem::exists(path));
        }

        // start/stop 应能正常创建和删除文件
        {
            tlm_stats::StreamingReporter reporter(path);
            reporter.start();
            REQUIRE(std::filesystem::exists(path));
            reporter.stop();
        }

        // 清理
        std::remove(path.c_str());
    }

    TEST_CASE("StreamingReporter.start_stop", "[streaming-reporter]") {
        std::string path = "/tmp/test_stream_start_stop.jsonl";
        std::remove(path.c_str());

        {
            tlm_stats::StreamingReporter reporter(path);

            // 未 start 前不应 running
            REQUIRE(!reporter.is_running());

            reporter.start();
            REQUIRE(reporter.is_running());

            reporter.stop();
            REQUIRE(!reporter.is_running());
        }

        // 重复 stop 不应崩溃
        {
            tlm_stats::StreamingReporter reporter(path);
            reporter.start();
            reporter.stop();
            reporter.stop(); // NOLINT
        }

        std::remove(path.c_str());
    }

    TEST_CASE("StreamingReporter.interval_cycle_accessors", "[streaming-reporter]") {
        std::string path = "/tmp/test_stream_accessors.jsonl";
        std::remove(path.c_str());

        tlm_stats::StreamingReporter reporter(path);

        // 默认 interval 应为 10000
        REQUIRE(reporter.get_interval() == 10000);

        // 设置新值
        reporter.set_interval(5000);
        REQUIRE(reporter.get_interval() == 5000);

        // set_current_cycle 不应崩溃
        reporter.set_current_cycle(12345);
        REQUIRE(reporter.get_interval() == 5000); // interval 不变

        std::remove(path.c_str());
    }

    TEST_CASE("StreamingReporter.double_start", "[streaming-reporter]") {
        std::string path = "/tmp/test_stream_double_start.jsonl";
        std::remove(path.c_str());

        tlm_stats::StreamingReporter reporter(path);
        reporter.start();
        REQUIRE(reporter.is_running());

        // 重复 start 不应崩溃（内部有保护）
        reporter.start();
        REQUIRE(reporter.is_running());

        reporter.stop();
        REQUIRE(!reporter.is_running());

        std::remove(path.c_str());
    }

    // ============================================================================
    // enqueue_snapshot Tests
    // ============================================================================

    TEST_CASE("StreamingReporter.enqueue_snapshot_scalar", "[streaming-reporter]") {
        std::string path = "/tmp/test_stream_scalar.jsonl";
        std::remove(path.c_str());

        tlm_stats::StatsManager::instance().reset_all();

        // 创建测试组
        tlm_stats::StatGroup group("test.scalar");
        auto& scalar = group.addScalar("counter", "Test counter", "count");
        scalar += 42;

        tlm_stats::StatsManager::instance().register_group(&group, "test.scalar");

        // 启动 reporter 并入队
        {
            tlm_stats::StreamingReporter reporter(path);
            reporter.set_current_cycle(1000);
            reporter.start();

            // 入队单个组
            reporter.enqueue_snapshot("test.scalar", &group);

            // 等待后台线程处理
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            reporter.stop();
        }

        // 验证文件内容
        std::string content = read_file(path);
        REQUIRE(!content.empty());
        REQUIRE(content.find("\"simulation_cycle\":1000") != std::string::npos);
        REQUIRE(content.find("\"group\":\"test.scalar\"") != std::string::npos);
        REQUIRE(content.find("\"counter\":42") != std::string::npos);

        // 清理
        tlm_stats::StatsManager::instance().unregister_group("test.scalar");
        std::remove(path.c_str());
    }

    TEST_CASE("StreamingReporter.enqueue_snapshot_average", "[streaming-reporter]") {
        std::string path = "/tmp/test_stream_average.jsonl";
        std::remove(path.c_str());

        tlm_stats::StatsManager::instance().reset_all();

        tlm_stats::StatGroup group("test.avg");
        auto& avg = group.addAverage("load", "System load", "value");
        avg.sample(10.5, 100);
        avg.sample(20.0, 100);

        tlm_stats::StatsManager::instance().register_group(&group, "test.avg");

        {
            tlm_stats::StreamingReporter reporter(path);
            reporter.set_current_cycle(500);
            reporter.start();
            reporter.enqueue_snapshot("test.avg", &group);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            reporter.stop();
        }

        std::string content = read_file(path);
        REQUIRE(!content.empty());
        REQUIRE(content.find("\"simulation_cycle\":500") != std::string::npos);
        REQUIRE(content.find("\"load\"") != std::string::npos);

        tlm_stats::StatsManager::instance().unregister_group("test.avg");
        std::remove(path.c_str());
    }

    TEST_CASE("StreamingReporter.enqueue_snapshot_distribution", "[streaming-reporter]") {
        std::string path = "/tmp/test_stream_dist.jsonl";
        std::remove(path.c_str());

        tlm_stats::StatsManager::instance().reset_all();

        tlm_stats::StatGroup group("test.dist");
        auto& dist = group.addDistribution("lat", "Latency", "cycle");
        dist.sample(5);
        dist.sample(10);
        dist.sample(15);

        tlm_stats::StatsManager::instance().register_group(&group, "test.dist");

        {
            tlm_stats::StreamingReporter reporter(path);
            reporter.set_current_cycle(2000);
            reporter.start();
            reporter.enqueue_snapshot("test.dist", &group);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            reporter.stop();
        }

        std::string content = read_file(path);
        REQUIRE(!content.empty());
        REQUIRE(content.find("\"simulation_cycle\":2000") != std::string::npos);
        REQUIRE(content.find("\"count\":3") != std::string::npos);
        REQUIRE(content.find("\"min\":5") != std::string::npos);
        REQUIRE(content.find("\"max\":15") != std::string::npos);

        tlm_stats::StatsManager::instance().unregister_group("test.dist");
        std::remove(path.c_str());
    }

    TEST_CASE("StreamingReporter.enqueue_snapshot_percentile", "[streaming-reporter]") {
        std::string path = "/tmp/test_stream_pct.jsonl";
        std::remove(path.c_str());

        tlm_stats::StatsManager::instance().reset_all();

        tlm_stats::StatGroup group("test.pct");
        auto& pct = group.addPercentileHistogram("lat", "Latency", "cycle");
        for (int i = 1; i <= 100; ++i) {
            pct.record(i);
        }

        tlm_stats::StatsManager::instance().register_group(&group, "test.pct");

        {
            tlm_stats::StreamingReporter reporter(path);
            reporter.set_current_cycle(3000);
            reporter.start();
            reporter.enqueue_snapshot("test.pct", &group);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            reporter.stop();
        }

        std::string content = read_file(path);
        REQUIRE(!content.empty());
        REQUIRE(content.find("\"simulation_cycle\":3000") != std::string::npos);
        REQUIRE(content.find("\"p50\"") != std::string::npos);
        REQUIRE(content.find("\"p95\"") != std::string::npos);
        REQUIRE(content.find("\"p99\"") != std::string::npos);
        REQUIRE(content.find("\"p99.9\"") != std::string::npos);

        tlm_stats::StatsManager::instance().unregister_group("test.pct");
        std::remove(path.c_str());
    }

    TEST_CASE("StreamingReporter.enqueue_null_group", "[streaming-reporter]") {
        std::string path = "/tmp/test_stream_null.jsonl";
        std::remove(path.c_str());

        {
            tlm_stats::StreamingReporter reporter(path);
            reporter.start();

            // 传入 nullptr 不应崩溃
            reporter.enqueue_snapshot("null.group", nullptr);

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            reporter.stop();
        }

        // 文件应存在但内容可能为空或只有之前的行
        std::remove(path.c_str());
    }

    // ============================================================================
    // enqueue_all Tests
    // ============================================================================

    TEST_CASE("StreamingReporter.enqueue_all", "[streaming-reporter]") {
        std::string path = "/tmp/test_stream_all.jsonl";
        std::remove(path.c_str());

        tlm_stats::StatsManager::instance().reset_all();

        // 创建多个组
        tlm_stats::StatGroup group1("g1");
        group1.addScalar("v1", "Value 1", "count");
        tlm_stats::StatsManager::instance().register_group(&group1, "test.g1");

        tlm_stats::StatGroup group2("g2");
        group2.addScalar("v2", "Value 2", "count");
        tlm_stats::StatsManager::instance().register_group(&group2, "test.g2");

        {
            tlm_stats::StreamingReporter reporter(path);
            reporter.set_current_cycle(999);
            reporter.start();

            // 入队所有组
            reporter.enqueue_all();

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            reporter.stop();
        }

        std::string content = read_file(path);
        size_t lines = count_jsonl_lines(content);
        // 应至少有 2 行（每个注册的组一行）
        REQUIRE(lines >= 2);
        REQUIRE(content.find("test.g1") != std::string::npos);
        REQUIRE(content.find("test.g2") != std::string::npos);
        REQUIRE(content.find("simulation_cycle") != std::string::npos);

        tlm_stats::StatsManager::instance().unregister_group("test.g1");
        tlm_stats::StatsManager::instance().unregister_group("test.g2");
        std::remove(path.c_str());
    }

    // ============================================================================
    // JSON Lines Format Tests
    // ============================================================================

    TEST_CASE("StreamingReporter.jsonl_format", "[streaming-reporter]") {
        std::string path = "/tmp/test_stream_format.jsonl";
        std::remove(path.c_str());

        tlm_stats::StatsManager::instance().reset_all();

        tlm_stats::StatGroup group("fmt");
        group.addScalar("x", "X", "count");
        tlm_stats::StatsManager::instance().register_group(&group, "fmt");

        {
            tlm_stats::StreamingReporter reporter(path);
            reporter.set_current_cycle(100);
            reporter.start();
            reporter.enqueue_snapshot("fmt", &group);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            reporter.stop();
        }

        std::string content = read_file(path);
        // 每行应是独立的 JSON 对象（以 { 开头，以 } 结尾）
        std::istringstream iss(content);
        std::string line;
        bool found_object = false;
        while (std::getline(iss, line)) {
            if (line.empty())
                continue;
            if (line.front() == '{' && line.back() == '}') {
                found_object = true;
                // 验证 timestamp_ns 字段存在
                REQUIRE(line.find("\"timestamp_ns\"") != std::string::npos);
            }
        }
        REQUIRE(found_object);

        tlm_stats::StatsManager::instance().unregister_group("fmt");
        std::remove(path.c_str());
    }

    TEST_CASE("StreamingReporter.multiple_snapshots", "[streaming-reporter]") {
        std::string path = "/tmp/test_stream_multi.jsonl";
        std::remove(path.c_str());

        tlm_stats::StatsManager::instance().reset_all();

        tlm_stats::StatGroup group("multi");
        group.addScalar("c", "Counter", "count");
        tlm_stats::StatsManager::instance().register_group(&group, "multi");

        {
            tlm_stats::StreamingReporter reporter(path);
            reporter.start();

            // 模拟多个时间点的快照
            for (uint64_t cycle = 0; cycle < 5; ++cycle) {
                reporter.set_current_cycle(cycle * 1000);
                reporter.enqueue_snapshot("multi", &group);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }

            reporter.stop();
        }

        std::string content = read_file(path);
        size_t lines = count_jsonl_lines(content);
        REQUIRE(lines >= 5);

        // 验证不同 cycle 的快照
        REQUIRE(content.find("simulation_cycle\":0") != std::string::npos);
        REQUIRE(content.find("simulation_cycle\":4000") != std::string::npos);

        tlm_stats::StatsManager::instance().unregister_group("multi");
        std::remove(path.c_str());
    }

    // ============================================================================
    // Background Thread Tests
    // ============================================================================

    TEST_CASE("StreamingReporter.background_thread_join", "[streaming-reporter]") {
        std::string path = "/tmp/test_stream_thread.jsonl";
        std::remove(path.c_str());

        tlm_stats::StreamingReporter reporter(path);
        reporter.start();
        REQUIRE(reporter.is_running());

        // 确保后台线程可 join
        reporter.stop();
        REQUIRE(!reporter.is_running());

        std::remove(path.c_str());
    }

    TEST_CASE("StreamingReporter.blocking_enqueue", "[streaming-reporter]") {
        std::string path = "/tmp/test_stream_block.jsonl";
        std::remove(path.c_str());

        tlm_stats::StatsManager::instance().reset_all();

        tlm_stats::StatGroup group("block");
        group.addScalar("x", "X", "count");
        tlm_stats::StatsManager::instance().register_group(&group, "block");

        // 验证 enqueue 不是阻塞的（后台线程负责写入）
        {
            tlm_stats::StreamingReporter reporter(path);
            reporter.start();

            auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < 100; ++i) {
                reporter.enqueue_snapshot("block", &group);
            }
            auto end = std::chrono::steady_clock::now();

            // 入队 100 个快照不应花费太长时间（后台处理）
            auto duration =
                std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            REQUIRE(duration < 500); // 应远小于 500ms

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            reporter.stop();
        }

        tlm_stats::StatsManager::instance().unregister_group("block");
        std::remove(path.c_str());
    }

    // ============================================================================
    // Directory Creation Tests
    // ============================================================================

    TEST_CASE("StreamingReporter.creates_parent_directory", "[streaming-reporter]") {
        std::string path = "/tmp/cpptlm_test_subdir/nested/stats.jsonl";
        std::remove(path.c_str());
        std::filesystem::remove_all("/tmp/cpptlm_test_subdir");

        {
            tlm_stats::StreamingReporter reporter(path);
            reporter.start();
            REQUIRE(std::filesystem::exists("/tmp/cpptlm_test_subdir/nested"));
            reporter.stop();
        }

        std::filesystem::remove_all("/tmp/cpptlm_test_subdir");
    }

    // ============================================================================
    // Empty Groups Tests
    // ============================================================================

    TEST_CASE("StreamingReporter.empty_group", "[streaming-reporter]") {
        std::string path = "/tmp/test_stream_empty.jsonl";
        std::remove(path.c_str());

        tlm_stats::StatsManager::instance().reset_all();

        // 无任何 stat 的组
        tlm_stats::StatGroup empty_group("empty");
        tlm_stats::StatsManager::instance().register_group(&empty_group, "empty");

        {
            tlm_stats::StreamingReporter reporter(path);
            reporter.set_current_cycle(100);
            reporter.start();
            reporter.enqueue_snapshot("empty", &empty_group);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            reporter.stop();
        }

        std::string content = read_file(path);
        REQUIRE(!content.empty());
        REQUIRE(content.find("\"group\":\"empty\"") != std::string::npos);

        tlm_stats::StatsManager::instance().unregister_group("empty");
        std::remove(path.c_str());
    }

    // ============================================================================
    // Subgroups Tests
    // ============================================================================

    TEST_CASE("StreamingReporter.subgroups", "[streaming-reporter]") {
        std::string path = "/tmp/test_stream_sub.jsonl";
        std::remove(path.c_str());

        tlm_stats::StatsManager::instance().reset_all();

        tlm_stats::StatGroup group("parent");
        auto& sub = group.addSubgroup("child");
        sub.addScalar("sub_val", "Sub value", "count");

        tlm_stats::StatsManager::instance().register_group(&group, "parent");

        {
            tlm_stats::StreamingReporter reporter(path);
            reporter.set_current_cycle(777);
            reporter.start();
            reporter.enqueue_snapshot("parent", &group);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            reporter.stop();
        }

        std::string content = read_file(path);
        REQUIRE(!content.empty());
        REQUIRE(content.find("\"parent\"") != std::string::npos);
        REQUIRE(content.find("\"sub_val\"") != std::string::npos);

        tlm_stats::StatsManager::instance().unregister_group("parent");
        std::remove(path.c_str());
    }

    // ============================================================================
    // Thread Safety Tests
    // ============================================================================

    TEST_CASE("StreamingReporter.thread_safe_enqueue", "[streaming-reporter][thread-safe]") {
        std::string path = "/tmp/test_stream_threadsafe.jsonl";
        std::remove(path.c_str());

        tlm_stats::StatsManager::instance().reset_all();

        tlm_stats::StatGroup group("ts");
        group.addScalar("c", "Counter", "count");
        tlm_stats::StatsManager::instance().register_group(&group, "ts");

        const int num_threads = 4;
        const int enqueues_per_thread = 50;

        {
            tlm_stats::StreamingReporter reporter(path);
            reporter.start();

            std::vector<std::thread> threads;

            for (int t = 0; t < num_threads; ++t) {
                threads.emplace_back([&reporter, &group, t]() {
                    for (int i = 0; i < enqueues_per_thread; ++i) {
                        reporter.set_current_cycle(t * 1000 + i);
                        reporter.enqueue_snapshot("ts", &group);
                    }
                });
            }

            for (auto& th : threads) {
                th.join();
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            reporter.stop();
        }

        std::string content = read_file(path);
        size_t lines = count_jsonl_lines(content);
        REQUIRE(lines >= num_threads * enqueues_per_thread);

        tlm_stats::StatsManager::instance().unregister_group("ts");
        std::remove(path.c_str());
    }

    // ============================================================================
    // Full Pipeline Integration
    // ============================================================================

    TEST_CASE("StreamingReporter.full_pipeline", "[streaming-reporter][integration]") {
        std::string path = "/tmp/test_stream_pipeline.jsonl";
        std::remove(path.c_str());

        tlm_stats::StatsManager::instance().reset_all();

        // 创建测试统计层次
        tlm_stats::StatGroup* cpu_group = new tlm_stats::StatGroup("cpu");
        auto& cpu_cycles = cpu_group->addScalar("cycles", "CPU cycles", "cycle");
        cpu_cycles += 1000;
        auto& cpu_instr = cpu_group->addScalar("instr", "Instructions", "count");
        cpu_instr += 500;
        auto& cpu_lat = cpu_group->addDistribution("latency", "CPU latency", "cycle");
        cpu_lat.sample(10);
        cpu_lat.sample(20);

        auto& cpu_sub = cpu_group->addSubgroup("sub");
        cpu_sub.addScalar("sub_c", "Sub counter", "count");

        tlm_stats::StatsManager::instance().register_group(cpu_group, "system.cpu");

        tlm_stats::StatGroup* mem_group = new tlm_stats::StatGroup("mem");
        mem_group->addScalar("bandwidth", "Memory bandwidth", "GB/s");
        tlm_stats::StatsManager::instance().register_group(mem_group, "system.memory");

        {
            tlm_stats::StreamingReporter reporter(path);
            reporter.set_interval(10000);
            reporter.start();

            // 仿真不同阶段
            for (uint64_t phase = 0; phase < 3; ++phase) {
                reporter.set_current_cycle(phase * 10000);
                cpu_cycles += 1000; // 更新统计
                reporter.enqueue_all();
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }

            reporter.stop();
        }

        // 验证输出
        std::string content = read_file(path);
        REQUIRE(!content.empty());
        REQUIRE(content.find("\"system.cpu\"") != std::string::npos);
        REQUIRE(content.find("\"system.memory\"") != std::string::npos);
        REQUIRE(content.find("\"cycles\":") != std::string::npos);
        REQUIRE(content.find("\"sub_c\"") != std::string::npos);

        // 清理
        tlm_stats::StatsManager::instance().unregister_group("system.cpu");
        tlm_stats::StatsManager::instance().unregister_group("system.memory");
        delete cpu_group;
        delete mem_group;
        std::remove(path.c_str());
    }

} // namespace