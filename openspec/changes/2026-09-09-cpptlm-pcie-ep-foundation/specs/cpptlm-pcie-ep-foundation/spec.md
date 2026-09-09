# Spec: cpptlm-pcie-ep-foundation

> **Capability**: cpptlm-pcie-ep-foundation
> **Owner**: CppTLM Architecture Team
> **Status**: 🔄 Proposed（2026-09-09）
> **Created**: 2026-09-09
> **关联**: [proposal.md](../proposal.md) + [design.md](../design.md) + [tasks.md](../tasks.md)

## Purpose

补全 CppTLM dGPU **基础必备 PCIe 能力**（PCIe EP 基础 / MSI-X / DMA 引擎 / 电源管理）+ **性能增强 P2P**，消除 Oracle 三轮审查揭示的 7 个根本性错误（PCIe EP 评审 3.5/10 + CP attach 评审 3.5/10 + 5.5.8 评审 8.7/10）。解锁 UsrLinuxEmu 5.5.6+ dGPU E2E 主线（用户 2026-09-09 战略调整：**先打通 CppTLM PCIe EP 协同流程，再考虑 CommandProcessor**）。

4 层 PCIe 能力框架：
- **基础必备**（本 change 实施）：PCIe EP + MSI-X + DMA + 电源管理
- **性能增强**（本 change 阶段 2）：P2P + Resizable BAR
- **虚拟化必备**（本 change 排除）：SR-IOV + VF 配置 + VF 中断隔离
- **高级可选**（本 change 排除）：CXL / NTB / TPH / ATS / PRI / PASID

## ADDED Requirements

### Requirement: PCIe Configuration Space Real Implementation

The system MUST implement `cpptlm_emulator_pcie_config_read` / `_write` by forwarding to `DGpuBoard::ep_->cfg_space_->read` / `write` (not returning -ENOSYS stub). Configuration space MUST contain standard Type 0 Header fields: Vendor ID (0x10DE), Device ID, Class Code (0x03 for Display Controller), Revision ID.

#### Scenario: Vendor ID readable via config_read

- **WHEN** `cpptlm_emulator_pcie_config_read(emu, 0x00, 4, &val)` is called
- **THEN** the function returns 0
- **AND** `val == 0x10DE` (NVIDIA vendor ID, or AMD 0x1002 as configured)

#### Scenario: Class Code readable

- **WHEN** `cpptlm_emulator_pcie_config_read(emu, 0x08, 4, &val)` is called
- **THEN** `val & 0xFF000000` contains Class Code 0x03 (Display Controller)

### Requirement: mmio_read Returns Real Data (Not Garbage)

The system MUST implement `cpptlm_emulator_mmio_read` such that after `sim_loop` drains the injected request (T-bs-3c data copy), the user-supplied buffer is filled with the actual data read from the BAR register, not the uninitialized buffer contents.

#### Scenario: mmio_read fills buffer with real data

- **WHEN** driver writes `0xDEADBEEF` to `mmio_write(emu, 0, 0, src, 4)`
- **AND** `mmio_read(emu, 0, 0, buf, 4)` is called
- **THEN** `buf` contains `0xDEADBEEF` (real roundtrip data)
- **AND** `memcmp(buf, src, 4) == 0`

#### Scenario: mmio_read race eliminated

- **WHEN** `mmio_read` is invoked 2000 times consecutively with proper TLP injection
- **THEN** the failure rate (return -110) is < 0.1%
- **AND** the buffer is filled with real data in ≥ 99.9% of calls

### Requirement: mmio_write Data Persisted

The system MUST implement `cpptlm_emulator_mmio_write` synchronously, blocking until `sim_loop` drains the injected request and the data is persisted to the BAR register. Currently the function returns immediately (data discarded).

#### Scenario: mmio_write blocks until data persisted

- **WHEN** `mmio_write(emu, 0, 0, src, 4)` is called
- **THEN** the function returns after `sim_loop` has processed the write
- **AND** a subsequent `mmio_read(emu, 0, 0, buf, 4)` returns the same data (roundtrip verified)

### Requirement: backdoor_read Returns -ENOENT on Miss

The system MUST change `cpptlm_emulator_backdoor_read` to return `-ENOENT` when the requested `(bar, offset, len)` does not match a stored segment, NOT `len` (which masquerades as byte-count success).

#### Scenario: backdoor_read miss returns -ENOENT

- **WHEN** `backdoor_read(emu, bar=0, offset=4096, buf, 4)` is called (offset 4096 not stored)
- **THEN** the function returns `-ENOENT` (negative error code)
- **AND** `buf` is left unchanged

#### Scenario: backdoor_read hit returns 0 with data

- **WHEN** driver writes `0xCAFEBABE` to `backdoor_write(emu, 0, 0, src, 4)`
- **AND** `backdoor_read(emu, 0, 0, buf, 4)` is called
- **THEN** the function returns 0
- **AND** `buf` contains `0xCAFEBABE` (real roundtrip data)

### Requirement: register_backdoor_cb Actually Invoked

The system MUST change `cpptlm_emulator_register_backdoor_cb` to actually store the callback and invoke it when `DGpuBoard::backdoor_read/write` is called. Currently the callback is discarded (`(void)cb`).

#### Scenario: backdoor_cb invoked on read

- **WHEN** `register_backdoor_cb(emu, my_cb)` is called
- **AND** `backdoor_read(emu, bar, offset, buf, len)` is invoked
- **THEN** `my_cb` is invoked with the corresponding parameters
- **AND** the callback's return value propagates to the caller of `backdoor_read`

### Requirement: register_dma_translate_cb Returns Real Translation (Not pa=0)

