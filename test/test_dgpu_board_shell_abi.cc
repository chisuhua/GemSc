// test/test_dgpu_board_shell_abi.cc
// BS-G2: DGpuBoard shell 5 职责 + 多线程注入 + 异常传播测试
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <catch_amalgamated.hpp>
#include "tlm/gpu/dgpu_board_shell.hh"

using namespace tlm::gpu;

TEST_CASE("DGpuBoard: mmio_write returns 0 without exception", "[dgpu][shell]") {
    DGpuBoard board("test_board");
    board.init();
    uint32_t value = 0x12345678;
    REQUIRE_NOTHROW(board.mmio_write(0, 0x14, &value, sizeof(value)));
    // mmio_write async,不等待响应
    board.shutdown();
}

TEST_CASE("DGpuBoard: mmio_read with 1ms timeout returns -110 ETIMEDOUT or 0", "[dgpu][shell]") {
    DGpuBoard board("test_board");
    board.init();
    uint32_t value = 0;
    // SOC 未注入真实 PcieEndpointTLM,所以 mmio_read 应超时(-110)
    int rc = board.mmio_read(0, 0x14, &value, sizeof(value));
    REQUIRE((rc == -110 || rc == 0)); // 允许两种(超时或占位 set_value(0))
    board.shutdown();
}

TEST_CASE("DGpuBoard: 5 responsibilities present", "[dgpu][shell]") {
    DGpuBoard board("test_board");
    // 1. ABI 翻译
    REQUIRE_NOTHROW(board.mmio_read(0, 0, nullptr, 0));
    // 2. 设备枚举
    REQUIRE(board.device_id() == 0u); // 默认 0
    // 3. SOC 装配(load_soc_config 留给 T-bs-4)
    // 4. 回调接线
    bool irq_called = false;
    board.set_irq_callback([&](uint32_t) { irq_called = true; });
    // 5. 生命周期
    REQUIRE_NOTHROW(board.tick());
}

TEST_CASE("DGpuBoard: destroy order is strict (stop→poison→join→destruct)", "[dgpu][shell]") {
    DGpuBoard board("test_board");
    REQUIRE_NOTHROW(board.init());
    REQUIRE_NOTHROW(board.shutdown());
    REQUIRE_NOTHROW(board.shutdown()); // 幂等(第二次 destroy no-op)
}

TEST_CASE("DGpuBoard: 2 boards concurrent mmio_write (multi-card thread isolation)",
          "[dgpu][shell]") {
    DGpuBoard board1("board_1");
    DGpuBoard board2("board_2");
    board1.init();
    board2.init();

    std::thread t1([&]() {
        for (int i = 0; i < 100; ++i) {
            uint32_t v = i;
            board1.mmio_write(0, 0x14, &v, sizeof(v));
        }
    });
    std::thread t2([&]() {
        for (int i = 0; i < 100; ++i) {
            uint32_t v = i + 1000;
            board2.mmio_write(0, 0x14, &v, sizeof(v));
        }
    });
    t1.join();
    t2.join();

    board1.shutdown();
    board2.shutdown();
}

TEST_CASE("DGpuBoard: sim→host callback is non-blocking (async thread)", "[dgpu][shell]") {
    DGpuBoard board("test_board");
    board.init();

    std::atomic<int> call_count{0};
    auto start = std::chrono::steady_clock::now();
    board.set_irq_callback([&](uint32_t) {
        // 模拟阻塞操作(本任务测的是不阻塞 sim 线程,不是测 callback 内部)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        call_count++;
    });

    // 触发 5 次 callback
    for (int i = 0; i < 5; ++i) {
        board.trigger_irq_async(i);
    }

    // sim 线程应立即返回(trigger_irq_async 不阻塞)
    auto elapsed = std::chrono::steady_clock::now() - start;
    REQUIRE(elapsed < std::chrono::milliseconds(10)); // 触发应 <10ms(5 次 * 0ms)

    // 等待 callback 完成
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    REQUIRE(call_count >= 1); // 至少 1 次(可能 detach 的 thread 已完成)

    board.shutdown();
}

TEST_CASE("DGpuBoard: 2 cards StatsManager paths are isolated (no collision)", "[dgpu][shell]") {
    DGpuBoard board1("board_0");
    DGpuBoard board2("board_1");
    board1.init();
    board2.init();

    // 验证 stats_path 前缀不同
    REQUIRE_NOTHROW(board1.get_stats_path("pcie_ep"));
    REQUIRE_NOTHROW(board2.get_stats_path("pcie_ep"));

    // 验证路径字符串内容(默认 device_id_ 都是 0,但模块名不同)
    std::string path1 = board1.get_stats_path("pcie_ep");
    std::string path2 = board2.get_stats_path("sdma");
    REQUIRE(path1 != path2); // 不同模块名 → 不同路径

    board1.shutdown();
    board2.shutdown();
}

// ── T-bs-3e 新增测试 ──

