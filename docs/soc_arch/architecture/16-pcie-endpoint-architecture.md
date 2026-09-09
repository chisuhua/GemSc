# PCIe Endpoint 跨仓架构 (Driver-to-Hardware)

> **目的**: 描述 GPGPU PCIe 能力（4 层框架：基础必备 / 性能增强 / 虚拟化必备 / 高级可选）在 UsrLinuxEmu + CppTLM 跨仓架构中的**数据流**与**控制流**
> **状态**: Draft v0.1 (2026-09-09, 基于 Oracle 三轮审查 + CppTLM 5 步实施框架)
> **范围**: 跨仓 SSOT（CppTLM 硬件侧 + UsrLinuxEmu 驱动侧）
> **关联**:
> - [`docs/roadmap/pcie-ep-cpptlm-collaboration-roadmap.md`](../roadmap/pcie-ep-cpptlm-collaboration-roadmap.md) — 5 步实施 roadmap
> - [`docs/soc_arch/architecture/17-sdma-engine-design.md`](sdma-engine-design.md) — **SDMA 引擎内部设计**（Ring Buffer + RPTR/WPTR + Doorbell + Packet + 状态机 + 地址翻译 + Fence + D2D 路径 + CmdProc 集成；11 章节）
> - [`openspec/changes/2026-09-09-cpptlm-pcie-ep-foundation/specs/cpptlm-pcie-ep-foundation/spec.md`](../../openspec/changes/2026-09-09-cpptlm-pcie-ep-foundation/specs/cpptlm-pcie-ep-foundation/spec.md) — 14 ADDED Requirements
> - UsrLinuxEmu 对应文档: `docs/soc_arch/architecture/16-pcie-endpoint-architecture.md`（驱动侧同步）

---

## §0 范围与术语

### §0.1 4 层 PCIe 能力框架

| 层级 | 状态 | 包含 |
|------|------|------|
| **基础必备**（阶段一必须）| 🎯 **本文档覆盖** | PCIe Endpoint 基础 + MSI-X 中断 + DMA 引擎 + 电源管理 |
| **性能增强**（阶段一后期或阶段二）| 🎯 §3.4 简述 | P2P + Resizable BAR + 原子操作 + 带宽优化 |
| **虚拟化必备**（阶段二/三）| ❌ 排除 | SR-IOV + VF 配置空间 + VF 中断隔离 |
| **高级可选**（后续阶段）| ❌ 排除 | CXL / NTB / TPH / ATS / PRI / PASID |

### §0.2 关键术语

- **PCIe EP** (PCIe Endpoint): GPU 作为 PCIe 设备端
- **BAR** (Base Address Register): MMIO 寄存器 + VRAM 映射空间
- **MSI-X**: PCIe 中断机制
- **TLP** (Transaction Layer Packet): PCIe 总线事务包
- **DMA** (Direct Memory Access): 直接内存访问
- **SDMA** (System DMA): GPU 内部 DMA 引擎
- **CommandBuffer**: 命令缓冲区（含 PM4/AQL 协议）
- **Doorbell**: 通知 GPU 有新命令的寄存器

---

## §1 跨仓整体架构

### §1.1 仓库边界与模块映射

