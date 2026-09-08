// test/test_dgpu_board_msix_wrappers.cc
// T-W3-3 Phase 2: DGpuBoard 4 wrapper 方法 (msix_init/update_pending/clear_pending +
// lookup_register) SOC 未实例化时返 -ENOSYS (-38);参数错误返 -EINVAL (-22)
// D15 (5425c45): 永久回归 — load_soc_config 后 SOC 真正实例化,wrapper 转发
#include <catch_amalgamated.hpp>
#include "core/event_queue.hh"
#include "tlm/gpu/dgpu_board_shell.hh"
#include <nlohmann/json.hpp>

using namespace tlm::gpu;
using json = nlohmann::json;

TEST_CASE("DGpuBoard::msix_init returns -ENOSYS when SOC not instantiated",
          "[dgpu][shell][msix][wrapper]") {
    DGpuBoard board("test_board");
    board.init();
    REQUIRE(board.msix_init(16, 0xFFFFu) == -38);
    REQUIRE(board.msix_init(2049, 0u) == -22); // table_size > 2048
    board.shutdown();
}

TEST_CASE("DGpuBoard::msix_update_pending returns -ENOSYS when SOC not instantiated",
          "[dgpu][shell][msix][wrapper]") {
    DGpuBoard board("test_board");
    board.init();
    REQUIRE(board.msix_update_pending(0) == -38);
    board.shutdown();
}

TEST_CASE("DGpuBoard::msix_clear_pending returns -ENOSYS when SOC not instantiated",
          "[dgpu][shell][msix][wrapper]") {
    DGpuBoard board("test_board");
    board.init();
    REQUIRE(board.msix_clear_pending(0) == -38);
    board.shutdown();
}

TEST_CASE("DGpuBoard::lookup_register validates args and returns -ENOSYS without SOC",
          "[dgpu][shell][lookup_register][wrapper]") {
    DGpuBoard board("test_board");
    board.init();
    uint32_t val = 0;
    REQUIRE(board.lookup_register(0x14, nullptr) == -22); // null value
    REQUIRE(board.lookup_register(0x15, &val) == -22);    // unaligned
    REQUIRE(board.lookup_register(0x10000, &val) == -22); // > BAR0
    REQUIRE(board.lookup_register(0x14, &val) == -38);    // SOC null
    board.shutdown();
}

// D15 (5425c45) regression: load_soc_config 后 SOC 真正实例化,wrapper 转发到
// PcieEndpointTLM::msix()/bar_router()。inline JSON 避免 cwd 依赖。
static json d15_mini_board_cfg() {
    return json::parse(R"({
        "name": "d15_probe_board",
        "params": {"device_id": "0x1234", "quantum_cycles": 1000,
                   "ptx_emu_root": "/tmp/test-ptx-emu"},
        "modules": [{
            "name": "soc", "type": "DGpuSoc",
            "modules": [{
                "name": "pcie_ep", "type": "PcieEndpointTLM",
                "params": {
                    "config_size": 4096, "num_msix_vectors": 16,
                    "bar_sizes": [65536, 268435456],
                    "bar0_registers": [
                        {"offset": 0, "name": "GPU_REG_GPFIFO_PUT", "access": "rw"},
                        {"offset": 20, "name": "GPU_REG_DOORBELL", "access": "wo", "side_effect": "doorbell"}
                    ]
                }
            }],
            "connections": []
        }],
        "connections": []
    })");
}

TEST_CASE("D15 regression: load_soc_config instantiates SOC so msix wrappers forward",
          "[dgpu][shell][msix][d15][regression]") {
    DGpuBoard board("d15_test_board");
    board.init();
    REQUIRE(board.load_soc_config(d15_mini_board_cfg()));
    REQUIRE(board.msix_init(16, 0xFFFFu) == 0); // forwards → 0 (was -38 pre-D15)
    REQUIRE(board.msix_init(2049, 0u) == -22);  // table_size > 2048
    REQUIRE(board.msix_update_pending(0) == 0);
    REQUIRE(board.msix_clear_pending(0) == 0);
    board.shutdown();
}

TEST_CASE(
    "D15 regression: lookup_register hits PcieEndpointTLM bar0_registers after SOC instantiate",
    "[dgpu][shell][lookup_register][d15][regression]") {
    DGpuBoard board("d15_test_board");
    board.init();
    REQUIRE(board.load_soc_config(d15_mini_board_cfg()));

    uint32_t val = 0;
    REQUIRE(board.lookup_register(0x14, nullptr) == -22);
    REQUIRE(board.lookup_register(0x15, &val) == -22);    // unaligned
    REQUIRE(board.lookup_register(0x10000, &val) == -22); // > BAR0

    REQUIRE(board.lookup_register(0, &val) == 0);       // GPFIFO_PUT hit
    REQUIRE(board.lookup_register(20, &val) == 0);      // DOORBELL hit
    REQUIRE(board.lookup_register(0x100, &val) == -38); // miss

    board.shutdown();
}
