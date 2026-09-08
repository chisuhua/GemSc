/**
 * @file test_metrics_reporter.cc
 * @brief Phase 4 StatsManager + MetricsReporter 单元测试
 *
 * 测试覆盖：
 * - StatsManager: 单例、注册/注销、查找、dump_all、reset_all、groups()
 * - TextReporter: gem5 风格对齐列格式输出
 * - JSONReporter: 嵌套 JSON 层次结构输出
 * - MarkdownReporter: Markdown 表格 + JSON 摘要
 * - MultiReporter: 多格式同时生成
 *
 * 验收标准：
 * - 使用 Catch2 标签 [phase8-metrics-reporter]
 * - 不影响现有 327 测试用例
 * - 线程安全验证
 *
 * @author CppTLM Development Team
 * @date 2026-04-17
 */

#include <cstdio>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>
#include "catch_amalgamated.hpp"

// C++17: std::filesystem 用于跨平台目录操作
#include <filesystem>

#include "metrics/metrics_reporter.hh"
#include "metrics/stats.hh"
#include "metrics/stats_manager.hh"

namespace {

    // ============================================================================
    // StatsManager Tests
    // ============================================================================

    TEST_CASE("StatsManager.singleton", "[phase8-metrics-reporter][stats-manager]") {
        // 单例两次获取应返回同一引用
        auto& inst1 = tlm_stats::StatsManager::instance();
        auto& inst2 = tlm_stats::StatsManager::instance();
        REQUIRE(&inst1 == &inst2);
    }

    TEST_CASE("StatsManager.register_and_find", "[phase8-metrics-reporter][stats-manager]") {
        // 清理状态
        tlm_stats::StatsManager::instance().reset_all();

        tlm_stats::StatGroup group("system.cpu");
        tlm_stats::StatsManager::instance().register_group(&group, "system.cpu");

        auto* found = tlm_stats::StatsManager::instance().find_group("system.cpu");
        REQUIRE(found == &group);

        // 清理
        tlm_stats::StatsManager::instance().unregister_group("system.cpu");
    }

    TEST_CASE("StatsManager.unregister", "[phase8-metrics-reporter][stats-manager]") {
        tlm_stats::StatsManager::instance().reset_all();

        tlm_stats::StatGroup group("system.mem");
        tlm_stats::StatsManager::instance().register_group(&group, "system.mem");
        tlm_stats::StatsManager::instance().unregister_group("system.mem");

        auto* found = tlm_stats::StatsManager::instance().find_group("system.mem");
        REQUIRE(found == nullptr);
    }

    TEST_CASE("StatsManager.dump_all", "[phase8-metrics-reporter][stats-manager]") {
        tlm_stats::StatsManager::instance().reset_all();

        tlm_stats::StatGroup* group1 = new tlm_stats::StatGroup("cpu");
        group1->addScalar("cycles", "CPU cycles", "cycle");
        group1->addScalar("instr", "Instructions", "count");

        tlm_stats::StatsManager::instance().register_group(group1, "system.cpu");

        std::ostringstream oss;
        tlm_stats::StatsManager::instance().dump_all(oss, 50);

        std::string output = oss.str();
        REQUIRE(output.find("system.cpu.cycles") != std::string::npos);
        REQUIRE(output.find("system.cpu.instr") != std::string::npos);

        // 清理
        tlm_stats::StatsManager::instance().unregister_group("system.cpu");
        delete group1;
    }

    TEST_CASE("StatsManager.reset_all", "[phase8-metrics-reporter][stats-manager]") {
        tlm_stats::StatsManager::instance().reset_all();

        tlm_stats::StatGroup group("test.reset");
        auto& scalar = group.addScalar("counter", "Test counter", "count");
        scalar++;
        scalar++;
        scalar++;

        REQUIRE(scalar.value() == 3);

        tlm_stats::StatsManager::instance().register_group(&group, "test.reset");
        tlm_stats::StatsManager::instance().reset_all();

        REQUIRE(scalar.value() == 0);

        tlm_stats::StatsManager::instance().unregister_group("test.reset");
    }

