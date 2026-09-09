# Tasks: cpptlm-pcie-ep-foundation — dGPU PCIe EP 基础必备能力补完

> **TDD 纪律**: 每个 task 顺序 = Write test → Verify fail → Implement → Verify pass → Commit
> **状态**: 🔄 Proposed v1.0（2026-09-09）
> **前置基线**: CppTLM 现有 22 ABI 函数（7 个根本错误）+ 14 PCIe 头文件骨架 + 10 PCIe .cc 实现
> **工期**: 2-3 周（基础必备 4 步）+ 1 周（性能增强 1 步）
> **关联**: [proposal.md](proposal.md) + [design.md](design.md) + [specs/cpptlm-pcie-ep-foundation/spec.md](specs/cpptlm-pcie-ep-foundation/spec.md)

---

## §1 任务总览

| Wave | 子任务 | 工期 | 依赖 | 优先级 |
|------|--------|:---:|------|:------:|
| **阶段 1.1** | PCIe EP 基础（修复 #3 + #5 + #6 + #7） | 0.5-1 周 | — | P0 |
| **阶段 1.2** | MSI-X 中断（修复 #4） | 0.5 周 | 阶段 1.1 | P0 |
| **阶段 1.3** | DMA 引擎（修复 #2） | 0.5 周 | 阶段 1.2 | P0 |
| **阶段 1.4** | 电源管理 | 0.5 周 | 阶段 1.3 | P0 |
| **阶段 2.1** | P2P + Resizable BAR | 1 周 | 阶段 1.4 | P1 |
| **总计** | | **3-4 周** | | |

**关键路径**: 阶段 1.1 → 1.2 → 1.3 → 1.4 → 2.1

---

## §2 阶段 1.1: PCIe EP 基础

### 任务 1.1.1：建立 CppTLM ABI 单元测试骨架

- [ ] **Write test**: `tests/abi/test_cpptlm_emulator_abi.cc`
  - `TEST_CASE("abi: mmio_read returns real data (not garbage)", "[abi][mmio]")`
  - `TEST_CASE("abi: mmio_write data actually persisted", "[abi][mmio]")`
  - `TEST_CASE("abi: backdoor_read miss returns -ENOENT (not len)", "[abi][backdoor]")`
  - `TEST_CASE("abi: pcie_config_read returns vendor_id 0x10DE", "[abi][config]")`
- [ ] **Verify fail**: 当前实现下：
  - mmio_read 返 0 但 buf 未填充（assert 失败）
  - mmio_write 数据未持久化（assert 失败）
  - backdoor_read miss 返 4（assert 失败）
  - pcie_config_read 返 -ENOSYS（assert 失败）
- [ ] **Implement**: 测试骨架（无修复代码）
- [ ] **Verify pass**: 测试 fail 符合预期（用于驱动 4 步修复）

### 任务 1.1.2：修复 #3（pcie_config_read/write -ENOSYS）

- [ ] **Modify**: `src/tlm/gpu/dgpu_board_shell.cc:151-157`
  - `DGpuBoard::config_read` 转发 `ep_->cfg_space_->read(offset, width, value)`
  - `DGpuBoard::config_write` 转发 `ep_->cfg_space_->write(offset, width, value)`
- [ ] **Verify pass**: test_pcie_config_read 返 0 + value = Vendor ID 0x10DE

### 任务 1.1.3：修复 #5（mmio_read 数据缺口）+ #7（mmio_write 同步阻塞）

- [ ] **Modify**: `src/tlm/gpu/dgpu_board_shell.cc:106-133`
  - `mmio_read` 等待 sim_loop drain 完整 + 真实填充 buf
  - `mmio_write` 同步阻塞至 sim_loop drain 完成 + 数据真正存储
  - 实现 TLP 注入 TODO T-bs-3c
- [ ] **Verify pass**: mmio_read/write 测试用例全 PASS

### 任务 1.1.4：修复 #6（backdoor_read 语义错位）

- [ ] **Modify**: `src/tlm/gpu/dgpu_board_shell.cc:180`
  - backdoor_read 未命中返 -ENOENT（不是 len）
  - backdoor 数据落 MemoryTLM 'vram' 真实容量（让 roundtrip 跨段可读）
- [ ] **Verify pass**: test_backdoor_read_enoent PASS + test_backdoor_read_data_roundtrip PASS

### 任务 1.1.5：PCIe Link 管理（LTSSM + 速率/宽度协商）

- [ ] **Modify**: `src/tlm/pcie/pcie_link_layer_tlm.cc` 补全状态机
- [ ] **Implement**: L0/L0s/L1/L2/L3 状态切换 + x16 Gen4/Gen5 协商
- [ ] **Verify pass**: link_state_machine 单元测试

---

## §3 阶段 1.2: MSI-X 中断

### 任务 1.2.1：修复 #4（中断链断裂）

- [ ] **Modify**: `src/abi/cpptlm_emulator.cc` + `src/tlm/pcie/pcie_endpoint_ip.cc`
  - `register_callbacks` 5 cb 全部真实接线（含 reset/power）
  - 新增 `pcie_ep.irq_out` → `board->trigger_irq_async` 接线
- [ ] **Write test**: `test_msix_update_pending_triggers_intr_cb`
- [ ] **Verify fail**: 当前实现下 cb_called = 0（200ms 后）
- [ ] **Verify pass**: cb_called ≥ 1（msix_update_pending 后 200ms 内）

### 任务 1.2.2：MSI-X Capability + 向量表

- [ ] **Modify**: `src/tlm/pcie/pcie_msix_per_vf_tlm.cc`
  - 实现 MSI-X Extended Capability
  - 至少 4-8 个中断向量
