# Proposal: cpptlm-pcie-ep-foundation — dGPU PCIe EP 基础必备能力补完

> **状态**: 🔄 Proposed v1.0（2026-09-09）
> **优先级**: P0（前置 UsrLinuxEmu 5.5.6+ dGPU E2E 主线解锁）
> **工期**: 2-3 周（基础必备 4 步）+ 1 周（性能增强 1 步）
> **关联 ADR**:
> - [ADR-088](../../docs/00_adr/adr-088-dgpu-complete-simulation.md) ✅ Accepted — dGPU 完整仿真
> - [ADR-091](../../docs/00_adr/adr-091-pci-driver-architecture-and-four-quadrant.md) ✅ Accepted v0.2 — 4 象限
> - ADR-052 ✅ Accepted — PM4 microcode（依赖本 change）
> **前置基线**:
> - CppTLM `cpptlm_emulator.cc` 22 ABI 函数（部分 stub/NO-OP，详见 §1）
> - PCIe EP 子模块骨架存在但功能不完整（详见 §2）
> **下游**: UsrLinuxEmu 5.5.6+ dGPU E2E 主线解锁（command_processor → kernel dispatch → 真机验证）

---

## Why

UsrLinuxEmu 通过 dlopen `libcpptlm_emulator.so` 委托 dGPU 板卡仿真。Oracle 三轮审查（2026-09-09）揭示 **PCIe EP 协同方案存在根本性错误**（PCIe EP 评审 3.5/10 + P5.NEW-X.1 CP attach 评审 3.5/10 + 5.5.8 立项评审 8.7/10）：

| # | 现有能力 | 实际行为 | Oracle 评价 |
|---|---------|---------|------------|
| 1 | `cpptlm_emulator_register_backdoor_cb` | `(void)cb` NO-OP，cb 永不入 DGpuBoard | **假成功** |
| 2 | `cpptlm_emulator_register_dma_translate_cb` | 硬编码 `return 0`（pa=0 ≠ identity） | **假成功** |
| 3 | `cpptlm_emulator_pcie_config_read/write` | `return -ENOSYS`（纯 stub） | **死路** |
| 4 | `cpptlm_emulator_msix_init/update/clear` | intr_cb 真实接线但 `trigger_irq_async` 全仓无调用方 | **中断链断裂** |
| 5 | `cpptlm_emulator_mmio_read` | inject_q_ + 1ms wait_for race；**ret==0 时 buf 未填充** | **数据缺口** |
| 6 | `cpptlm_emulator_backdoor_read` | 未命中 vram_segments_ 返 `len`（如 4）伪装成功 | **语义错位** |
| 7 | `cpptlm_emulator_mmio_write` | inject_q_ 推入后立即返 0；TLP 注入 TODO T-bs-3c 未实现 | **写入假成功** |

**影响**：
- UsrLinuxEmu 5.5.6 dGPU E2E 主线 P0（commit `c263867` + `4bf7508` + `0e300ef`）"✅ ship + Oracle 9.5/10" 实为**接线真实但语义空转**——5 个新测试仅断言 `ret != -ENOSYS`，对数据正确性零覆盖
- UsrLinuxEmu 5.5.7.1 P5.NEW-A（commit `2cf4bc5` + Oracle 9.4/10）profile 验证 5/5 PASS，但 D.1 决策 X（CP attach 消除 -ETIMEDOUT）因果链错位（-ETIMEDOUT 根因是 sim_loop 调度 race，与 callback 注册无关，实测 2000 次 race 率 0.55%）
- UsrLinuxEmu 5.5.8 立项（commit `d4a98f7` + Oracle 8.7/10）阶段 1（CP attach 消除 -ETIMEDOUT）**应直接删除**——实测 cp_attach 根本不调 cb
- **UsrLinuxEmu dGPU E2E 主线 #2 CommandProcessor 无法启动**——依赖的数据通路、中断链路、DMA 翻译均为假成功或死路

**战略调整**（用户 2026-09-09 决策）：
> **先打通 CppTLM PCIe EP 协同流程，再考虑 CommandProcessor。** 在完全实现 PCIe EP 协同的情况下，再启动 5.5.7 dGPU E2E 主线 #2。

## What Changes

本 change 是 **CppTLM dGPU PCIe EP 基础必备能力补完**，按 4 层 PCIe 能力框架 + 5 步开发建议实施。**优先级聚焦"基础必备"层**（阶段一必须），"性能增强"层 P2P 作为 stage 2，"虚拟化必备"与"高级可选"层留待后续。

### 阶段 1 — 基础必备（4 步，2-3 周）

#### 步骤 1.1：PCIe Endpoint 基础

**目标**：让 GPU 作为 PCIe Endpoint 被系统识别和驱动。

- **PCIe 配置空间**：实现标准 Type 0 Configuration Header（Vendor ID / Device ID / Class Code 03h=Display Controller / Revision ID）
  - **修复 #3（pcie_config_read/write -ENOSYS stub）**：在 `dgpu_board_shell.cc` 转发 `board->ep->cfg_space_->read/write`（`PcieConfigSpace` 已实现）