```
┌──────────────────────────────────┐  ┌──────────────────────────────────┐
│  UsrLinuxEmu (driver 侧)          │  │  CppTLM (hardware 仿真侧)      │
│                                  │  │                                  │
│  ┌────────────────────────────┐ │  │ ┌────────────────────────────┐ │
│  │  GpgpuDevice (drv/)        │ │  │ │  PcieEndpointIP              │ │
│  │  • ioctl 派发表            │ │  │ │  • Type 0 Config Header      │ │
│  │  • 38 IOCTL 处理           │ │  │ │  • BAR 路由（6 BAR）         │ │
│  │  • BO/VA/Queue/Fence       │ │  │ │  • MSI-X Capability          │ │
│  └─────────┬──────────────────┘ │  │ │  • PM Capability              │ │
│            ↓                     │  │ └─────────┬────────────────────┘ │
│  ┌────────────────────────────┐ │  │           ↓                       │
│  │  HAL 契约层 (hal/)         │ │  │ ┌────────────────────────────┐ │
│  │  • struct gpu_hal_ops      │ ←┼──┼─→ 22 ABI functions (abi/)      │ │
│  │  • 71 fn-ptrs              │ │  │ │  • cpptlm_emulator_*         │ │
│  │  • hal_user / hal_mock     │ │  │ │  • dlopen libcpptlm_emu.so  │ │
│  │  • hal_cpptlm (3 adapter)  │ │  │ └─────────┬────────────────────┘ │
│  └─────────┬──────────────────┘ │  │           ↓                       │
│            ↓                     │  │ ┌────────────────────────────┐ │
│  ┌────────────────────────────┐ │  │ │  DGpuBoard (tlm/gpu/)        │ │
│  │  CpptlmBridge              │ │  │ │  • dgpu_board_shell.cc       │ │
│  │  • dlopen 22 ABI           │ ←┼──┼─• sim_thread + inject_q_       │ │
│  │  • 4 data path fns         │ │  │ │  • backdoor (vram_segments)  │ │
│  │  • bridge_state()          │ │  │ │  • mmio (T-bs-3c)            │ │
│  └─────────┬──────────────────┘ │  │ └─────────┬────────────────────┘ │
│            ↓                     │  │           ↓                       │
│  ┌────────────────────────────┐ │  │ ┌────────────────────────────┐ │
│  │  BackdoorEndpoint          │ │  │ │  PcieEndpointIP + AXI/SDMA  │ │
│  │  • ule_dgpu_* 5 functions  │ ←┼──┼─• mmio_read/write(emu, ...)  │ │
│  │  • 4-space dispatch       │ │  │ │  • backdoor_read/write      │ │
│  └─────────┬──────────────────┘ │  │ │  • dma_translate_cb          │ │
│            ↓                     │  │ │  • msix_init/update/clear    │ │
│         dlopen                   │  │ │  • pcie_config_read/write    │ │
│  (libcpptlm_emulator.so)        │  │ └────────────────────────────┘ │
└──────────────────────────────────┘  └──────────────────────────────────┘
```

### §1.2 4 层框架 × 模块映射

| 4 层框架能力 | UsrLinuxEmu 模块 | CppTLM 模块 |
|-------------|------------------|-------------|
| **PCIe EP 基础** | `backdoor_endpoint.cpp` (5 fn) + `bridge.cpp` (4 data path) | `PcieEndpointIP` + `DGpuBoard` (cfg/bars/mmio/backdoor) |
| **MSI-X 中断** | `bridge.register_msix_callback` + `host_bridge` (intr 投递) | `pcie_msix_per_vf_tlm.cc` + `PcieEndpointIP.irq_out → trigger_irq_async` |
| **DMA 引擎** | `bridge.register_dma_translate_cb` (via hal_cpptlm) + TaskRunner 直调 adapter | `sdma_engine_tlm.cc` + DGpuBoard.set_dma_translate_callback (真实调用) |
| **电源管理** | GpgpuDevice reset + PM state machine | `pcie_endpoint_ip.cc` (D0/D3 state) + `pcie_config_space_per_vf_tlm.cc` (PM Cap) |
| **P2P (性能)** | （待实现）| `pcie_bypass_mux.cc` + `pcie_ari_router_tlm.cc` |
| **Resizable BAR** | （待实现）| `pcie_config_space_per_vf_tlm.cc` (Resizable BAR Cap) |
| **SR-IOV (排除)**| — | — |
| **VF (排除)** | — | — |

---

## §2 数据流（基础必备 4 能力）

### §2.1 PCIe Endpoint 基础 — BAR 空间数据流

#### BAR 类型与映射

| BAR | 用途 | 大小 | 类型 | 包含 |
|-----|------|------|------|------|
| **BAR0** | MMIO 寄存器空间 | 4KB-64KB | 32/64-bit Prefetchable | Doorbell / 状态寄存器 / 固件 Mailbox |
| **BAR1** | VRAM 映射空间 1 | VRAM size | 64-bit Prefetchable | 帧缓冲（Frame Buffer）|
| **BAR2** | VRAM 映射空间 2 | VRAM size | 64-bit Prefetchable | 帧缓冲扩展（多卡）|
| **扩展 BAR** | Doorbell 专用 | 4KB | 64-bit Prefetchable | 高频低延迟 Doorbell 写入 |

