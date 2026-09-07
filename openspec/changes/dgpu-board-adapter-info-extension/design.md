# Design: dgpu-board-adapter-info-extension

## Context

在真实 GPU 硬件以及标准 Linux 内核驱动（如 `amdgpu`）的设计中，CPU 端驱动必须清晰区分两类显存：
1. **Visible VRAM (CPU 可直接寻址显存)**：通过 PCI BAR（通常为 BAR2 / Framebuffer BAR）直接映射到 CPU 虚拟地址空间，CPU 可以通过 `readl/writel` 或直接内存拷贝进行高速读写。
2. **Invisible VRAM (GPU 内部独占显存)**：无法直接被 CPU 地址窗口覆盖（受 32-bit BAR 或主板地址窗口限制），必须通过分页机制（GPU Page Tables）或 DMA 搬运（H2D / D2H）进行交互。

此外，驱动初始化必须精确获知 GPU 虚拟地址（GPUVM）范围、硬件版本（GFX Version）以及 PCI BDF 拓扑位置。CppTLM 现存的 `cpptlm_device_info_t` 缺少这些属性，且缺乏面向多线程外部调试器的 Handle 访问通道。

## Goals / Non-Goals

**Goals:**
- 在 CppTLM 的 `cpptlm_device_info_t` 与 `DGpuBoard::DeviceInfo` 中补齐显存架构（Visible/Invisible/VA）与拓扑字段。
- 提供面向调用方的 First-touch Handle 接口（`cpptlm_emulator_open/close/get_adapter_info`），支持基于 Handle 的安全状态查询。
- 在 `DGpuBoard` 中实现对板卡 JSON 配置文件（如 `configs/dgpu_board_v1.json`）中显存拓扑字段的兼容解析。

**Non-Goals:**
- 不修改 CppTLM 现存 19 个 ABI 函数的符号签名，保持严格向下兼容。
- 本 change 不重写数据面仿真逻辑（CommandProcessor、SM 内部架构保持原样），仅扩展板卡外壳（Shell）的拓扑自省与 Handle 控制面能力。

## Decisions

### D1: ABI 结构体向后兼容扩展
在 `cpptlm_device_info_t` 尾部追加新字段，并在 `cpptlm_emulator_get_device_info` 和新增的 `cpptlm_emulator_get_adapter_info` 中正确填充。
```c
typedef struct cpptlm_device_info_s {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t revision;
    uint32_t subsys_vendor_id;
    uint32_t subsys_device_id;
    char profile_path[256];
    
    /* ── v1.1 扩展字段: 对齐 amdgpu / QEMU 设备拓扑 ── */
    uint64_t visible_vram_size;
    uint64_t invisible_vram_size;
    uint64_t va_region_size;
    uint32_t gpu_id;
    uint16_t gfx_version;
    uint16_t bdf;
    uint64_t bar_sizes[6];
} cpptlm_device_info_t;
```

### D2: Handle 映射设计
- `cpptlm_emulator_handle_t` 定义为 `uint64_t` 不透明句柄。
- 底层维护一个线程安全的句柄查找表（`std::unordered_map<uint64_t, cpptlm_emulator_t*>` + `std::mutex`）。
- `open` 操作在首次触碰时建立实例引用并返回句柄；`close` 释放对应句柄并在引用归零时执行清理。

## Risks / Trade-offs

- [结构体大小变化导致二进制不兼容] → 该 ABI 仅在当前仿真项目树中由 `UsrLinuxEmu` 动态加载，且通过语义版本控制，双仓协同升级。
- [JSON 配置文件缺失新字段] → `DGpuBoard::load_soc_config` 提供合理的工业级默认值（Visible VRAM 默认 256MB，Invisible VRAM 默认 15GB，VA 默认 48-bit 256TB）。