    TEST_CASE("StatsManager.num_groups", "[phase8-metrics-reporter][stats-manager]") {
        tlm_stats::StatsManager::instance().reset_all();

        size_t initial = tlm_stats::StatsManager::instance().num_groups();

        tlm_stats::StatGroup g1("a"), g2("b");
        tlm_stats::StatsManager::instance().register_group(&g1, "group.a");
        tlm_stats::StatsManager::instance().register_group(&g2, "group.b");

        REQUIRE(tlm_stats::StatsManager::instance().num_groups() == initial + 2);

        tlm_stats::StatsManager::instance().unregister_group("group.a");
        tlm_stats::StatsManager::instance().unregister_group("group.b");
    }

    TEST_CASE("StatsManager.empty", "[phase8-metrics-reporter][stats-manager]") {
        tlm_stats::StatsManager::instance().reset_all();

        bool was_empty = tlm_stats::StatsManager::instance().empty();

        tlm_stats::StatGroup g("test.empty");
        tlm_stats::StatsManager::instance().register_group(&g, "test.empty");
        REQUIRE(!tlm_stats::StatsManager::instance().empty());

        tlm_stats::StatsManager::instance().unregister_group("test.empty");

        if (was_empty) {
            REQUIRE(tlm_stats::StatsManager::instance().empty());
        }
    }

    TEST_CASE("StatsManager.groups_accessor", "[phase8-metrics-reporter][stats-manager]") {
        tlm_stats::StatsManager::instance().reset_all();

        tlm_stats::StatGroup g("test.groups");
        tlm_stats::StatsManager::instance().register_group(&g, "test.groups");

        const auto& groups = tlm_stats::StatsManager::instance().groups();
        REQUIRE(groups.find("test.groups") != groups.end());
        REQUIRE(groups.at("test.groups") == &g);

        tlm_stats::StatsManager::instance().unregister_group("test.groups");
    }

    TEST_CASE("StatsManager.dump_text", "[phase8-metrics-reporter][stats-manager]") {
        tlm_stats::StatsManager::instance().reset_all();

        tlm_stats::StatGroup group("test.text");
        group.addScalar("value", "A value", "count");

        tlm_stats::StatsManager::instance().register_group(&group, "test.text");

        std::string text = tlm_stats::StatsManager::instance().dump_text(50);
        REQUIRE(text.find("test.text.value") != std::string::npos);

        tlm_stats::StatsManager::instance().unregister_group("test.text");
    }

    // ============================================================================
    // TextReporter Tests
    // ============================================================================

    TEST_CASE("TextReporter.generate", "[phase8-metrics-reporter][text-reporter]") {
        tlm_stats::StatsManager::instance().reset_all();

        tlm_stats::StatGroup group("sys.cpu");
        group.addScalar("cycles", "CPU cycles", "cycle");
        group.addScalar("instr", "Instructions", "count");

        tlm_stats::StatsManager::instance().register_group(&group, "sys.cpu");

        std::ostringstream oss;
        tlm_stats::TextReporter reporter;
        reporter.generate(oss);

        std::string output = oss.str();
        REQUIRE(output.find("---------- Begin Simulation Statistics ----------") !=
                std::string::npos);
        REQUIRE(output.find("sys.cpu.cycles") != std::string::npos);
        REQUIRE(output.find("sys.cpu.instr") != std::string::npos);
        REQUIRE(output.find("---------- End Simulation Statistics ----------") !=
                std::string::npos);

        tlm_stats::StatsManager::instance().unregister_group("sys.cpu");
    }

    TEST_CASE("TextReporter.string_form", "[phase8-metrics-reporter][text-reporter]") {
        tlm_stats::StatsManager::instance().reset_all();

        tlm_stats::StatGroup group("test.str");
        group.addScalar("x", "X value", "count");

        tlm_stats::StatsManager::instance().register_group(&group, "test.str");

        tlm_stats::TextReporter reporter;
        std::string output = reporter.generateToString();

        REQUIRE(output.find("test.str.x") != std::string::npos);

        tlm_stats::StatsManager::instance().unregister_group("test.str");
    }