#### 数据流: Driver MMIO Write

```
Driver (UsrLinuxEmu)
  │
  │ 1. GpgpuDevice::ioctl() — e.g., GPU_IOCTL_PUSHBUFFER_SUBMIT_BATCH
  │   args.cmdbuf_addr, args.size
  │
  ▼
  │
  │ 2. hal->cmd_submit(hal, &cmd_args)
  │   struct gpu_hal_ops → hal_user_init / hal_cpptlm_init 后端
  │
  ▼
  │
  │ 3. hal_cpptlm → ule_dgpu_acquire (via backdoor_endpoint.cpp:235)
  │   → cpptlm_emulator_create_by_id(1)
  │   → cpptlm_emulator_open(dev_id, &handle)
  │
  ▼
  │
  │ 4. CpptlmBridge::mmio_write(0, offset, src, len)
  │   bridge.cpp:293 → syms.mmio_write(emu, bar, offset, src, len)
  │
  ▼ (跨仓 dlopen 边界)
  │
┌─────────────────────────────────────────────────────┐
│  CppTLM (hardware)                                    │
│                                                      │
│  5. cpptlm_emulator_mmio_write(emu, 0, offset, src, len)│
│     src/abi/cpptlm_emulator.cc:77 → board->mmio_write │
│                                                      │
│  6. DGpuBoard::mmio_write(bar, offset, src, len)    │
│     src/tlm/gpu/dgpu_board_shell.cc:148              │
│     → inject_q_.push_back(req())                      │
│     → 返回 0（异步，数据待 sim_loop drain）         │
│                                                      │
│  7. sim_loop() tick → drain_injection_queue()       │
│     → pending_resp_[trans_id].set_value(0)           │
│     → BAR 寄存器实际存储 src                          │
└─────────────────────────────────────────────────────┘
```

#### 数据流: Driver MMIO Read

```
Driver MMIO Read (步骤 1-4 类似, 但方向相反)
  │
  ▼
  │ 5. cpptlm_emulator_mmio_read(emu, 0, offset, buf, len)
  │   → board->mmio_read(bar, offset, buf, len)
  │
  ▼
  │ 6. DGpuBoard::mmio_read(bar, offset, buf, len)
  │   → inject_q_.push_back(req())
  │   → wait_for(1ms) 等 sim_loop drain                │
  │   → T-bs-3c 实现后: buf 填充真实 BAR 寄存器数据     │
  │   → 返回 byte-count (≥0) 或 -EINVAL                │
  │
  ▼
  │ 7. syms.mmio_read 返回 → bridge.cpp:285
  │ 8. bridge.mmio_read 返回 → hal_cpptlm → GpgpuDevice
```

**关键约束**：
- MMIO Write 是异步（数据待 sim_loop drain 落地）
- MMIO Read 是同步阻塞（需 wait_for 完成 + 真实数据填充）
- 数据路径必须通过 BAR 路由表（`PcieEndpointIP` 处理 6 BAR）

### §2.2 PCIe Config Space 数据流

```
Driver 读 Vendor ID
  │
  │ 1. hal_cpptlm → ule_dgpu_read(handle, kConfig, offset, &val, 4)
  │   backdoor_endpoint.cpp:320 → syms.pcie_config_read
  │
  ▼
  │ 2. cpptlm_emulator_pcie_config_read(emu, offset, width, &val)
  │   → 修复 #3: 转发 board->ep_->cfg_space_->read(offset, width, &val)
  │
  ▼
  │ 3. PcieConfigSpace::read(offset, width, &val)
  │   → Type 0 Header 字段:
  │     - 0x00: Vendor ID (0x10DE) + Device ID
  │     - 0x08: Revision ID + Class Code (0x03 Display Controller)
  │     - 0x2C: Subsystem Vendor ID + Device ID
  │     - 0x3C: Interrupt Pin + Line
  │
  ▼
  │ 4. 返回 val → driver 读到 Vendor ID 0x10DE
```

### §2.3 MSI-X 中断数据流