The system MUST change `cpptlm_emulator_register_dma_translate_cb` to actually invoke the callback for DMA address translation, NOT hardcode `return 0` (pa=0). The callback signature is adapted between board shell layer (`uint64_t(uint64_t iova, size_t size)`) and SDMA engine layer (`int(uint64_t iova, uint32_t size, uint64_t* phys)`).

#### Scenario: dma_translate_cb invoked with identity mapping

- **WHEN** `register_dma_translate_cb(emu, identity_cb)` is called where `identity_cb(iova, size, &phys) { phys = iova; return 0; }`
- **AND** SDMA engine initiates a DMA transfer to `iova=0x1000, size=4096`
- **THEN** `identity_cb` is invoked with `(iova=0x1000, size=4096)`
- **AND** the SDMA engine reads/writes physical address `0x1000` (not `0x0`)

#### Scenario: dma_translate_cb fallback on error

- **WHEN** `dma_translate_cb` returns non-zero (translation error)
- **THEN** the SDMA engine treats the translation as failed (pa=0 fallback)
- **AND** logs a WARN message

### Requirement: MSI-X Interrupt Chain Complete

The system MUST wire `pcie_ep.irq_out` → `board->trigger_irq_async` so that when `cpptlm_emulator_msix_update_pending(emu, vector)` is called, the registered `intr_cb` is actually invoked (currently the chain is broken — intr_cb is stored but no caller invokes it).

#### Scenario: msix_update_pending triggers intr_cb

- **WHEN** `register_callbacks(emu, intr_cb, ...)` is called
- **AND** `msix_init(emu, 4, 0)` initializes 4 vectors
- **AND** `msix_update_pending(emu, 0)` is called
- **THEN** within 200ms, `intr_cb` is invoked with vector 0
- **AND** the invocation count is ≥ 1

#### Scenario: msix_clear_pending acknowledges

- **WHEN** `msix_update_pending(emu, 0)` triggers `intr_cb`
- **AND** `msix_clear_pending(emu, 0)` is called
- **THEN** the next `msix_update_pending(emu, 0)` can re-trigger `intr_cb`

### Requirement: MSI-X Capability + 4-8 Vectors

The system MUST implement MSI-X Extended Capability in PCIe configuration space, with at least 4 interrupt vectors supporting command completion / error / firmware communication / power management events.

#### Scenario: MSI-X Capability discoverable

- **WHEN** the driver enumerates PCIe Extended Capabilities
- **THEN** the MSI-X Capability (ID=0x11) is found at the expected offset

#### Scenario: 4 vectors init

- **WHEN** `msix_init(emu, 4, 0x0)` is called
- **THEN** the function returns 0
- **AND** `msix_update_pending(emu, 0..3)` triggers `intr_cb` for vectors 0-3

### Requirement: DMA Engine Scatter-Gather Support

The system MUST implement `sdma_engine_tlm` with Scatter-Gather DMA descriptors (one DMA transfer can span multiple non-contiguous physical pages). Supports SDMA Ring Buffer + RPTR/WPTR + Doorbell mechanism.

#### Scenario: SG DMA transfer completes

- **WHEN** driver submits a SG descriptor with 3 non-contiguous pages (`iova=0x0, 0x1000, 0x2000`, total size 12KB)
- **AND** rings the doorbell
- **THEN** the SDMA engine initiates 3 PCIe TLP reads
- **AND** `dma_complete_cb` fires when all 3 pages are transferred

### Requirement: IOMMU-Compatible DMA Address Translation

The system MUST support IOMMU address translation for DMA addresses (Intel VT-d / AMD IOMMU). The translation callback receives `iova` and returns `phys` (physical address), with size parameter for TLB lookup.

#### Scenario: IOMMU translation succeeds

- **WHEN** `dma_translate_cb` is registered with IOMMU translation function
- **AND** DMA transfer to `iova=0xDEAD0000` (guest virtual address)
- **THEN** `phys` is the host physical address (e.g., `0x1000_0000` after IOMMU translation)
- **AND** the SDMA engine uses `phys` for PCIe TLP

### Requirement: Power Management D0/D3 States

The system MUST implement PCIe PM Capability with D0 / D3hot / D3cold state transitions. ASPM (Active State Power Management) MUST support L0s and L1 low-power link states.

#### Scenario: D0 to D3hot transition

- **WHEN** driver writes `PMCSR = 0x3` (D3hot request)
- **THEN** the GPU transitions to D3hot state
- **AND** MMIO accesses return `-ENODEV` until D0 resume

#### Scenario: ASPM L1 entry

- **WHEN** the link is idle for ≥ 100µs
- **THEN** the link enters L1 state
- **AND** the link exits L1 within 1µs when traffic resumes

### Requirement: P2P DMA Support

The system MUST support Peer-to-Peer DMA, allowing the GPU to directly access memory of other PCIe devices (e.g., another GPU's VRAM, NVMe SSD, NIC) without CPU intervention. Requires ACS Capability for routing policy control.

#### Scenario: GPU P2P read from NVMe

- **WHEN** GPU initiates a P2P DMA to NVMe SSD BAR space
- **THEN** the DMA completes without CPU intervention
- **AND** the data is copied to GPU VRAM

### Requirement: Resizable BAR Capability

The system MUST implement Resizable BAR Capability, allowing the CPU to map the GPU's entire VRAM into CPU address space (avoiding fragmented mapping performance loss).

#### Scenario: Resize BAR to 8GB

- **WHEN** driver writes `BAR_SIZE = 0b11` (8GB) to Resizable BAR Control register
- **THEN** the BAR aperture is resized to 8GB
- **AND** subsequent MMIO accesses to the resized BAR are routed correctly