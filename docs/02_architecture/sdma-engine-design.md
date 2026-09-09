# SDMA 引擎设计 (System DMA Engine)

> **目的**: 补全 CppTLM dGPU SDMA 引擎的**内部设计**（Ring Buffer + RPTR/WPTR + Doorbell + Packet + 状态机 + 地址翻译 + 完成通知 + D2D 路径 + CmdProc 集成），解决现有 `sdma_engine_tlm.cc`（"descriptor 直投"）与 openspec/specs/sdma-engine-tlm/spec.md + pcie-ep-cpptlm-collaboration-roadmap.md §阶段 1.3（"Ring Buffer + RPTR/WPTR + Doorbell"）之间的 **架构 gap**
> **状态**: Draft v0.1 (2026-09-09)
> **审计**: Oracle 审查 CONDITIONAL (4.5/10) → 本文档完成后预期 7.5/10
> **关联**:
> - [`pcie-endpoint-architecture.md`](pcie-endpoint-architecture.md) — PCIe EP 跨仓架构 SSOT（§3 SDMA 摘要）
> - [`openspec/changes/2026-09-09-cpptlm-pcie-ep-foundation/`](../../openspec/changes/2026-09-09-cpptlm-pcie-ep-foundation/) — 14 ADDED Requirements
> - [`openspec/specs/sdma-engine-tlm/spec.md`](../../openspec/specs/sdma-engine-tlm/spec.md) — 组件 spec

---

## §1 概述

### §1.1 三层 DMA 架构（CommandBuffer / SDMA / CP）

```
┌─────────────────────────────────────────────────────┐
│                    驱动层（软件）                      │
│                                                     │
│  ┌───────────────┐     ┌───────────────────────┐    │
│  │ CommandBuffer  │     │ SDMA Ring Buffer      │    │
│  │ (PM4/AQL)     │     │ (DMA 描述符 + Fence)   │    │
│  │               │     │                       │    │
│  │ 包含：         │     │ 包含：                 │    │
│  │ - DMA 指令包   │     │ - Copy Packet (D2D/   │    │
│  │ - 计算指令包   │     │   H2D/D2H)            │    │
│  │ - 同步指令包   │     │ - SG 描述符链           │    │
│  │ - Fence 指令   │     │ - Fence 命令           │    │
│  └───────┬───────┘     └───────────┬───────────┘    │
│          │ Doorbell                │ Doorbell       │
└──────────┼─────────────────────────┼────────────────┘
           ▼                         ▼
┌──────────────────┐     ┌───────────────────────┐
│ Command Processor │     │ SDMA 引擎（硬件）      │
│ (CP, 翻译官)      │     │ (执行层)                │
│                  │     │                       │
│ - Fetch PM4 指令  │     │ - Ring Buffer 读取    │
│ - Decode (DMA?)  │     │ - RPTR/WPTR 维护      │
│ - Dispatch       │     │ - 地址翻译 (GART/IOMMU)│
│   - DMA → SDMA   │     │ - PCIe TLP 发起       │
│   - Compute→TMU  │     │ - D2D: NoC bypass    │
│   - Fence → SDMA  │     │ - 完成通知 (MSI-X/Fence)│
└────────┬─────────┘     └───────────┬───────────┘
         │ dma_req[2]               │
         └─────────────────────────┘
```

**关系**：
- **CommandBuffer** 是"调度层"：驱动构造命令序列（PM4/AQL），通过 Doorbell 通知 CP
- **CP** 是"翻译官"：解析 CommandBuffer 中命令包，识别 DMA 类 → 构造 SDMA Descriptor → 转发到 SDMA Ring Buffer
- **SDMA** 是"执行层"：从 Ring Buffer Fetch Descriptor → 地址翻译 → 发起 PCIe TLP（H2D/D2H）或 NoC 传输（D2D）

### §1.2 与 5 端口 wire-format 关系

| 5 端口索引 | 含义 | SDMA Ring Buffer 角色 |
|-----------|------|---------------------|
| 0 = desc_in | Descriptor 入队（CP→SDMA） | **Ring Buffer WPTR 推进** |
| 1 = mem_in | 显存访问（SDMA 读 VRAM） | Ring Buffer EXECUTE 数据源 |
| 2 = mem_out | 显存访问（SDMA 写 VRAM） | Ring Buffer EXECUTE 数据汇 |
| 3 = host_out | PCIe TLP 出（SDMA→Host） | Ring Buffer EXECUTE 跨仓通道 |
| 4 = done_out | 完成通知（SDMA→CompletionRing） | **Ring Buffer RPTR 推进** |

**关键约束**：5 端口 wire-format **冻结不动**（参见 `sdma_engine_tlm.cc` §设计 §2.5 Port index ordering lock）。Ring Buffer 作为 `desc_in` 端口**前端的描述符来源**，由 SDMA 引擎内部负责 Ring→descriptor 转换。

### §1.3 与双层 DMA 数据路径对应