```
GPU 内部事件触发（命令完成 / 错误 / 固件 / 电源）
  │
  │ 1. cmd done 事件 → cmd_processor → msix_update_pending(vector=0)
  │   cpptlm_emulator.cc → ep->msix().update_pending(0)
  │   → PBA 置位 (Pending Bit Array)
  │
  ▼
  │ 2. 修复 #4 (关键路径):
  │   pending_irq_out_ 触发 IRQ_DELIVERY TLP
  │   → resp_out[PORT_IRQ_OUT] 发送 TLP
  │   → Host Root Complex 接收 TLP
  │   → board->trigger_irq_async(vector)
  │
  ▼ (跨仓回调)
  │
  │ 3. UsrLinuxEmu 端 intr_cb 被调
  │   bridge.cpp:347 register_msix_callback(intr_cb, ctx)
  │   → bridge_inject_msix_shim(vector) (test seam)
  │   → host_bridge.cpp → Gpgpu_device ISR
  │
  ▼
  │ 4. Driver ISR 处理中断
  │   - read PBA → 识别 vector
  │   - 处理命令完成 / 错误 / 固件通信 / 电源事件
  │   - ack 中断 → msix_clear_pending(vector)
```

**关键约束**：
- intr_cb 必须真实被调（修复 #4）
- trigger_irq_async 必须有调用方（`PcieEndpointIP` IRQ 输出 → board trigger）
- Vector 隔离：每个 vector 独立（命令完成 / 错误 / 固件 / 电源）

### §2.4 DMA 引擎数据流（PCIe SDMA）

#### SDMA Ring Buffer 架构

```
Driver (UsrLinuxEmu)
  │
  │ 1. sdma_submit(src_pa, dst_pa, size)
  │   写入 SDMA Ring Buffer descriptor:
  │     { src_pa, dst_pa, size, flags, completion_vector }
  │
  ▼
  │
  │ 2. update WPTR + doorbell ring
  │   cpptlm_emulator_doorbell_ring(stream_id, value)
  │
  ▼ (跨仓)
  │
┌─────────────────────────────────────────────────────┐
│  CppTLM SDMA 引擎                                    │
│                                                      │
│  3. SDMA 硬件读 RPTR + WPTR                           │
│     → 取出 descriptor                                 │
│     → dma_translate_cb(iova, size, &phys)            │
│       (修复 #2: 真实调用 UsrLinuxEmu cb)             │
│     → 获取 host physical address                      │
│     → PCIe TLP: MRd (read) / MWr (write)             │
│     → 数据直接传输（无需 CPU 中转）                  │
│                                                      │
│  4. 完成 → sdma_complete_cb →                        │
│     msix_update_pending(completion_vector)            │
└─────────────────────────────────────────────────────┘
```

#### IOMMU 地址翻译（修复 #2）

```
SDMA 引擎发起 DMA
  │
  │ 1. board->dma_translate_cb_(iova, size)
  │   → CppTLM lambda 调用 UsrLinuxEmu cb
  │
  ▼
  │
  │ 2. UsrLinuxEmu 注册的 dma_translate_cb
  │   options:
  │   a) Identity mapping: phys = iova
  │   b) IOMMU 翻译: phys = iommu_translate(iova)
  │
  ▼
  │
  │ 3. 返回 phys → SDMA 引擎用 phys 发起 PCIe TLP
```

**关键约束**：
- dma_translate_cb 必须真实调用（修复 #2，移除硬编码 pa=0）
- cb 失败 fallback: phys = iova（identity mapping）
- IOMMU 翻译支持 Intel VT-d / AMD IOMMU

**完整 SDMA 内部设计**（Ring Buffer 协议 / RPTR/WPTR 算法 / Doorbell 协议 / Packet 格式 / 状态机 / 地址翻译链 / 完成通知 / D2D 路径 / CmdProc 集成）见 [`sdma-engine-design.md`](sdma-engine-design.md)。

### §2.5 电源管理数据流

