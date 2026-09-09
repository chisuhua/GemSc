# Design: cpptlm-pcie-ep-foundation

> **关联**: [proposal.md](proposal.md) · [tasks.md](tasks.md) · [specs/cpptlm-pcie-ep-foundation/spec.md](specs/cpptlm-pcie-ep-foundation/spec.md)
> **前置**: Oracle 三轮审查揭示的 7 个根本性错误（PCIe EP 评审 3.5/10 + CP attach 评审 3.5/10 + 5.5.8 评审 8.7/10）

---

## §1 现状盘点（CppTLM 现有 PCIe 能力）

### §1.1 文件清单

| 目录 | 文件数 | 总行数 | 备注 |
|------|:------:|:------:|------|
| `include/tlm/pcie/` | 14 | ~1900 | 头文件骨架完整 |
| `src/tlm/pcie/` | 10 | ~2600 | .cc 实现 |
| `include/tlm/gpu/` | 6 | ~700 | GPU 内部 |
| `src/tlm/gpu/` | 9 | ~1900 | dGPU board shell + soc + SDMA + CP MVP |
| `src/abi/cpptlm_emulator.cc` | 1 | 526 | 22 ABI 函数实现 |
| **合计** | **40** | **~7600** | |

### §1.2 7 个根本错误定位

| # | 错误 | 文件:行号 | 实施内容 |
|---|------|-----------|---------|
| 1 | `register_backdoor_cb` NO-OP | `src/abi/cpptlm_emulator.cc:434-440` | `(void)cb; return 0;` → 真实存储 cb + 真实调用 |
| 2 | `register_dma_translate_cb` 硬编码 pa=0 | `src/abi/cpptlm_emulator.cc:443-460` | lambda 内 `(void)cb; return 0;` → 真实调用 UsrLinuxEmu cb |
| 3 | `pcie_config_read/write` -ENOSYS stub | `src/tlm/gpu/dgpu_board_shell.cc:151-157` | 转发 `board->ep->cfg_space_->read/write`（PcieConfigSpace 已实现） |
| 4 | `msix_update_pending` 中断链断裂 | `src/tlm/gpu/dgpu_board_shell.cc` + `src/tlm/pcie/pcie_endpoint_ip.cc` | `pcie_ep.irq_out` → `board->trigger_irq_async` 接线 |
| 5 | `mmio_read` 数据缺口 + race | `src/tlm/gpu/dgpu_board_shell.cc:106-133` | TLP 注入 TODO T-bs-3c 实现 + race 修复 |
| 6 | `backdoor_read` 返 len 伪装成功 | `src/tlm/gpu/dgpu_board_shell.cc:180` | 精确匹配返 0 + 填充 buf；不匹配返 -ENOENT |
| 7 | `mmio_write` 数据丢弃 | `src/tlm/gpu/dgpu_board_shell.cc` | 同步阻塞至 sim_loop drain 完成 |

### §1.3 测试覆盖现状

- CppTLM 仓内**无单元测试覆盖 ABI 层**（22 函数无测试）
- UsrLinuxEmu 端 5 个新测试 binary 仅断言 `ret != -ENOSYS`（不覆盖数据正确性）
- 真实集成测试依赖 `dgpu_board_v1.json` profile + CppTLM cwd

---

## §2 设计目标

按 4 层 PCIe 能力框架 + 5 步开发建议，**优先级聚焦基础必备**（阶段一必须），明确排除虚拟化必备与高级可选层。

| 层级 | 状态 | 步骤 | 工期 |
|------|------|:----:|:----:|
| 基础必备 | **本 change 实施** | 4 步 | 2-3 周 |
| 性能增强 | 本 change 阶段 2 | 1 步（P2P） | 1 周 |
| 虚拟化必备 | **本 change 排除** | SR-IOV + VF 配置 + VF 中断隔离 | 后续 |
| 高级可选 | **本 change 排除** | CXL / NTB / TPH / ATS / PRI / PASID | 后续 |

---

## §3 基础必备 4 步架构

### §3.1 步骤 1.1：PCIe Endpoint 基础

**修改文件**：
- `src/tlm/gpu/dgpu_board_shell.cc` — 修复 #3 + #6 + #7
- `src/tlm/pcie/pcie_endpoint_ip.cc` — 接线 `cfg_space_` 实现
- `src/tlm/pcie/pcie_link_layer_tlm.cc` — 补全 LTSSM + 速率/宽度协商 + 电源状态