| 传输类型 | 数据路径 | 是否经过 PCIe | SDMA 操作 |
|---------|---------|--------------|----------|
| **H2D** (Host→Device) | SDMA → GPU Memory Controller → PCIe Controller → Host | ✅ 是 | `process_h2d()` + PCIe TLP MWr |
| **D2H** (Device→Host) | SDMA → PCIe Controller → Host | ✅ 是 | `process_d2h()` + PCIe TLP MRd |
| **D2D** (Device↔Device, 同 GPU) | SDMA → GPU NoC → 显存控制器 → VRAM | ❌ 否 | `process_d2d()` + NoC bypass |
| **P2P** (GPU↔GPU, 跨 PCIe) | SDMA → PCIe P2P → 对端 GPU | ✅ 是（PCIe P2P） | 阶段 2.1 |

---

## §2 类定义

### §2.1 扩展后的 `SdmaEngineTLM` 类

```cpp
// sdma_engine_tlm.hh 新增成员（基于现有 421 行 .cc）
class SdmaEngineTLM : public sc_core::sc_module {
public:
  // === 现有 5 端口（冻结）===
  tlm_utils::simple_target_socket<SdmaEngineTLM> desc_in;   // [0]
  tlm_utils::simple_target_socket<SdmaEngineTLM> mem_in;    // [1]
  tlm_utils::simple_target_socket<SdmaEngineTLM> mem_out;   // [2]
  tlm_utils::simple_target_socket<SdmaEngineTLM> host_out;  // [3]
  tlm_utils::simple_initiator_socket<SdmaEngineTLM> done_out; // [4]

  SC_HAS_PROCESS(SdmaEngineTLM);
  SdmaEngineTLM(sc_core::sc_module_name nm, const SdmaConfig& cfg);

  // === 现有 4 个注入点（保留）===
  void set_translate_cb(DmaTranslateCb cb);    // 保留（#2 修复后真实调用）
  void set_error_cb(SdmaErrorCb cb);
  void set_host_backdoor(HostBackdoorAccessor cb); // D2H backdoor
  void set_vram_backdoor(VramBackdoorAccessor cb); // D2D backdoor

  // === 新增 Ring Buffer 接口（阶段 1.3a）===
  uint32_t read_wptr() const;   // Doorbell→CP 读取
  uint32_t read_rptr() const;
  void write_rptr(uint32_t r);  // CP / SDMA 推进 RPTR（同步原语）
  bool push_descriptor(const DmaDescriptor& d); // 替代 desc_in 直投
  bool push_sg_descriptor(const SgDmaDescriptor& sg); // SG 链（阶段 1.3a）
  bool push_fence(uint64_t fence_id); // Fence 命令（阶段 1.3d）

private:
  // === 新增 Ring Buffer 成员 ===
  std::unique_ptr<SdmaRingBuffer> ring_;        // §3 Ring Buffer 结构
  std::atomic<uint32_t> wptr_;                  // WPTR（驱动写，SDMA 读）
  std::atomic<uint32_t> rptr_;                  // RPTR（SDMA 推进）
  std::queue<InflightDesc> inflight_;           // 真实异步队列（非 MVP 占位）

  // === 新增状态机成员 ===
  SdmaState state_;                             // §7 状态机
  sc_core::sc_event fetch_event_;
  sc_core::sc_event decode_event_;
  sc_core::sc_event exec_event_;
  sc_core::sc_event complete_event_;

  // === 新增 D2D 路径成员 ===
  std::unique_ptr<D2DNocPath> d2d_noc_;         // §10 D2D NoC 路径
  VramBackdoorAccessor vram_backdoor_;          // D2D 直接 VRAM 访问

  // === 新增地址翻译成员 ===
  std::unique_ptr<GartTable> gart_;             // §8 GART 页表（identity + IOMMU 翻译）
  uint64_t vram_aperture_base_;                 // VRAM aperture 起始
  uint64_t vram_aperture_size_;                 // VRAM aperture 大小

  // === 新增完成通知成员 ===
  std::unique_ptr<FenceTable> fence_table_;     // §9 Fence 内存映射表
  std::queue<PendingFence> pending_fences_;     // 待触发 Fence
};
```

### §2.2 新增数据结构

```cpp
// sdma_ring_buffer.h
struct SdmaRingEntry {
  uint32_t header;        // [31:24] opcode (COPY/FENCE/SG), [23:0] count
  uint64_t src_addr;       // 源地址（GPU 侧可解析：GART MC address）
  uint64_t dst_addr;       // 目标地址（同上）
  uint32_t transfer_size;  // 传输字节数（对齐 4/8/16 字节）
  uint32_t flags;          // [31] FENCE, [30] SG, [29] INTERRUPT_ON_COMPLETE, [28] CHAIN
  uint64_t fence_id;       // Fence ID（flags[FENCE]=1 时有效）
  uint64_t chain_addr;     // SG 链下一项地址（flags[CHAIN]=1 时有效）
};
static_assert(sizeof(SdmaRingEntry) == 32 || sizeof(SdmaRingEntry) == 64,
              "Ring entry must be 32B (basic) or 64B (with chain/fence)");

// SG 描述符扩展（flags[SG]=1 时）
struct SgDmaDescriptor {
  uint32_t n_entries;
  std::array<SdmaRingEntry, MAX_SG_ENTRIES> entries; // MAX_SG_ENTRIES = 8 (典型)
};

// DmaDescriptor 扩展 Dir::D2D
enum class Dir : uint8_t { H2D = 0, D2H = 1, D2D = 2 };
```