```
Driver 切到 D3（低功耗）
  │
  │ 1. hal_cpptlm → pcie_config_write(emu, PMCSR_offset, 0x3)
  │   PMCSR = 0x3 (D3hot 请求)
  │
  ▼
  │
  │ 2. PcieEndpointIP::set_power_state(D3hot)
  │   → 状态机: D0 → D1 → D3hot
  │   → MMIO 访问禁用（返回 -ENODEV）
  │   → BAR 解映射
  │
  ▼
  │
  │ 3. Driver 唤醒（resume）
  │   → PMCSR = 0x0 (D0 请求)
  │   → 状态机: D3hot → D0
  │   → MMIO 重新启用
  │   → BAR 重新映射
```

---

## §3 控制流（按操作类型）

### §3.1 Driver 控制操作总表

| 操作 | UsrLinuxEmu 入口 | CppTLM 实现 | 数据流 |
|------|-----------------|-------------|--------|
| **设备探测** | `pci_probe_enumerate()` | DGpuBoard 列表 | 静态（编译期） |
| **BAR 映射** | `ioremap(BAR)` | PcieEndpointIP BAR 路由 | Host VA → BAR addr |
| **配置空间读** | `pci_read_config_*` | `cpptlm_emulator_pcie_config_read` | §2.2 |
| **MMIO 读** | `readl(bar + off)` | `cpptlm_emulator_mmio_read` | §2.1 |
| **MMIO 写** | `writel(bar + off, val)` | `cpptlm_emulator_mmio_write` | §2.1 |
| **Doorbell** | `writel(doorbell_reg, val)` | `cpptlm_emulator_doorbell_ring` | RPTR/WPTR 更新 |
| **MSI-X 配置** | `pci_enable_msix()` | `cpptlm_emulator_msix_init` | 中断向量表分配 |
| **DMA 提交** | SDMA ring write | `sdma_engine_tlm` + TLP | §2.4 |
| **DMA 翻译** | cb 注册 | `cpptlm_emulator_register_dma_translate_cb` | §2.4 |
| **中断处理** | ISR | `msix_update_pending` → intr_cb | §2.3 |
| **电源切换** | `pci_set_power_state()` | `cpptlm_emulator_pcie_config_write` (PMCSR) | §2.5 |

### §3.2 关键控制路径: GPGPU 完整计算任务

```
Driver: 提交向量加法 kernel
  │
  │ 1. 分配 VRAM (BO)
  │   ioctl(GPU_IOCTL_ALLOC_BO) → hal->mem_alloc → 分配 VRAM BAR1 区域
  │
  │ 2. DMA 上传输入数据 (Host → GPU)
  │   ioctl(GPU_IOCTL_PUSHBUFFER_SUBMIT_BATCH) → hal->cmd_submit
  │   → sdma_submit(host_va, gpu_va, size)
  │   → SDMA 引擎 PCIe TLP MWr 写 VRAM
  │
  │ 3. 提交计算命令 (CommandBuffer via PM4)
  │   写入 cmd ring → doorbell ring
  │   → CommandProcessor 解析 PM4 → 调度计算引擎
  │
  │ 4. GPU 计算完成 → 中断
  │   msix_update_pending(cmd_done_vector) → intr_cb
  │   → Driver ISR 唤醒 wait_fence 任务
  │
  │ 5. DMA 下载结果 (GPU → Host)
  │   类似步骤 2，方向相反
  │
  │ 6. 释放 VRAM
  │   ioctl(GPU_IOCTL_FREE_BO)
```

### §3.3 性能增强层（阶段 2.1）— 简述

| 能力 | 数据流特点 | UsrLinuxEmu 接入点 |
|------|------------|-------------------|
| **P2P DMA** | GPU ↔ 其他 PCIe 设备直连，绕过 CPU | 需扩 `sdma_submit` 支持跨 BAR 目标地址 |
| **Resizable BAR** | CPU 一次映射整个 VRAM | 需扩 BAR 路由表支持可配置大小 |
| **原子操作** | PCIe AtomicOp TLP | 待实现（阶段二）|
| **带宽优化**（Relaxed Ordering / No Snoop / Extended Tag）| TLP header 字段控制 | 待实现（阶段二）|

---

## §4 关键约束与边界

### §4.1 跨仓 ABI 不变约束

