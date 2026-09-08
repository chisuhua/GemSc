// test_dgpu_pcie_device_perspective.cc
// PCIe driver perspective tests using DGpuBoard C++ shell (per ADR-SOC-07 D7).
// Replaces s2 monolith (per design.md §5 stage-1 deprecation).
// Uses:
//   - board.mmio_write()         for BAR0 register access (doorbell, GPFIFO_PUT)
//   - board.backdoor_write/read() for BAR1 VRAM (per ADR-SOC-07 Q3)
//   - board.device_info()        for BAR sizes (replaces bar0_size/bar1_size)
// State-observability assertions (cp_is_idle, sq_pending_count, doorbell_sq_tail)
// deferred to follow-up shell accessor work.
// Author: CppTLM Team
// Date: 2026-08-29 (T-bs-4 stage-1 adaptation)

#include <array>
#include <cstdint>
#include <cstring>
#include "catch_amalgamated.hpp"
#include "core/event_queue.hh"
#include "tlm/gpu/dgpu_board_shell.hh"
#include <nlohmann/json.hpp>

namespace {
    constexpr uint32_t GPU_REG_GPFIFO_PUT = 0x0000;
    constexpr uint32_t GPU_REG_DOORBELL = 0x0014;
    constexpr size_t VRAM_TEST_OFFSET = 0x2000;

    // Inline board config mirrors configs/dgpu_board_v1.json (cwd-independent).
    nlohmann::json load_board_config() {
        constexpr const char* kBoardCfg = R"({
            "name": "dgpu_board_v1",
            "params": {
                "device_id": "0x1234",
                "quantum_cycles": 1000,
                "ptx_emu_root": "/tmp/test-ptx-emu"
            },
            "modules": [{
                "name": "soc",
                "type": "DGpuSoc",
                "modules": [
                    { "name": "pcie_ep", "type": "PcieEndpointTLM",
                      "params": {
                          "config_size": 4096,
                          "num_msix_vectors": 16,
                          "bar_sizes": [65536, 268435456],
                          "bar0_registers": [
                              { "offset": 0, "name": "GPU_REG_GPFIFO_PUT", "access": "rw" },
                              { "offset": 20, "name": "GPU_REG_DOORBELL", "access": "wo", "side_effect": "doorbell" }
                          ]
                      }
                    },
                    { "name": "sdma", "type": "SdmaEngineTLM", "params": { "max_inflight": 4 } },
                    { "name": "cp",   "type": "CommandProcessorTLM" },
                    { "name": "tmu",  "type": "TmuDispatchProcessorTLM" },
                    { "name": "sq",   "type": "SubmitQueueTLM" },
                    { "name": "cq",   "type": "CompletionRingTLM" },
                    { "name": "gpu",  "type": "GpuCluster",
                      "config": "configs/templates/gpu_2gpc_2tpc_2cu.json" },
                    { "name": "vram", "type": "MemoryTLM", "params": { "capacity_gb": 1 } }
                ],
                "connections": [
                    { "src": "pcie_ep.mmio_out", "dst": "cp.cmd_in" },
                    { "src": "pcie_ep.mem_out",  "dst": "vram.0" },
                    { "src": "cp.fetch_out",     "dst": "vram.0" },
                    { "src": "cp.dma_req",       "dst": "sdma.desc_in" },
                    { "src": "cp.dispatch",      "dst": "tmu.dispatch_in" },
                    { "src": "tmu.cta_out",      "dst": "sq.cta_in" },
                    { "src": "sq.dispatch",      "dst": "gpu.cta_in" },
                    { "src": "gpu.done",         "dst": "cq.done_in[0]" },
                    { "src": "sdma.done_out",    "dst": "cq.done_in[1]" },
                    { "src": "cq.done_out",      "dst": "tmu.done_in" },
                    { "src": "gpu.done",         "dst": "sq.done_in[0]" }
                ],
                "outputs": [
                    { "internal": "pcie_ep.irq_out", "external": "irq" },
                    { "internal": "sdma.host_out",   "external": "host_dma" }
                ],
                "inputs": [
                    { "internal": "pcie_ep.slave_in", "external": "host_tlp" }
                ]
            }],
            "connections": []
        })";
        return nlohmann::json::parse(kBoardCfg);
    }
} // namespace