---

## §3 Ring Buffer 结构

### §3.1 环形数组布局

```
┌─────────────────────────────────────────────────────┐
│              SDMA Ring Buffer (HW 视图)                │
│                                                      │
│  大小: 4KB / 8KB / 16KB / 64KB（cfg.ring_size 选择）│
│  对齐: 64 字节 cache line                              │
│  条目: sizeof(SdmaRingEntry) = 32B（basic）/ 64B（含SG）│
│  容量: ring_size / entry_size（4KB/32B = 128 条目）  │
│                                                      │
│  ┌─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐  │
│  │ E0  │ E1  │ E2  │ E3  │ E4  │ E5  │ E6  │ E7  │  │
│  └─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┘  │
│      ↑ RPTR (hardware reads from here)                │
│                                                      │
│  ... 中间条目 ...                                      │
│                                                      │
│  ┌─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐  │
│  │ En-8│ En-7│ En-6│ En-5│ En-4│ En-3│ En-2│ En-1│  │
│  └─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┘  │
│                                      ↑ WPTR (driver writes here)│
└─────────────────────────────────────────────────────┘
```

### §3.2 容量与对齐

| cfg.ring_size | entry_size | 条目数 | 典型场景 |
|---------------|-------------|--------|---------|
| 4 KB | 32 B | 128 | SDMA 轻载 |
| 8 KB | 32 B | 256 | 典型 H2D/D2H |
| 16 KB | 32 B | 512 | SDMA 重载 |
| 64 KB | 64 B | 1024 | D2D SG 链 |

**对齐约束**：
- RPTR / WPTR 必须按 `entry_size` 对齐（避免跨条目读写）
- Doorbell 写入的 wptr_offset 也需对齐

### §3.3 物理内存布局

```
BAR1 (VRAM Aperture) 偏移 0x10000000 起:
  0x10000000: SDMA Ring Buffer Base (4 KB / 8 KB / 16 KB / 64 KB)
  0x10010000: SDMA Doorbell Register (4 B) - 仅 WPTR 写入触发
  0x10010004: SDMA RPTR Register (4 B) - 只读（SDMA 暴露给 driver）
  0x10010008: SDMA Config Register (4 B) - ring_size / enable / reset
  0x10010010: SDMA Status Register (4 B) - idle/busy/error/fence_pending
```

**注意**：Ring Buffer 物理地址由 driver 分配 BAR1 VRAM 区域时指定（`gpu_hal_ops.mem_alloc_vram` 未来 API）。

---

## §4 RPTR/WPTR 算法

### §4.1 双指针语义

| 指针 | 全称 | 谁推进 | 含义 |
|------|------|--------|------|
| **WPTR** | Write Pointer | **驱动（CPU）** | 驱动已经写入命令到哪 |
| **RPTR** | Read Pointer | **SDMA（硬件）** | SDMA 已经执行到哪 |

**关键不变式**：
- `RPTR == WPTR` → 队列空（无待执行命令）
- `WPTR > RPTR`（按模 ring_size）→ 有 `(WPTR - RPTR)` 条待执行命令
- `WPTR == RPTR - 1`（模 ring_size）→ 队列满，驱动需等待

### §4.2 WPTR 推进算法（驱动侧）

```
driver_push_descriptor(d):
  1. 计算 entry_count = 1 (basic) or d.sg_entries.size() (SG)
  2. 读取 RPTR (atomic load)
  3. 检查 (WPTR + entry_count) mod ring_size != RPTR - 1 (mod ring_size)
     - 若满：等待 WPTR 信号，spinlock 或 futex
     - 若可写：写入条目到 ring[WPTR..WPTR+entry_count]
  4. atomic store wptr_ = (WPTR + entry_count) mod ring_size
  5. 写 Doorbell 寄存器触发 SDMA fetch
```

### §4.3 RPTR 推进算法（SDMA 侧）

```
sdma_fetch_and_exec():
  1. loop:
       a. 读 RPTR / WPTR
       b. if RPTR == WPTR: 等待 Doorbell 信号（state = IDLE）
       c. 读 ring[RPTR].header，解析 opcode
       d. switch (opcode):
            - COPY: 解析 src/dst/size → process_h2d() / process_d2h() / process_d2d()
            - FENCE: 触发 fence_table_[fence_id] 内存写入，唤醒等待的 driver ISR
            - SG: 解析 chain_addr，遍历 SG entries
       e. process_xxx() 完成后：
            - 写 CompletionBundle 到 done_out 端口（IRQ + fence_id）
            - RPTR += 1（或 SG n_entries）
       f. 回到 a.
```

### §4.4 满/空判定 + Race 防护

```
queue_empty():   return (WPTR == RPTR) mod ring_size
queue_full():    return ((WPTR + 1) mod ring_size) == RPTR  // 留 1 空位区分满/空
queue_count():   return (WPTR - RPTR + ring_size) mod ring_size
```