**修复 #3（pcie_config_read/write）**：
```cpp
// Before (stub)
int DGpuBoard::config_read(uint16_t offset, uint8_t width, uint32_t* value) {
    return -ENOSYS;  // ← Oracle: 死路
}

// After
int DGpuBoard::config_read(uint16_t offset, uint8_t width, uint32_t* value) {
    return ep_->cfg_space_->read(offset, width, value);
}
```

**修复 #5（mmio_read 数据缺口）+ #7（mmio_write 同步阻塞）**：
```cpp
// Before (race + 数据缺口)
int DGpuBoard::mmio_read(uint8_t bar, uint64_t offset, void* buf, size_t len) {
    inject_q_.push_back(std::move(req));
    auto status = pending_resp_[req.trans_id].wait_for(1ms);
    if (status != std::future_status::ready) return -110;  // ← race
    // ← buf 未填充（T-bs-3c 未实现）
    return 0;
}

// After
int DGpuBoard::mmio_read(uint8_t bar, uint64_t offset, void* buf, size_t len) {
    inject_q_.push_back(std::move(req));
    // 等待 sim_loop drain 完整（包括数据填充）
    auto status = pending_resp_[req.trans_id].wait_for(WAIT_TIMEOUT_MS);
    if (status != std::future_status::ready) return -110;
    auto data = pending_resp_[req.trans_id].get();  // 真实数据
    std::memcpy(buf, data.data(), std::min(len, data.size()));
    return static_cast<int>(std::min(len, data.size()));  // byte-count convention
}
```

**修复 #6（backdoor_read 语义错位）**：
```cpp
// Before (未命中返 len 伪装成功)
int DGpuBoard::backdoor_read(uint8_t bar, uint64_t offset, void* buf, size_t len) {
    auto it = vram_segments_[bar].find(offset);
    if (it == vram_segments_[bar].end() || it->second.size() != len) {
        return static_cast<int>(len);  // ← Oracle: 伪装成功
    }
    std::memcpy(buf, it->second.data(), len);
    return 0;
}

// After
int DGpuBoard::backdoor_read(uint8_t bar, uint64_t offset, void* buf, size_t len) {
    auto it = vram_segments_[bar].find(offset);
    if (it == vram_segments_[bar].end()) {
        return -ENOENT;  // 显式 miss
    }
    if (it->second.size() < len) {
        return -EINVAL;  // 长度不足
    }
    std::memcpy(buf, it->second.data(), len);
    return 0;  // 真实成功
}
```

### §3.2 步骤 1.2：MSI-X 中断

**修改文件**：
- `src/abi/cpptlm_emulator.cc` — 修复 #4
- `src/tlm/pcie/pcie_endpoint_ip.cc` — 接线 `trigger_irq_async`
- `src/tlm/pcie/pcie_msix_per_vf_tlm.cc` — 补全 4-8 向量

**修复 #4（中断链断裂）**：
```cpp
// Before (cb 不入 DGpuBoard)
int cpptlm_emulator_register_callbacks(emu, intr_cb, err_cb, reset_cb, power_cb, ctx) {
    // 注册 5 cb 但无 trigger 路径
}

// After
int cpptlm_emulator_register_callbacks(emu, intr_cb, err_cb, reset_cb, power_cb, ctx) {
    emu->board->set_irq_callback(intr_cb);  // ← Oracle: 已真实接线，需补 trigger 路径
    emu->board->set_error_callback(err_cb);
    emu->board->set_reset_callback(reset_cb);  // 新增：存储
    emu->board->set_power_callback(power_cb);  // 新增：存储
    // 新增：pcie_ep.irq_out → board->trigger_irq_async 接线
    emu->board->ep_->set_irq_delegate([emu](uint32_t vector) {
        emu->board->trigger_irq_async(vector);  // ← 修复中断链
    });
    return 0;
}
```

### §3.3 步骤 1.3：DMA 引擎（**已细分 4 子阶段**）

> **重要修订**：Oracle 2026-09-09 审查指出，原"阶段 1.3 = 0.5 周"严重低估（现有 `sdma_engine_tlm.cc` 是 descriptor 直投而非 Ring Buffer 架构，需"补架构"而非"修 bug"）。按 SDMA 内部设计（[`docs/02_architecture/sdma-engine-design.md`](../../docs/02_architecture/sdma-engine-design.md) 11 章节）拆分如下：