- CppTLM `src/abi/cpptlm_emulator.h` 22 函数签名不变（仅行为从 stub → 真实）
- UsrLinuxEmu `sim_hardware/src/cpptlm/` 接线不变（5.5.6 P4.NEW-A/B/C/D + B.5 + 5.5.7.1 ship 代码）
- 5 步实施仅修改 CppTLM 内部实现（`DGpuBoard` / `PcieEndpointIP` / `sdma_engine_tlm`），不涉及 ABI

### §4.2 时序约束

- MMIO Read: 同步阻塞 ≤1ms（race 需修；`wait_for` 延长 + sim_loop 优化）
- MMIO Write: 异步（sim_loop tick 内 drain）
- DMA 完成: 命令完成后 msix_update_pending（≤100us 内 cb 触发）
- 电源切换: D0 ↔ D3 ≤1ms（状态机内部延迟）

### §4.3 错误处理边界

| 错误码 | 含义 | 触发条件 |
|--------|------|---------|
| `-ENOSYS` | 功能未实现 | stage N+1 待实施（5.5.6 ship 时多为此） |
| `-EINVAL` | 参数错误 | bar ≥ 6 / len > 4096 / NULL 指针 |
| `-ENODEV` | 设备未初始化 | bridge 未 init |
| `-EBUSY` | 设备忙 | 重复 init |
| `-ETIMEDOUT` (-110) | 超时 | mmio_read 1ms wait_for race |

### §4.4 资源边界

- 单设备: dev_id=1（自动分配，可改）
- 多设备: dev_id=2,3,...（`create_by_id` 幂等，重复返同指针 — UAF 风险，需加引用计数）
- BAR 数: 6 个（BAR0-5）+ Resizable BAR 扩展
- MSI-X 向量: 4-8 个（基础必备），可扩展

---

## §5 同步点与里程碑

### §5.1 跨仓同步检查清单

- [ ] 阶段 1.1 完成后：UsrLinuxEmu 端回归测试 `test_bridge_kcpptlm_profile_real_standalone` 4 数据通路全 PASS + data assertion
- [ ] 阶段 1.2 完成后：msix_update_pending 触发 intr_cb（实测 200ms 内 ≥1 次）
- [ ] 阶段 1.3 完成后：dma_translate_cb 真实调用（identity mapping pa == iova）
- [ ] 阶段 1.4 完成后：D0 ↔ D3 状态切换 + MMIO 启用/禁用
- [ ] 阶段 2.1 完成后：P2P DMA + Resizable BAR 配置生效
- [ ] 全 5 步 Oracle 审查 ≥ 9.0/10
- [ ] UsrLinuxEmu 5.5.7 重启

### §5.2 关键里程碑

| 里程碑 | 时间 | 验收 |
|--------|------|------|
| M1: 阶段 1.1 完成 | 0.5-1 周 | cfg space + 4 data path + race 修复 |
| M2: 阶段 1.2 完成 | +0.5 周 | msix intr_cb 真实触发 |
| M3: 阶段 1.3 完成 | +0.5 周 | dma_translate_cb 真实调用 + SDMA scatter-gather |
| M4: 阶段 1.4 完成 | +0.5 周 | D0/D3 切换 + ASPM L0s/L1 |
| M5: 阶段 2.1 完成 | +1 周 | P2P + Resizable BAR |
| M6: UsrLinuxEmu 5.5.7 重启 | +2 周 | CommandProcessor 真实化 |
| M7: UsrLinuxEmu 5.5.8 重启 | +3 周 | kernel dispatch + DMA 真实化 |
| M8: UsrLinuxEmu 5.5.9 启动 | +4 周 | 真机双轨验证 |

---

## §6 修订记录

- **v0.1** (2026-09-09, Draft): 初版,基于 Oracle 三轮审查 + 用户战略调整
  - §1 跨仓整体架构 + 4 层框架 × 模块映射
  - §2 数据流（基础必备 4 能力: BAR/Config/MSI-X/DMA/PM）
  - §3 控制流（按操作类型 + 关键路径）
  - §4 关键约束与边界（ABI / 时序 / 错误 / 资源）
  - §5 同步点与里程碑（8 个 M1-M8）
- **待 P3-P4**: 根据实施进度追加（特别是阶段 1.3 后补充 IOMMU 翻译细节）