**Race 防护**（多生产者场景）：
- WPTR 推进：`std::atomic<uint32_t>` + `compare_exchange_strong` 循环
- Ring Buffer 内存：driver 写入后 `__atomic_store_n` 或 memory barrier（`std::atomic_thread_fence(std::memory_order_release)`）
- RPTR 推进：单消费者（SDMA）无需 atomic，但需 `acquire` fence 保证读到最新 WPTR

### §4.5 Ring Buffer 回绕处理

```
WPTR = (WPTR + 1) mod ring_size
RPTR = (RPTR + 1) mod ring_size

满判定特殊处理：因 RPTR == WPTR 表示空，故满判定为 (WPTR + 1) mod ring_size == RPTR
→ ring_size 必须为 2 的幂（典型 128 / 256 / 512 / 1024），保证 mod 高效
```

---

## §5 Doorbell 协议

### §5.1 Doorbell 寄存器接口

```
地址: BAR1 + 0x10010000（SDMA Doorbell）

写入语义：
  write32(addr, wptr_offset):
    - SDMA 收到写信号 → 触发 fetch_event_
    - 若 SDMA 在 IDLE 态：立即进入 FETCH 态
    - 若 SDMA 在 FETCH/DECODE/EXECUTE 态：完成当前 entry 后立即处理新 WPTR
    
  写入值 wptr_offset 是相对增量（不是绝对 WPTR）：
    - 例：WPTR=100，driver 推 3 条 → 写 3（Doorbell 增量）
    - 或写绝对值（不同实现选择），本设计用绝对值（更简单）

读语义：
  read32(addr) → 当前 WPTR（驱动调试用，正常不应读）
```

### §5.2 与 `Doorbell` 类绑定

```cpp
// 与现有 doorbell_mvp.cc/hh 绑定
Doorbell sdma_doorbell("sdma_doorbell", 250ns /* latency */);
sdma_doorbell.register_handler([&](uint32_t stream_id, uint64_t wdu_offset) {
  if (stream_id == SDMA_STREAM_ID) {
    sdma_engine_.fetch_event_.notify(); // 250-700ns 后 SDMA 看到
  }
});

// driver 路径
driver_write_doorbell(sdma_doorbell_addr, new_wptr);
  → sdma_doorbell.ring(SDMA_STREAM_ID, new_wptr)
  → SDMA fetch_event_.notify() after latency
```

### §5.3 强序保证

- **WPTR 写入原子性**：32-bit aligned write 必须 atomic（`std::atomic<uint32_t>`）
- **Doorbell 写 vs Ring 数据写顺序**：driver 必须先写 Ring 条目 → memory barrier → 写 Doorbell（否则 SDMA 看到 Doorbell 但 Ring 数据未达）
- **driver 内写入顺序**：`std::atomic_thread_fence(std::memory_order_release)` 在 Doorbell 写之前

---

## §6 Packet 格式

### §6.1 Ring Entry Header (32 位)

```
bit 31..24: opcode (8 bit)
  0x01 = COPY_LINEAR (basic copy, H2D/D2H/D2D)
  0x02 = COPY_SG (scatter-gather, 多条目链)
  0x03 = COPY_TILED (2D tiled copy, 进阶)
  0x04 = FENCE (内存栅栏, 不执行 DMA, 仅更新 fence_table_)
  0x05..0xFF = 保留

bit 23..0: count (24 bit) - SG 条目数（COPY_LINEAR 时 = 1）
```

### §6.2 Ring Entry Payload（32 字节基础）

```
偏移  字段              含义
0x00  header            见 §6.1
0x04  src_addr_lo       源地址低 32 位（GPU 侧可解析）
0x08  src_addr_hi       源地址高 32 位
0x0C  dst_addr_lo       目标地址低 32 位
0x10  dst_addr_hi       目标地址高 32 位
0x14  transfer_size     传输字节数（对齐 4 字节）
0x18  flags             见 §6.3
0x1C  fence_id_lo       Fence ID 低 32 位（flags[FENCE]=1 时有效）
```

### §6.3 flags 字段（32 位）

```
bit 31     FENCE (本次传输完成后触发 fence_id)
bit 30     SG (scatter-gather chain_addr 指向下一条目)
bit 29     INTERRUPT_ON_COMPLETE (完成后发 MSI-X)
bit 28     CHAIN (与 SG 等价，本设计统一用 SG)
bit 27..16  reserved
bit 15..12  src_addr_space (0=VRAM, 1=GART/IOVA, 2=PCIe config)
bit 11..8   dst_addr_space (同上)
bit 7..0   reserved
```

### §6.4 D2D / H2D / D2H 三向编码

| 传输类型 | src_addr_space | dst_addr_space | 数据路径 |
|---------|----------------|----------------|---------|
| **H2D** (Host→Device) | GART/IOVA=1 | VRAM=0 | host_out (PCIe TLP MWr) → mem_out |
| **D2H** (Device→Host) | VRAM=0 | GART/IOVA=1 | mem_in → host_out (PCIe TLP MRd) |
| **D2D** (Device↔Device) | VRAM=0 | VRAM=0 | mem_in → mem_out（NoC bypass, 不走 PCIe） |