| 子阶段 | 内容 | 工期 | 对应 SDMA 设计章节 |
|--------|------|:---:|------------------|
| **1.3a** | PCIe SDMA 基础: Ring Buffer + RPTR/WPTR + Doorbell 绑定 + SG 描述符扩展 | 1 周 | §2-§6（类 + Ring + RPTR/WPTR + Doorbell + Packet）|
| **1.3b** | D2D SDMA 路径: `DmaDescriptor::Dir::D2D` 扩展 + NoC 数据面（GpuMeshNoC 从延迟模型扩为 payload 转发）+ 显存控制器 bypass | 0.5-1 周 | §10（D2D 路径）|
| **1.3c** | dma_translate_cb 真实化（#2）+ GART/IOMMU 4 级翻译链（identity + IOMMU 两模式）+ CP→SDMA DMA 转发（PM4 opcode 0x4600-0x4900 映射）| 0.5 周 | §8（地址翻译）+ §11（CmdProc 集成）|
| **1.3d** | SDMA 完成通知: Fence 命令（Ring 内）+ done_out→CompletionRing→MSI-X 接线（#4）| 0.5 周 | §9（完成通知）|
| **总计** | | **2.5-3 周**（原 0.5 周）| |

**修改文件**（累计）：
- `src/abi/cpptlm_emulator.cc` — 修复 #2（阶段 1.3c）
- `src/tlm/gpu/sdma_engine_tlm.cc` — Ring Buffer + RPTR/WPTR + Doorbell + SG + D2D + Fence（阶段 1.3a/b/d）
- `src/tlm/pcie/pcie_endpoint_ip.cc` — IOMMU 兼容地址翻译接口（阶段 1.3c）
- `src/tlm/gpu/command_processor_mvp.cc` — DISPATCH 态 dma_req 分支（阶段 1.3c）
- `src/tlm/gpu/dma_descriptor_mvp.hh` + `dma_bundles_tlm.hh` — Dir::D2D + SG 扩展（阶段 1.3a）
- 新建：`src/tlm/gpu/sdma_ring_buffer.h/cc` + `sdma_packet.h/cc` + `d2d_noc_path.h/cc`（阶段 1.3a/b）

**修复 #2（dma_translate_cb 硬编码 pa=0）**：
```cpp
// Before (lambda 内忽略 cb)
board->set_dma_translate_callback([cb](uint64_t iova, size_t size) -> uint64_t {
    (void)cb; (void)iova; (void)size;
    return 0;  // ← Oracle: pa=0 假成功
});

// After (真实调用 UsrLinuxEmu cb)
board->set_dma_translate_callback([cb](uint64_t iova, size_t size) -> uint64_t {
    uint64_t pa = 0;
    int ret = reinterpret_cast<int(*)(uint64_t, uint32_t, uint64_t*)>(cb)(iova, size, &pa);
    return ret == 0 ? pa : 0;  // cb 失败返 0（fallback）
});
```

**完整设计参考**：[`docs/02_architecture/sdma-engine-design.md`](../../docs/02_architecture/sdma-engine-design.md)（11 章节）
- §2 类定义（5 端口冻结 + RingBuffer/RPTR/WPTR/Doorbell/SG 新增）
- §3 Ring Buffer 结构（容量/对齐/物理布局）
- §4 RPTR/WPTR 算法（满/空判定 + race 防护 + memory ordering）
- §5 Doorbell 协议（BAR0+0x10010000 + 强序 + Doorbell 类绑定）
- §6 Packet 格式（Header opcode+count + Payload + D2D/H2D/D2H 三向编码 + SG chain）
- §7 状态机（IDLE→FETCH→DECODE→EXECUTE→COMPLETE 与 CP FSM 对齐）
- §8 地址翻译链（4 级 + identity/IOMMU 两模式 + GART 集成）
- §9 完成通知（MSI-X + Fence 双轨 + Fence Table 内存映射）
- §10 D2D 路径（NoC 数据面扩展 + 显存控制器 bypass）
- §11 CmdProc 集成（PM4 DMA opcode 0x4600-0x4900 映射 + 双轨过渡）

### §3.4 步骤 1.4：电源管理

**修改文件**：
- `src/tlm/pcie/pcie_config_space_per_vf_tlm.cc` — PM Capability
- `src/tlm/pcie/pcie_endpoint_ip.cc` — D0/D3hot/D3cold + ASPM
- `src/tlm/gpu/dgpu_soc.cc` — 电源状态切换逻辑