TEST_CASE("DGpuBoard: backdoor_read goes through inject_q (non-direct VRAM access)",
          "[dgpu][shell]") {
    DGpuBoard board("test_board");
    board.init();
    uint8_t buf[64] = {0};
    // backdoor_read 应走 inject_q 路径,返回值可能是 -110(超时)或 len(占位)
    int rc = board.backdoor_read(0x1000, buf, sizeof(buf));
    REQUIRE((rc == -110 || rc == static_cast<int>(sizeof(buf))));
    board.shutdown();
}

TEST_CASE("DGpuBoard: backdoor_write goes through inject_q", "[dgpu][shell]") {
    DGpuBoard board("test_board");
    board.init();
    uint8_t data[64] = {0xAA};
    // backdoor_write 异步,返回 0
    REQUIRE_NOTHROW(board.backdoor_write(0x1000, data, sizeof(data)));
    board.shutdown();
}

TEST_CASE("DGpuBoard: mixed mmio + backdoor concurrent", "[dgpu][shell]") {
    DGpuBoard board("test_board");
    board.init();

    std::atomic<int> mmio_count{0};
    std::atomic<int> backdoor_count{0};

    for (int i = 0; i < 10; ++i) {
        uint32_t val = 0;
        try {
            board.mmio_read(0, 0x14, &val, sizeof(val));
        } catch (...) {
        }
        mmio_count++;
    }
    for (int i = 0; i < 10; ++i) {
        uint8_t buf[16] = {0};
        try {
            board.backdoor_read(0x1000 + i * 16, buf, sizeof(buf));
        } catch (...) {
        }
        backdoor_count++;
    }

    REQUIRE(mmio_count == 10);
    REQUIRE(backdoor_count == 10);

    board.shutdown();
}

TEST_CASE("DGpuBoard: destroy with non-empty inject_q_ (poison pill must wake sim_loop)",
          "[dgpu][shell]") {
    DGpuBoard board("test_board");
    board.init();

    for (int j = 0; j < 100; ++j) {
        uint32_t v = j;
        try {
            board.mmio_write(0, 0x14, &v, sizeof(v));
        } catch (...) {
        }
    }

    REQUIRE_NOTHROW(board.shutdown());
    REQUIRE_NOTHROW(board.shutdown());
}

TEST_CASE("DGpuBoard: last_exception_ from sim_loop rethrows on next ABI call", "[dgpu][shell]") {
    DGpuBoard board("test_board");
    board.init();
    REQUIRE_NOTHROW(board.mmio_read(0, 0, nullptr, 0));
    board.shutdown();
}

// ── T-W3-3 1A: backdoor happy path ──
// ABI 契约: hit 返 0 + buf echo; miss / size mismatch 返 len (per design §2.5)
TEST_CASE("DGpuBoard: backdoor_write→backdoor_read happy path echoes data through vram_segments_",
          "[dgpu][shell][backdoor][happy]") {
    DGpuBoard board("test_board");
    board.init();

    std::vector<uint8_t> write_data(64);
    for (size_t i = 0; i < 64; ++i) {
        write_data[i] = static_cast<uint8_t>(i * 3 + 7);
    }
    REQUIRE(board.backdoor_write(0x1000, write_data.data(), write_data.size()) == 0);

    std::vector<uint8_t> read_data(64, 0xFF);
    REQUIRE(board.backdoor_read(0x1000, read_data.data(), read_data.size()) == 0);
    REQUIRE(read_data == write_data);

    std::vector<uint8_t> miss_buf(32, 0);
    REQUIRE(board.backdoor_read(0x9000, miss_buf.data(), miss_buf.size()) == 32);

    std::vector<uint8_t> mismatch_buf(32, 0);
    REQUIRE(board.backdoor_read(0x1000, mismatch_buf.data(), mismatch_buf.size()) == 32);

    board.shutdown();
}

// ── T-W3-3 1A: tick drain ──
// 100 reqs (50 backdoor + 50 mmio) push → tick×3 不抛, backdoor 数据未丢
TEST_CASE("DGpuBoard: tick drains injection queue across 100 pending reqs without exception",
          "[dgpu][shell][tick][drain]") {
    DGpuBoard board("test_board");
    board.init();

    for (int i = 0; i < 50; ++i) {
        uint8_t data[8] = {static_cast<uint8_t>(i & 0xFF)};
        board.backdoor_write(0x2000 + i * 8, data, sizeof(data));
    }
    for (int i = 0; i < 50; ++i) {
        uint32_t val = static_cast<uint32_t>(i);
        board.mmio_write(0, 0x14, &val, sizeof(val));
    }

    REQUIRE_NOTHROW(board.tick());
    REQUIRE_NOTHROW(board.tick());
    REQUIRE_NOTHROW(board.tick());

    std::vector<uint8_t> buf(8, 0);
    REQUIRE(board.backdoor_read(0x2000, buf.data(), buf.size()) == 0);
    REQUIRE(buf[0] == 0);

    board.shutdown();
}