### §6.5 SG Chain 编码（扩展）

```
bit 30 (SG) = 1 时:
  src_addr 字段重新定义为 chain_addr（SG 链表头地址）
  transfer_size 字段重新定义为 n_entries
  flags[31] FENCE 在最后一个条目触发
```

---

## §7 状态机

### §7.1 IDLE → FETCH → DECODE → EXECUTE → COMPLETE FSM

```
       ┌─────┐
   ┌──→│IDLE │←── (Ring 空, 等待 Doorbell)
   │   └──┬──┘
   │      │ Doorbell 触发 / WPTR > RPTR
   │      ▼
   │   ┌──────┐
   │   │FETCH │─── ring[rptr].header 读取
   │   └──┬───┘
   │      │ opcode 解析完成
   │      ▼
   │   ┌──────┐
   │   │DECODE│─── src/dst/size/flags 解析
   │   └──┬───┘
   │      │ 描述符就绪
   │      ▼
   │   ┌───────┐
   │   │EXECUTE│─── process_h2d / process_d2h / process_d2d
   │   │       │    地址翻译（§8）
   │   │       │    PCIe TLP / NoC bypass
   │   └───┬───┘
   │       │ 传输完成
   │       ▼
   │   ┌─────────┐
   │   │COMPLETE│─── done_out (CompletionBundle)
   │   │        │    fence_table_[id] 写入
   │   │        │    MSI-X 触发（flags[INTERRUPT]=1）
   │   └───┬─────┘
   │       │ RPTR += 1
   │       ▼
   └─────── (loop back to FETCH/IDLE)
```

### §7.2 与现有 CP FSM 对齐

| SDMA FSM | CP FSM | 关系 |
|---------|--------|------|
| IDLE | IDLE | 双方均等待 |
| FETCH | FETCH | CP 读 cmd ring，SDMA 读 descriptor ring |
| DECODE | DECODE | CP 解 PM4，SDMA 解 descriptor header |
| EXECUTE | DISPATCH | CP 转发到引擎，SDMA 执行 PCIe TLP/NoC |
| COMPLETE | COMPLETE | 双方均通过 CompletionRing 上报 |

### §7.3 状态转换条件

```cpp
void sdma_tick() {
  switch (state_) {
    case IDLE:
      if (wptr_ != rptr_) { state_ = FETCH; notify(fetch_event_); }
      break;
    case FETCH:
      read_entry(rptr_);
      state_ = DECODE;
      break;
    case DECODE:
      decode_current();
      state_ = EXECUTE;
      break;
    case EXECUTE:
      switch (entry.opcode) {
        case COPY_LINE: process_copy(); break;
        case COPY_SG: process_sg(); break;
        case FENCE: process_fence(); break;
      }
      state_ = COMPLETE;
      break;
    case COMPLETE:
      write_done_out(CompletionBundle{entry.fence_id, entry.flags});
      if (entry.flags & FENCE) write_fence_table(entry.fence_id);
      if (entry.flags & INTERRUPT) trigger_msix(vector);
      rptr_ = (rptr_ + entry.count) % ring_size;
      state_ = IDLE;
      break;
  }
}
```

---

## §8 地址翻译链

### §8.1 4 级翻译链

```
驱动视角：
  CPU VA (0x7fff_ffff_f000)
    ↓ 页表（Linux 内核 mm/）
  Host PA (0x1000_0000)
    ↓ IOMMU（Intel VT-d / AMD IOMMU）
  IOVA (0x1000_0000)  ← dma_translate_cb(iova, size, &phys) 输入
    ↓ GART/IOMMU 页表（GPU 端，可选 identity 或 IOMMU 翻译）
  GPU MC address (0x1000_0000)  ← SDMA 内部使用
    ↓ VRAM aperture decode（判断落在 VRAM 内还是 PCIe aperture）
  最终: PCIe TLP (MRd/MWr to PA) 或 VRAM 访问
```

### §8.2 GART/IOMMU 翻译实现

```cpp
// 简化 GartTable 接口
class GartTable {
public:
  // identity mode: phys = iova（无需 host IOMMU）
  // iommu mode: phys = iommu_translate(iova)（调用 host cb）
  enum class Mode { Identity, Iommu };
  void set_mode(Mode m) { mode_ = m; }
  
  int translate(uint64_t iova, uint32_t size, uint64_t* out_pa) {
    if (mode_ == Identity) { *out_pa = iova; return 0; }
    if (translate_cb_) return translate_cb_(iova, size, out_pa);
    return -ENOSYS;
  }
  
private:
  Mode mode_ = Mode::Identity;
  DmaTranslateCb translate_cb_;  // 已实现（stage 1.3c 真实化）
};
```

### §8.3 SDMA 翻译调用点

```cpp
void process_h2d() {
  uint64_t host_pa;
  int ret = gart_.translate(entry_.src_addr, entry_.transfer_size, &host_pa);
  if (ret != 0) { error_cb_(ret); return; }
  
  // 发起 PCIe TLP MWr
  PcieTlp tlp;
  tlp.op = MWr;
  tlp.addr = host_pa;
  tlp.length = entry_.transfer_size;
  host_out_.write(tlp); // 跨仓出口
}
```