**实现要点**：
- PCIe PM Capability 寄存器（Power Management Capabilities / Control / Status）
- D0（active）/ D1（低功耗）/ D2（更深低功耗）/ D3hot / D3cold 状态机
- ASPM L0s（短期低功耗）/ L1（长期低功耗）自动协商

---

## §4 性能增强 1 步：P2P + Resizable BAR

**修改文件**：
- `src/tlm/pcie/pcie_ari_router_tlm.cc` — ARI（Alternative Routing-ID Interpretation）
- `src/tlm/pcie/pcie_bypass_mux.cc` — P2P 路由
- `src/tlm/pcie/pcie_config_space_per_vf_tlm.cc` — Resizable BAR Capability

**实现要点**：
- P2P DMA：GPU 通过 PCIe 总线直接访问其他 PCIe 设备
- ACS Capability：控制 P2P 路由策略
- Resizable BAR Capability：CPU 完整映射 GPU VRAM

---

## §5 测试设计

### §5.1 CppTLM 端单元测试（新建）

**位置**：`tests/abi/test_cpptlm_emulator_abi.cc`

| 测试 | 验证 |
|------|------|
| test_mmio_read_real_data | buf 被真实填充（不是原值） |
| test_mmio_write_sync_block | 数据真正落地 |
| test_backdoor_read_enoent | 未命中返 -ENOENT（不是 len） |
| test_backdoor_read_data_roundtrip | roundtrip 数据一致 |
| test_pcie_config_read_vendor_id | Vendor ID = 0x10DE |
| test_msix_update_pending_triggers_intr_cb | cb 真正被调 |
| test_register_backdoor_cb_actually_invoked | cb 真实存储+调用 |
| test_register_dma_translate_cb_returns_iova | identity mapping |
| test_power_state_d0_to_d3 | 状态切换 |

### §5.2 CppTLM 集成测试（新建）

**位置**：`tests/integration/test_dgpu_pcie_ep.cc`

- 完整链路：profile `dgpu_board_v1.json` + create + 4 通路 roundtrip + MSI-X 中断投递 + DMA scatter-gather + 电源状态切换

### §5.3 UsrLinuxEmu 端回归

**位置**：`tests/sim_hardware/test_bridge_kcpptlm_profile_real_standalone.cpp`（已有，5.5.7.1 ship）

**升级断言**：
- `CHECK(ret != -ENOSYS)` → `REQUIRE(ret == 0)` + **buf 内容断言**（Oracle 建议）
- mmio_read 写入 0xDEADBEEF → 读回相等
- backdoor_read 写 0xCAFEBABE → 读回相等

---

## §6 风险评估

| 风险 | 等级 | 缓解 |
|------|:----:|------|
| 步骤 5 修复 mmio_read race 影响 TLP 注入时序 | 中 | profile `dgpu_board_v1.json` + 现有 5.5.7.1 测试验证 |
| register_dma_translate_cb 真实调用 cb 触发 UsrLinuxEmu 侧错误处理 | 中 | cb 失败返 0 fallback + UsrLinuxEmu 端 retry 包装 |
| 中断链修复后 5.5.7.1 profile 测试 `mmio_read` 仍可能偶发 -110 | 低 | wait_for 超时延长 + ret==0 时 buf 已填充 |
| 电源管理状态切换影响 profile 加载 | 低 | profile 加载时强制 D0 |
| 跨仓协调（UsrLinuxEmu 端升级断言） | 低 | 本 change 仅改 CppTLM，UsrLinuxEmu 端独立 follow-up |

---

## §7 跨引用

- [proposal.md](proposal.md) — Why/What/Capabilities/Impact
- [tasks.md](tasks.md) — TDD 5 步结构
- [specs/cpptlm-pcie-ep-foundation/spec.md](specs/cpptlm-pcie-ep-foundation/spec.md) — capability 规范
- Oracle 审查三连（PCIe EP 3.5/10 + CP attach 3.5/10 + 5.5.8 8.7/10）
- UsrLinuxEmu 5.5.6+ dGPU E2E 主线解锁（依赖本 change 完成）
- [ADR-088](../../docs/00_adr/adr-088-dgpu-complete-simulation.md) — dGPU 完整仿真边界
- [ADR-091](../../docs/00_adr/adr-091-pci-driver-architecture-and-four-quadrant.md) — 4 象限
- [ADR-052](../../docs/00_adr/adr-052-pm4-microcode.md) — PM4 microcode（依赖）