- **BAR 空间**：实现 6 个 BAR
  - BAR0：MMIO 寄存器空间（Doorbell / 状态寄存器 / 固件 Mailbox），64-bit Prefetchable
  - BAR1/BAR2：VRAM 映射空间，64-bit Prefetchable
  - **修复 #5（mmio_read 数据缺口）**：实现 TLP 注入 TODO T-bs-3c，`mmio_read` 时 `sim_loop` drain 后真正 `set_value(0)` 并填充 buf
  - **修复 #7（mmio_write 数据丢弃）**：mmio_write 同步阻塞至 sim_loop drain 完成，**数据真正落地**
- **PCIe Link 管理**：支持 LTSSM 链路训练、链路速率/宽度协商（x16 Gen4/Gen5）、L0/L0s/L1/L2/L3 状态管理
  - 现有 `pcie_link_layer_tlm.cc` 骨架（307 行），需补全状态机

#### 步骤 1.2：MSI-X 中断

**目标**：实现 MSI-X Capability + 中断向量表 + 中断合并。

- **MSI-X Capability**：在 PCIe 配置空间中实现 Extended Capability
- **中断向量表**（PBA / MSI-X Table）：至少 4-8 个向量
  - 命令完成中断
  - 错误/异常中断
  - 固件通信中断
  - 电源管理事件中断
- **修复 #4（中断链断裂）**：
  - `register_callbacks` 5 cb 全部真实接线（不只是 intr/err，reset/power 也需真实存储）
  - **新增**：`pcie_ep.irq_out` → `board->trigger_irq_async` 接线，让 `msix_update_pending` 真正触发 cb
- **中断节流**（Interrupt Coalescing）：避免高频小任务导致中断风暴

#### 步骤 1.3：DMA 引擎

**目标**：实现 PCIe SDMA（System DMA Engine），支持 Scatter-Gather。

- **DMA 控制器**：GPU 内部集成独立 DMA 控制器，直接发起 PCIe TLP
- **DMA 描述符**：支持 Scatter-Gather DMA
- **IOMMU 兼容**：DMA 地址兼容 Intel VT-d / AMD IOMMU 地址翻译
- **修复 #2（dma_translate_cb 硬编码 pa=0）**：
  - CppTLM 侧 `register_dma_translate_cb` 的 lambda 移除 `(void)cb`，真实调用 UsrLinuxEmu 传入的 cb（identity mapping 或 IOMMU 翻译）
  - 适配两套签名：board shell 层 `uint64_t(uint64_t iova, size_t size)` vs SDMA 引擎层 `int(uint64_t iova, uint32_t size, uint64_t& phys)`
- **修复 #6（backdoor_read 返 len 伪装成功）**：
  - backdoor_read 改为：精确匹配返 0 + 填充 buf；不匹配返 -ENOENT 显式 miss 码
  - backdoor 数据落 MemoryTLM 'vram' 真实容量（让 roundtrip 跨段可读）

#### 步骤 1.4：电源管理

**目标**：实现 PCIe PM Capability + D0/D3hot/D3cold + ASPM。

- **PCIe PM Capability**：在 PCIe 配置空间中实现
- **D0/D3hot/D3cold 状态切换**
- **ASPM**（L0s / L1 低功耗链路状态）

### 阶段 2 — 性能增强（1 步，1 周）

#### 步骤 2.1：P2P + Resizable BAR

- **P2P DMA**：GPU 直接通过 PCIe 总线访问其他 PCIe 设备的内存
- **ACS**（Access Control Services）：控制 P2P 流量路由策略
- **Resizable BAR**：CPU 将 GPU 整个帧缓冲映射到 CPU 地址空间，避免分段映射性能损失

### 不在本 change 范围（明确排除）

- **虚拟化必备**（SR-IOV / VF 配置空间 / VF 中断隔离）—— 留待阶段二
- **高级可选**（CXL / NTB / TPH / ATS / PRI / PASID）—— 留待后续阶段
- **CommandProcessor**（用户决策 2026-09-09：必须先 PCIe EP 完整再启动 CP）
- **kernel dispatch / DMA 用户态**—— UsrLinuxEmu 侧，留待 5.5.8+

## Capabilities

### ADDED Requirements

- **`cpptlm-pcie-ep-foundation`**：基础必备 4 步（PCIe EP + MSI-X + DMA + 电源管理）+ 性能增强 1 步（P2P）
- 详见 [`specs/cpptlm-pcie-ep-foundation/spec.md`](specs/cpptlm-pcie-ep-foundation/spec.md)

## Impact

### 下游 UsrLinuxEmu

- **5.5.6+ 重启**：当前 `d4a98f7` 5.5.8 立项 + commit `2cf4bc5` 5.5.7.1 + archived 5.5.6 的"接线真实但语义空转"问题在本 change 完成前**不可启动**
- **5.5.7 CommandProcessor 启动条件**：本 change 完成（PCIe EP 完整）后才能启动——依赖真实数据通路、中断链路、DMA 翻译
- **5.5.8+ kernel dispatch / DMA**：依赖本 change 完成 + UsrLinuxEmu 侧同步实现