- [ ] **Verify pass**: msix_init + msix_update_pending 真实生效

### 任务 1.2.3：中断节流（Interrupt Coalescing）

- [ ] **Implement**: 基础中断合并机制（避免高频小任务导致中断风暴）
- [ ] **Verify pass**: msix_throttle_test PASS

---

## §4 阶段 1.3: DMA 引擎

### 任务 1.3.1：修复 #2（dma_translate_cb 硬编码 pa=0）

- [ ] **Modify**: `src/abi/cpptlm_emulator.cc:443-460`
  - lambda 内移除 `(void)cb`
  - 真实调用 UsrLinuxEmu cb（签名适配两套：board shell 层 vs SDMA 引擎层）
  - cb 失败返 0 fallback
- [ ] **Write test**: `test_register_dma_translate_cb_returns_iova`（identity mapping）
- [ ] **Verify fail**: 当前 pa=0（identity 是 iova，不是 0）
- [ ] **Verify pass**: cb 真实调用，pa == iova

### 任务 1.3.2：Scatter-Gather DMA 描述符

- [ ] **Modify**: `src/tlm/gpu/sdma_engine_tlm.cc`
  - SDMA Ring Buffer + RPTR/WPTR + Doorbell
  - Scatter-Gather 描述符支持
- [ ] **Verify pass**: sg_dma_test PASS

### 任务 1.3.3：IOMMU 兼容地址翻译

- [ ] **Modify**: `src/tlm/pcie/pcie_endpoint_ip.cc`
  - IOMMU 翻译接口（Intel VT-d / AMD IOMMU）
- [ ] **Verify pass**: iommu_compat_test PASS

---

## §5 阶段 1.4: 电源管理

### 任务 1.4.1：PCIe PM Capability

- [ ] **Modify**: `src/tlm/pcie/pcie_config_space_per_vf_tlm.cc`
  - PM Capabilities / Control / Status 寄存器
- [ ] **Verify pass**: pm_capability_test PASS

### 任务 1.4.2：D0/D3hot/D3cold 状态切换

- [ ] **Modify**: `src/tlm/pcie/pcie_endpoint_ip.cc` + `src/tlm/gpu/dgpu_soc.cc`
  - 状态机实现
- [ ] **Verify pass**: power_state_transition_test PASS

### 任务 1.4.3：ASPM（L0s / L1 低功耗）

- [ ] **Implement**: ASPM 自动协商
- [ ] **Verify pass**: aspm_test PASS

---

## §6 阶段 2.1: P2P + Resizable BAR

### 任务 2.1.1：P2P DMA

- [ ] **Modify**: `src/tlm/pcie/pcie_bypass_mux.cc` + `src/tlm/pcie/pcie_ari_router_tlm.cc`
- [ ] **Verify pass**: p2p_dma_test PASS

### 任务 2.1.2：ACS Capability

- [ ] **Implement**: ACS Capability 寄存器 + 路由策略
- [ ] **Verify pass**: acs_test PASS

### 任务 2.1.3：Resizable BAR

- [ ] **Modify**: `src/tlm/pcie/pcie_config_space_per_vf_tlm.cc`
  - Resizable BAR Capability
- [ ] **Verify pass**: resizable_bar_test PASS

---

## §7 关键路径

**阶段 1.1** (PCIe EP 基础) → **1.2** (MSI-X) → **1.3** (DMA) → **1.4** (电源) → **2.1** (P2P)

**总工时**: 0.5-1 + 0.5 + 0.5 + 0.5 + 1 = **3-4 周**

---

## §8 风险与回退

### 风险 1：mmio_read 修复影响 TLP 注入时序（中）

**症状**：wait_for 超时延长导致 5.5.7.1 profile 测试偶发 -110
**回退**：保留 timeout 但延长到 10ms；cpptlm_sim_loop tick 频率提升

### 风险 2：register_dma_translate_cb 真实调用 cb 触发 UsrLinuxEmu 错误处理（中）

**症状**：UsrLinuxEmu 侧 cb 失败（如未注册 IOMMU）导致 DMA transfer 全部返 0
**回退**：cb 失败时 fallback 返 pa=iova（identity mapping）

### 风险 3：中断链修复后 msix 测试不稳定（中）

**症状**：intr_cb 触发后测试偶发超时
**回退**：测试用 100ms 超时 + retry 1 次

### 风险 4：电源管理状态切换影响 profile 加载（低）

**症状**：profile `dgpu_board_v1.json` 加载时处于 D3 状态导致 mmio_read 失败
**回退**：profile 加载时强制 D0（默认 active 状态）

### 风险 5：跨仓协调——UsrLinuxEmu 端升级断言（低）

**症状**：本 change 完成但 UsrLinuxEmu 5.5.7.1 测试仍是 `CHECK(ret != -ENOSYS)`，未升级为数据内容验证
**回退**：本 change 仅改 CppTLM；UsrLinuxEmu 端独立 follow-up 升级断言

---

## §9 跨引用

- [proposal.md](proposal.md) — Why/What/Capabilities/Impact
- [design.md](design.md) — 技术设计
- [specs/cpptlm-pcie-ep-foundation/spec.md](specs/cpptlm-pcie-ep-foundation/spec.md) — capability 规范
- Oracle 审查三连（PCIe EP 3.5/10 + CP attach 3.5/10 + 5.5.8 8.7/10）
- UsrLinuxEmu 5.5.6+ dGPU E2E 主线解锁（依赖本 change 完成）

---

**任务作者**: CppTLM Architecture Team
**创建日期**: 2026-09-09
**预期完成**: 2026-10-07（3-4 周后）