    TEST_CASE("TextReporter.file_form", "[phase8-metrics-reporter][text-reporter]") {
        tlm_stats::StatsManager::instance().reset_all();

        tlm_stats::StatGroup group("test.file");
        group.addScalar("val", "Value", "count");

        tlm_stats::StatsManager::instance().register_group(&group, "test.file");

        tlm_stats::TextReporter reporter;
        std::string path = "/tmp/cpptlm_test_text.txt";
        bool success = reporter.generate(path);

        REQUIRE(success);

        // 验证文件内容
        std::ifstream ifs(path);
        REQUIRE(ifs.good());
        std::string content((std::istreambuf_iterator<char>(ifs)),
                            std::istreambuf_iterator<char>());
        REQUIRE(content.find("test.file.val") != std::string::npos);
        ifs.close();
        std::remove(path.c_str());

        tlm_stats::StatsManager::instance().unregister_group("test.file");
    }

    // ============================================================================
    // JSONReporter Tests
    // ============================================================================

    TEST_CASE("JSONReporter.generate", "[phase8-metrics-reporter][json-reporter]") {
        tlm_stats::StatsManager::instance().reset_all();

        tlm_stats::StatGroup group("sys.cpu");
        group.addScalar("cycles", "CPU cycles", "cycle");
        group.addScalar("instr", "Instructions", "count");

        tlm_stats::StatsManager::instance().register_group(&group, "sys.cpu");

        std::ostringstream oss;
        tlm_stats::JSONReporter reporter;
        reporter.generate(oss);

        std::string output = oss.str();
        // 验证 JSON 格式（简单解析检查）
        REQUIRE(output.find('{') != std::string::npos);
        REQUIRE(output.find("sys") != std::string::npos);
        REQUIRE(output.find("cpu") != std::string::npos);
        REQUIRE(output.find("cycles") != std::string::npos);

        tlm_stats::StatsManager::instance().unregister_group("sys.cpu");
    }

    TEST_CASE("JSONReporter.nested_path", "[phase8-metrics-reporter][json-reporter]") {
        tlm_stats::StatsManager::instance().reset_all();

        tlm_stats::StatGroup group("cache");
        group.addScalar("hits", "Cache hits", "count");

        // 注册为嵌套路径
        tlm_stats::StatsManager::instance().register_group(&group, "system.l1.cache");

        std::ostringstream oss;
        tlm_stats::JSONReporter reporter;
        reporter.generate(oss);

        std::string output = oss.str();
        // 应该包含嵌套路径
        REQUIRE(output.find("system") != std::string::npos);
        REQUIRE(output.find("l1") != std::string::npos);

        tlm_stats::StatsManager::instance().unregister_group("system.l1.cache");
    }

    TEST_CASE("JSONReporter.distribution", "[phase8-metrics-reporter][json-reporter]") {
        tlm_stats::StatsManager::instance().reset_all();

        tlm_stats::StatGroup group("test.dist");
        auto& dist = group.addDistribution("lat", "Latency", "cycle");
        dist.sample(5);
        dist.sample(10);
        dist.sample(15);

        tlm_stats::StatsManager::instance().register_group(&group, "test.dist");

        std::ostringstream oss;
        tlm_stats::JSONReporter reporter;
        reporter.generate(oss);

        std::string output = oss.str();
        // Distribution JSON 应包含 count, min, avg, max, stddev
        REQUIRE(output.find("\"count\"") != std::string::npos);
        REQUIRE(output.find("\"avg\"") != std::string::npos);
        REQUIRE(output.find("\"min\"") != std::string::npos);
        REQUIRE(output.find("\"max\"") != std::string::npos);

        tlm_stats::StatsManager::instance().unregister_group("test.dist");
    }

    TEST_CASE("JSONReporter.percentile_histogram", "[phase8-metrics-reporter][json-reporter]") {
        tlm_stats::StatsManager::instance().reset_all();

        tlm_stats::StatGroup group("test.pct");
        auto& pct = group.addPercentileHistogram("lat", "Latency", "cycle");
        for (int i = 1; i <= 100; ++i) {
            pct.record(i);
        }

        tlm_stats::StatsManager::instance().register_group(&group, "test.pct");

        std::ostringstream oss;
        tlm_stats::JSONReporter reporter;
        reporter.generate(oss);

        std::string output = oss.str();
        // PercentileHistogram JSON 应包含 p50, p95, p99, p99.9
        REQUIRE(output.find("\"p50\"") != std::string::npos);
        REQUIRE(output.find("\"p95\"") != std::string::npos);
        REQUIRE(output.find("\"p99\"") != std::string::npos);
        REQUIRE(output.find("\"p99.9\"") != std::string::npos);

        tlm_stats::StatsManager::instance().unregister_group("test.pct");
    }