### §8.4 VRAM Aperture Decode

```cpp
bool decode_addr_space(uint64_t gpu_addr, AddrSpace* space) {
  if (gpu_addr >= vram_aperture_base_ && 
      gpu_addr < vram_aperture_base_ + vram_aperture_size_) {
    *space = VRAM;
    return true;
  }
  *space = GART;  // 其他地址走 PCIe TLP
  return false;
}
```

---

## §9 完成通知

### §9.1 MSI-X 中断投递（修复 #4）

```
SDMA COMPLETE 态
  ↓
if (entry.flags & INTERRUPT_ON_COMPLETE):
  done_out.write(CompletionBundle{entry.fence_id, status})
  ↓
CompletionRingTLM.done_in(port)
  ↓
CompletionRing 内部 MsiXDeliveryBundle 累积
  ↓
irq_out → PcieEndpointIP.irq_out
  ↓
board->trigger_irq_async(vector)   ← 修复 #4 必须有调用方
  ↓
UsrLinuxEmu intr_cb 被调（bridge.cpp:337 register_msix_callback）
  ↓
Driver ISR 处理命令完成
```

### §9.2 Fence 机制（Ring 内 Fence 命令）

```
Fence 命令（opcode=0x04, transfer_size=0, fence_id=X）:
  SDMA FETCH → DECODE (opcode=FENCE) → EXECUTE (process_fence)
  → COMPLETE: 写 fence_table_[X] = 1（用户态内存）
  → 等待该 fence 的 driver 线程（条件变量）被唤醒
  
driver 视角：
  fence_wait(fence_id):
    while (fence_table_[fence_id] == 0) cond_wait()  // 阻塞
    return; // fence 已触发
```

### §9.3 MSI-X + Fence 双轨

| 场景 | MSI-X 用 | Fence 用 |
|------|---------|---------|
| 单次命令完成 | ✅ flags[INTERRUPT_ON_COMPLETE]=1 | ✅ flags[FENCE]=1 |
| 批量命令序列 | ❌（中断风暴）| ✅ fence 在序列末尾 |
| 高频小任务 | ❌ | ✅ Fence 合并（ring buffer 内连续 Fence 只触发最后一个） |
| 调试/性能分析 | ✅ 单次中断便于 profiling | ❌ |

### §9.4 Fence Table 内存映射

```
BAR0 + 0x20000 (Fence Table Base, 4 KB):
  fence_id 0: 4 字节 (uint32_t, 0=pending, 1=triggered)
  fence_id 1: 4 字节
  ...
  fence_id 1023: 4 字节
  → 1 KB 容纳 256 fences × 4B
  → 4 KB 容纳 1024 fences
```

---

## §10 D2D 路径

### §10.1 数据路径（无 PCIe）

```
SDMA EXECUTE 态 (D2D)
  ↓
decode_addr_space(src) = VRAM
decode_addr_space(dst) = VRAM
  ↓
vram_backdoor_.read(src, buf, size)
  ↓ memcpy via 显存控制器直接访问
vram_backdoor_.write(dst, buf, size)
  ↓
COMPLETE → RPTR += 1
（不经过 host_out / PCIe TLP）
```

### §10.2 NoC 数据面扩展（vs 现有延迟模型）

```cpp
// 现有 gpu_mesh_noc_tlm.hh: 仅 route_latency (XY hops × latency)
class GpuMeshNoC {
  sc_time route_latency(uint32_t src_id, uint32_t dst_id); // 仅延迟
};

// 新增：数据面传输（阶段 1.3b）
class GpuMeshNoCData {
  // D2D 传输：src_agent → NoC → dst_agent，延迟 + 带宽
  void transfer(uint64_t src_addr, uint64_t dst_addr, size_t size,
                std::function<void(bool)> done_cb);
};
```

### §10.3 D2D 路径不写 PCIe

```
test 断言（阶段 1.3b 验证）：
  D2D descriptor 提交后：
  - host_out 端口零事务（grep host_out events = 0）
  - mem_in + mem_out 端口各 1 次
  - done_out 端口 1 次（含 fence_id）
```

### §10.4 D2D 与 PCIe P2P 区别

| 维度 | D2D（同 GPU 内）| P2P（跨 GPU PCIe）|
|------|---------------|------------------|
| 路径 | GPU 内部 NoC | PCIe 总线（P2P）|
| 延迟 | 100-500 ns | 1-5 μs |
| 带宽 | 数百 GB/s | 32-64 GB/s（PCIe Gen5 x16）|
| 阶段 | 阶段 1.3b | 阶段 2.1 |
| 地址空间 | 同 GPU VRAM | 跨 GPU VRAM（需 IOMMU） |

---

## §11 CmdProc 集成

### §11.1 CP→SDMA DMA 转发路径