TEST_CASE("PCIe driver perspective: BAR0 doorbell write (shell path)",
          "[pcie][dGPU][soc][stage-1]") {
    EventQueue eq;
    tlm::gpu::DGpuBoard board("pcie_dgpu", &eq);
    auto cfg = load_board_config();
    REQUIRE(board.load_soc_config(cfg));

    uint32_t val = 0x00000001;
    REQUIRE(board.mmio_write(0, GPU_REG_DOORBELL, &val, sizeof(val)) == 0);
    // cp_is_idle assertion deferred: requires board.soc()->cp() accessor
    // (per design.md §5 stage-1 deprecation path; T-bs-4 后置 work item).
}

TEST_CASE("PCIe driver perspective: BAR1 VRAM write/read round trip (shell backdoor)",
          "[pcie][dGPU][soc][stage-1]") {
    EventQueue eq;
    tlm::gpu::DGpuBoard board("pcie_dgpu", &eq);
    auto cfg = load_board_config();
    REQUIRE(board.load_soc_config(cfg));

    REQUIRE(board.device_info().bar_sizes[1] == 256ULL * 1024ULL * 1024ULL);

    const std::array<uint8_t, 16> image = {0x50, 0x54, 0x58, 0x49, 0x52, 0x00, 0x01, 0x00,
                                           0xDE, 0xAD, 0xBE, 0xEF, 0x11, 0x22, 0x33, 0x44};
    std::array<uint8_t, 16> readback{};

    REQUIRE(board.backdoor_write(VRAM_TEST_OFFSET, image.data(), image.size()) == 0);
    board.tick(); // drain inject_q (per design §2.5 #5)
    REQUIRE(board.backdoor_read(VRAM_TEST_OFFSET, readback.data(), readback.size()) == 0);
    REQUIRE(readback == image);
}

TEST_CASE("PCIe driver perspective: invalid BAR1 DMA bounds fail with errno",
          "[pcie][dGPU][soc][stage-1]") {
    EventQueue eq;
    tlm::gpu::DGpuBoard board("pcie_dgpu", &eq);
    auto cfg = load_board_config();
    REQUIRE(board.load_soc_config(cfg));

    uint8_t byte = 0xFF;
    const auto bar1_size = board.device_info().bar_sizes[1];

    REQUIRE(board.backdoor_write(bar1_size, &byte, 1) == -22);
    REQUIRE(board.backdoor_read(bar1_size, &byte, 1) == -22);
    REQUIRE(board.backdoor_write(0, nullptr, 1) == -22);
}

TEST_CASE("PCIe driver perspective: GPFIFO PUT register is an MMIO address",
          "[pcie][dGPU][soc][stage-1]") {
    EventQueue eq;
    tlm::gpu::DGpuBoard board("pcie_dgpu", &eq);
    auto cfg = load_board_config();
    REQUIRE(board.load_soc_config(cfg));

    uint32_t val = 4;
    REQUIRE(board.mmio_write(0, GPU_REG_GPFIFO_PUT, &val, sizeof(val)) == 0);
    board.tick();
    // sq_inflight_count assertion deferred (requires shell->soc()->sq() accessor).
}

// Deferred to follow-up per design.md §5 stage-1:
//   - "IOCTL 0x27/0x28/0x29 ABI path"
//   - "PUSHBUFFER submit then MMIO doorbell" (depends on UsrLinuxEmuIoctlStub IOCTL stub)
// Both depend on UsrLinuxEmuIoctlStub being refactored to attach DGpuBoard shell,
// which is sequenced into T-bs-4 follow-up + subsequent adapter iteration.