    TEST_CASE("JSONReporter.file_form", "[phase8-metrics-reporter][json-reporter]") {
        tlm_stats::StatsManager::instance().reset_all();

        tlm_stats::StatGroup group("test.json.file");
        group.addScalar("x", "X", "count");

        tlm_stats::StatsManager::instance().register_group(&group, "test.json.file");

        tlm_stats::JSONReporter reporter;
        std::string path = "/tmp/cpptlm_test.json";
        bool success = reporter.generate(path);

        REQUIRE(success);

        std::ifstream ifs(path);
        REQUIRE(ifs.good());
        std::string content((std::istreambuf_iterator<char>(ifs)),
                            std::istreambuf_iterator<char>());
        // 验证是合法 JSON
        REQUIRE(content.find('{') == 0);
        ifs.close();
        std::remove(path.c_str());

        tlm_stats::StatsManager::instance().unregister_group("test.json.file");
    }

    // ============================================================================
    // MarkdownReporter Tests
    // ============================================================================

    TEST_CASE("MarkdownReporter.generate", "[phase8-metrics-reporter][markdown-reporter]") {
        tlm_stats::StatsManager::instance().reset_all();

        tlm_stats::StatGroup group("test.md");
        group.addScalar("value", "A value", "count");

        tlm_stats::StatsManager::instance().register_group(&group, "test.md");

        std::ostringstream oss;
        tlm_stats::MarkdownReporter reporter;
        reporter.generate(oss);

        std::string output = oss.str();
        REQUIRE(output.find("# Performance Metrics Report") != std::string::npos);
        REQUIRE(output.find("## JSON Format") != std::string::npos);
        REQUIRE(output.find("```json") != std::string::npos);
        REQUIRE(output.find("```") != std::string::npos); // 代码块结束
        REQUIRE(output.find("test.md.value") != std::string::npos);

        tlm_stats::StatsManager::instance().unregister_group("test.md");
    }

    // ============================================================================
    // MultiReporter Tests
    // ============================================================================

    TEST_CASE("MultiReporter.add_format", "[phase8-metrics-reporter][multi-reporter]") {
        tlm_stats::MultiReporter mr;
        mr.add_format(tlm_stats::MultiReporter::Format::TEXT, "/tmp/out.txt");
        mr.add_format(tlm_stats::MultiReporter::Format::JSON, "/tmp/out.json");
        mr.add_format(tlm_stats::MultiReporter::Format::MARKDOWN, "/tmp/out.md");
    }

    TEST_CASE("MultiReporter.generate_all", "[phase8-metrics-reporter][multi-reporter]") {
        tlm_stats::StatsManager::instance().reset_all();

        tlm_stats::StatGroup group("test.multi");
        group.addScalar("val", "Value", "count");

        tlm_stats::StatsManager::instance().register_group(&group, "test.multi");

        tlm_stats::MultiReporter mr;
        mr.add_format(tlm_stats::MultiReporter::Format::TEXT, "/tmp/multi_test.txt");
        mr.add_format(tlm_stats::MultiReporter::Format::JSON, "/tmp/multi_test.json");
        mr.add_format(tlm_stats::MultiReporter::Format::MARKDOWN, "/tmp/multi_test.md");

        bool success = mr.generate_all();
        REQUIRE(success);

        // 验证三个文件都生成
        std::ifstream txt("/tmp/multi_test.txt");
        REQUIRE(txt.good());
        txt.close();

        std::ifstream json("/tmp/multi_test.json");
        REQUIRE(json.good());
        json.close();

        std::ifstream md("/tmp/multi_test.md");
        REQUIRE(md.good());
        md.close();

        // 清理
        std::remove("/tmp/multi_test.txt");
        std::remove("/tmp/multi_test.json");
        std::remove("/tmp/multi_test.md");

        tlm_stats::StatsManager::instance().unregister_group("test.multi");
    }