### 下游 TaskRunner

- 依赖 D.2 决策 P（TaskRunner 直调 `gpu_hal_ops.adapter_*`），5.5.8 P5.NEW-X.5 启动后
- 5.5.9 真机验证前置：本 change 完成 + UsrLinuxEmu dGPU E2E 端到端稳定

### 边界契约

- **ADR-088 不变**：dGPU 完整仿真边界不变
- **22 ABI 函数签名不变**（only 行为从 stub/NO-OP 变为真实实现）
- **UsrLinuxEmu 端代码** `sim_hardware/src/cpptlm/` 不变（5.5.6 P4.NEW-A/B/C/D + B.5 + 5.5.7.1 接线真实，调用真实化后行为自动生效）

## Alternatives Considered

### A1：只修一个根本错误（如 mmio_read 数据拷贝），其他保留 stub

- **优点**：1 周内可完成
- **缺点**：其他 stub 仍导致 5.5.7 CP 启动失败，单点修复不解决问题
- **结论**：拒绝。基础必备 4 步必须配套修复

### A2：先做 CommandProcessor（绕过 PCIe EP 基础）

- **优点**：用户原始顺序
- **缺点**：CP 依赖数据通路真实 + 中断链路通；当前是假成功，CP 必然失败
- **结论**：拒绝。用户 2026-09-09 决策已明确"先 PCIe EP 完整再 CP"

### A3：把修复拆到 4 个独立 change（per step）

- **优点**：每个 change scope 小
- **缺点**：4 个跨仓协调，commit 数量翻倍
- **结论**：拒绝。本 change 作为单一 4-步 change，commit 按 step 拆分（一个 step 一个 commit）

### A4：升级到 22 ABI 全部真实化（含 lookup_register 签名修正 + 多 handle 引用计数）

- **优点**：彻底解决 22 ABI 所有已知问题
- **缺点**：范围超出"基础必备"，侵入 UsrLinuxEmu 端调用方
- **结论**：部分接受。本 change 包含基础必备 4 步的修复，**UsrLinuxEmu 端修复（lookup_register 签名、多 handle UAF）作为单独 follow-up**

## Success Criteria

- [ ] 基础必备 4 步全部完成：PCIe EP 基础 + MSI-X + DMA + 电源管理
- [ ] 22 ABI 函数中 7 个已知问题全部修复（详见 §Why 表）
- [ ] CppTLM 单元测试覆盖：每个 ABI 函数至少 1 个测试用例 + 数据内容验证
- [ ] CppTLM 集成测试：profile `dgpu_board_v1.json` + `cpptlm_emulator_create` + 4 通路 roundtrip + MSI-X 中断投递 + DMA scatter-gather + 电源状态切换
- [ ] UsrLinuxEmu 5.5.7.1 P5.NEW-A profile 测试从 CppTLM cwd 跑，mmio_read 返回真实数据（不是 buf 原值）
- [ ] Oracle 审查 ≥ 9.0/10
- [ ] UsrLinuxEmu 5.5.7 dGPU E2E 主线 #2 CommandProcessor 启动条件解锁

## Dependencies

### Hard Dependencies

- UsrLinuxEmu `sim_hardware/src/cpptlm/` 现有接线（5.5.6 + 5.5.7.1 ship 代码）—— **不变**
- CppTLM 22 ABI 函数签名（`include/abi/cpptlm_emulator.h`）—— **不变**

### Soft Dependencies

- UsrLinuxEmu 5.5.6/5.5.7.1 测试代码—— 实施后回归验证；断言升级为"数据内容验证"（独立 follow-up）
- UsrLinuxEmu 5.5.8 立项（commit `d4a98f7`）—— 阶段 1 cp_attach 应删除，本 change 不直接修改

## Cross-References

- [proposal.md](../proposal.md) — UsrLinuxEmu 5.5.8 立项（已 commit `d4a98f7`）
- [5.5.6 立项](../archive/2026-09-08-2026-09-08-5-5-6-cpptlm-ep-binding/) — 已 Oracle 9.5/10 归档
- [5.5.7 立项](../2026-09-09-5-5-7-cpptlm-cp-real-ification/) — Oracle CONDITIONAL PASS 9.3/10 + P5.NEW-A 9.4/10
- [5.5.8 立项](../2026-09-09-5-5-8-cpptlm-kernel-dispatch-dma/) — Oracle CONDITIONAL PASS 8.7/10，阶段 1 cp_attach 待删除
- [ADR-088](../../docs/00_adr/adr-088-dgpu-complete-simulation.md) — dGPU 完整仿真
- [ADR-091](../../docs/00_adr/adr-091-pci-driver-architecture-and-four-quadrant.md) — 4 象限
- [ADR-052](../../docs/00_adr/adr-052-pm4-microcode.md) — PM4 microcode（依赖本 change）