```
CommandProcessorTLM (5-state FSM)
  ↓
PM4 命令包解析（pm4_decoder_mvp.cc）
  ↓
识别 DMA/copy 类 opcode（新增映射）：
  0x4000 DISPATCH_DIRECT → TMU 路径（已有）
  0x4200 EVENT_WRITE → fence 路径（已有）
  0x4400 RELEASE_MEM → fence 释放（已有）
  0x4500 ACQUIRE_MEM → fence 等待（已有）
  0x4600 DMA_COPY (新增) → SDMA 路径 ← 本设计补全
  0x4700 DMA_SG (新增) → SDMA SG 路径
  0x4800 DMA_FENCE (新增) → SDMA Fence 路径
  ↓
DISPATCH 态：
  switch (opcode) {
    case DMA_COPY: 构造 SdmaRingEntry{COPY_LINEAR, ...} → push 到 SDMA Ring
    case DMA_SG: 构造 SdmaRingEntry{COPY_SG, ...} → push SG
    case DMA_FENCE: 构造 SdmaRingEntry{FENCE, fence_id} → push Fence
    case DISPATCH_DIRECT: 走 TMU（已有）
    case EVENT_WRITE: 走 fence（已有）
  }
  ↓
dma_req[2] 端口（已声明但 tick() 未写）→ 写入 SDMA Ring Buffer
```

### §11.2 PM4 DMA Opcode 映射表（新增）

| PM4 opcode (method range) | SDMA Ring Entry opcode | 备注 |
|--------------------------|------------------------|------|
| `0x4600` DMA_COPY_LINEAR | `0x01` COPY_LINEAR | H2D/D2H/D2D 自动检测 |
| `0x4700` DMA_COPY_SG | `0x02` COPY_SG | SG chain |
| `0x4800` DMA_COPY_TILED | `0x03` COPY_TILED | 2D tiled copy（阶段三）|
| `0x4900` DMA_FENCE | `0x04` FENCE | 不发起 DMA，仅 fence_id |

### §11.3 CP dma_req 端口接线（阶段 1.3c 实施）

```cpp
// command_processor_mvp.cc 当前状态（line ~89）
tlm_utils::simple_initiator_socket<CommandProcessorTLM> dma_req; // [2]

// 修改（阶段 1.3c）：
void CommandProcessorTLM::dispatch_dma_copy(PM4Packet& pkt) {
  // 构造 SdmaRingEntry
  SdmaRingEntry entry{};
  entry.header = (0x01u << 24) | 1u; // COPY_LINEAR, count=1
  entry.src_addr = pkt.src_addr;
  entry.dst_addr = pkt.dst_addr;
  entry.transfer_size = pkt.size;
  entry.flags = (pkt.fence_id ? (1u<<31) : 0) |
                (pkt.interrupt ? (1u<<29) : 0);
  entry.fence_id_lo = pkt.fence_id;
  
  // 通过 dma_req 端口发送（最终改为 SDMA Ring Buffer push）
  DmaDescriptorBundle bundle;
  bundle.dma_desc.dir = decode_dir(entry);
  bundle.dma_desc.host_iova = pkt.src_addr; // H2D/D2H
  bundle.dma_desc.vram_offset = pkt.dst_addr & 0xFFFFFFFFFF;
  bundle.dma_desc.size = pkt.size;
  bundle.dma_desc.tag = pkt.fence_id;
  dma_req->write(bundle); // 现有 wire-format（不变）
  
  // 未来直接 push 到 SDMA Ring Buffer（阶段 1.3c）：
  // sdma_engine_.push_descriptor(...); // Ring Buffer 路径
}
```

### §11.4 双轨期（阶段 1.3c 过渡）

为保持 5 端口 wire-format 不变，CP→SDMA 路径采用 **Ring Buffer 作为 desc_in 前端**模式：

```
CP dma_req[2] → (暂时) DmaDescriptorBundle → SDMA desc_in 端口 (现有)
                   ↓
              (未来) CP push 到 SDMA Ring Buffer → 5 端口 desc_in → 后续处理
```

**过渡策略**（阶段 1.3a）：
1. 5 端口 wire-format **冻结**（已 commit）
2. SDMA 引擎**内部**新增 Ring Buffer，Ring→descriptor 转换在 `handle_desc_in()` 内
3. CP `dma_req` 端口**暂时**仍写 DmaDescriptorBundle（保持 ABI 兼容）
4. 阶段 1.3c 后：CP `push_descriptor(entry)` → Ring Buffer 直接推送

---

## §12 关键约束与边界

### §12.1 ABI 兼容性约束

- 5 端口 wire-format 不变（`design.md §2.5` 冻结）
- 22 ABI 函数签名不变（`pcie-endpoint-architecture.md §4.3`）
- `DmaTranslateCb` 签名不变（ADR-088 §D3.8）

### §12.2 时序约束

| 操作 | 典型时序 | 备注 |
|------|---------|------|
| Doorbell → SDMA fetch | 250-700 ns（latency 模型） | `Doorbell` 类已有 |
| SDMA FETCH → DECODE | < 100 ns | 同步 |
| DECODE → EXECUTE | < 100 ns | 同步 |
| EXECUTE (D2D, 4 KB) | ~500 ns | NoC bypass |
| EXECUTE (H2D, 4 KB) | ~2 μs | PCIe TLP |
| EXECUTE → COMPLETE | < 100 ns | 同步 |
| COMPLETE → MSI-X 投递 | < 1 ms（端到端） | 修复 #4 后 |