    TEST_CASE("MultiReporter.generate_all_to_dir", "[phase8-metrics-reporter][multi-reporter]") {
        tlm_stats::StatsManager::instance().reset_all();

        tlm_stats::StatGroup group("test.dir");
        group.addScalar("x", "X", "count");

        tlm_stats::StatsManager::instance().register_group(&group, "test.dir");

        tlm_stats::MultiReporter mr;
        bool success = mr.generate_all("/tmp/cpptlm_metrics");
        REQUIRE(success);

        // 验证三个文件都生成
        std::ifstream txt("/tmp/cpptlm_metrics/metrics.txt");
        REQUIRE(txt.good());
        txt.close();

        std::ifstream json("/tmp/cpptlm_metrics/metrics.json");
        REQUIRE(json.good());
        json.close();

        std::ifstream md("/tmp/cpptlm_metrics/metrics.md");
        REQUIRE(md.good());
        md.close();

        // 清理（C++17 std::filesystem 跨平台替代 POSIX rmdir）
        std::filesystem::remove_all("/tmp/cpptlm_metrics");

        tlm_stats::StatsManager::instance().unregister_group("test.dir");
    }

    // ============================================================================
    // Thread Safety Tests
    // ============================================================================

    TEST_CASE("StatsManager.thread_safety",
              "[phase8-metrics-reporter][stats-manager][thread-safe]") {
        tlm_stats::StatsManager::instance().reset_all();

        const int num_threads = 4;
        const int groups_per_thread = 10;

        std::vector<std::thread> threads;

        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([t, groups_per_thread]() {
                for (int i = 0; i < groups_per_thread; ++i) {
                    std::string path =
                        "thread." + std::to_string(t) + ".group." + std::to_string(i);
                    tlm_stats::StatGroup* g = new tlm_stats::StatGroup("g");
                    g->addScalar("val", "V", "count");
                    tlm_stats::StatsManager::instance().register_group(g, path);

                    // 验证能找到
                    auto* found = tlm_stats::StatsManager::instance().find_group(path);
                    REQUIRE(found == g);

                    // dump 不应崩溃
                    std::ostringstream oss;
                    tlm_stats::StatsManager::instance().dump_all(oss, 50);
                }
            });
        }

        for (auto& t : threads) {
            t.join();
        }

        // 验证所有组都注册成功
        REQUIRE(tlm_stats::StatsManager::instance().num_groups() >=
                static_cast<size_t>(num_threads * groups_per_thread));

        // 清理
        for (int t = 0; t < num_threads; ++t) {
            for (int i = 0; i < groups_per_thread; ++i) {
                std::string path = "thread." + std::to_string(t) + ".group." + std::to_string(i);
                tlm_stats::StatsManager::instance().unregister_group(path);
            }
        }
    }

    // ============================================================================
    // Integration: Full Pipeline
    // ============================================================================

    TEST_CASE("MetricsReporter.full_pipeline", "[phase8-metrics-reporter][integration]") {
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
        cpu_lat.sample(30);

        tlm_stats::StatsManager::instance().register_group(cpu_group, "system.cpu");

        tlm_stats::StatGroup* mem_group = new tlm_stats::StatGroup("mem");
        auto& mem_bw = mem_group->addScalar("bandwidth", "Memory bandwidth", "GB/s");
        mem_bw += 50;

        tlm_stats::StatsManager::instance().register_group(mem_group, "system.memory");

        // 测试 TextReporter
        {
            std::ostringstream oss;
            tlm_stats::TextReporter rep;
            rep.generate(oss);
            std::string out = oss.str();
            REQUIRE(out.find("system.cpu.cycles") != std::string::npos);
            REQUIRE(out.find("system.memory.bandwidth") != std::string::npos);
        }

        // 测试 JSONReporter
        {
            std::ostringstream oss;
            tlm_stats::JSONReporter rep;
            rep.generate(oss);
            std::string out = oss.str();
            REQUIRE(out.find("cpu") != std::string::npos);
            REQUIRE(out.find("memory") != std::string::npos);
        }

        // 测试 MarkdownReporter
        {
            std::ostringstream oss;
            tlm_stats::MarkdownReporter rep;
            rep.generate(oss);
            std::string out = oss.str();
            REQUIRE(out.find("system.cpu.latency.count") != std::string::npos);
        }

        // 清理
        tlm_stats::StatsManager::instance().unregister_group("system.cpu");
        tlm_stats::StatsManager::instance().unregister_group("system.memory");
        delete cpu_group;
        delete mem_group;
    }

} // namespace