### §12.3 资源边界

| 资源 | 上限 | 备注 |
|------|------|------|
| Ring Buffer 容量 | 4 KB / 8 KB / 16 KB / 64 KB | cfg.ring_size |
| SG chain 深度 | MAX_SG_ENTRIES = 8 | SdmaRingEntry 数组 |
| Fence ID | 0..1023 | Fence Table 4 KB |
| MSI-X 向量 | 4-8（基础必备）| 与 PCIe EP 共享 |

### §12.4 错误处理

| 错误码 | 触发条件 | 处理 |
|--------|---------|------|
| `-ENOSPC` | Ring Buffer 满 | driver 等待 WPTR（spinlock/futex）|
| `-EINVAL` | descriptor 字段非法（size 未对齐 / addr 越界）| error_cb_(EINVAL) |
| `-EIO` | PCIe TLP 错误（Completion 状态非 Success）| error_cb_(EIO) + 完成状态 |
| `-ETIMEDOUT` | Doorbell 后 1ms 内未 fetch | SDMA 内部 state = ERROR，error_cb_(ETIMEDOUT) |

---

## §13 同步点与里程碑

### §13.1 实施同步点

| M | 内容 | 对应 openspec 任务 |
|---|------|-------------------|
| M1 | SDMA Ring Buffer + RPTR/WPTR + Doorbell 绑定（基础） | 阶段 1.3a |
| M2 | SG 描述符扩展 | 阶段 1.3a |
| M3 | D2D SDMA 路径（NoC 数据面 + 显存控制器 bypass）| 阶段 1.3b |
| M4 | dma_translate_cb 真实化（#2 修复）| 阶段 1.3c |
| M5 | GART/IOMMU 4 级翻译链 | 阶段 1.3c |
| M6 | SDMA 完成通知（MSI-X + Fence）| 阶段 1.3d |
| M7 | CP→SDMA DMA 转发（PM4 opcode 0x4600-0x4900）| 阶段 1.3c |

### §13.2 关键里程碑

| M | 内容 | 时间 |
|---|------|------|
| **阶段 1.3a** | Ring Buffer + RPTR/WPTR + Doorbell + SG | 1 周 |
| **阶段 1.3b** | D2D NoC 数据面 + 显存控制器 | 0.5-1 周 |
| **阶段 1.3c** | dma_translate_cb 真实化 + CP→SDMA DMA 转发 | 0.5 周 |
| **阶段 1.3d** | SDMA 完成通知（MSI-X + Fence）| 0.5 周 |
| **阶段 1.3 合计** | | 2.5-3 周（roadmap 阶段 1.3 工期 0.5 周 → 2.5-3 周）|

### §13.3 跨仓验证

- [ ] 阶段 1.3a 后：CppTLM `test_sdma_ring_rptr_wptr` + `test_sdma_packet_codec` 全 PASS
- [ ] 阶段 1.3b 后：CppTLM `test_d2d_noc_path`（D2D host_out 零事务断言）全 PASS
- [ ] 阶段 1.3c 后：CppTLM `test_dma_translate_iommu` + UsrLinuxEmu `test_bridge_kcpptlm_profile_real` profile 同步升级
- [ ] 阶段 1.3d 后：CppTLM `test_sdma_fence` + `test_sdma_msix`（intrcb 触发断言）全 PASS
- [ ] UsrLinuxEmu 5.5.7.1 profile 测试同步升级 `CHECK → REQUIRE + buf 内容`
- [ ] ctest 双向全绿 + docs-audit PASS

---

## §14 修订记录

- **v0.1** (2026-09-09, Draft): 初版,基于 Oracle 审查 CONDITIONAL (4.5/10) + 用户战略调整
  - §1 三层 DMA 架构概述（CommandBuffer/SDMA/CP 角色）
  - §2 类定义（5 端口冻结 + RingBuffer/RPTR/WPTR/Doorbell/SG 新增）
  - §3 Ring Buffer 结构（容量/对齐/物理布局）
  - §4 RPTR/WPTR 算法（满/空判定 + race 防护）
  - §5 Doorbell 协议（BAR0 寄存器 + 强序 + Doorbell 类绑定）
  - §6 Packet 格式（Header opcode+count + Payload + D2D/H2D/D2H 三向）
  - §7 状态机（IDLE→FETCH→DECODE→EXECUTE→COMPLETE 与 CP FSM 对齐）
  - §8 地址翻译链（4 级 + identity/IOMMU 两模式 + GART 集成）
  - §9 完成通知（MSI-X + Fence 双轨 + Fence Table 内存映射）
  - §10 D2D 路径（NoC 数据面扩展 + 显存控制器 bypass）
  - §11 CmdProc 集成（PM4 DMA opcode 0x4600-0x4900 映射 + 双轨过渡）
  - §12 关键约束（ABI/时序/资源/错误）
  - §13 同步点与里程碑（M1-M7 + 阶段 1.3 工期 0.5 周 → 2.5-3 周）
- **待 v0.2**: 阶段 1.3a 实施后追加（实际 wire-format 验证 + 性能基准）