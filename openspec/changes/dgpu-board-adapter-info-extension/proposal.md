# Proposal: dgpu-board-adapter-info-extension

## Why

为支撑 UsrLinuxEmu 端的真实硬件仿真绑定（`kcpptlm-backend-binding-with-handle-and-adapter-info`），并对齐 Linux amdgpu/QEMU/gem5 的设备发现范式，CppTLM 现有的 `cpptlm_device_info_t` 结构体以及 `DGpuBoard` 内部的 `DeviceInfo` 仅暴露了基础的 `vendor_id`、`device_id` 和 `bar_sizes[6]`，缺少驱动初始化所必需的关键适配器拓扑属性（visible VRAM 大小、invisible 内部独占显存大小、GPU VA 区域范围、GPU ID 及 BDF 位置）。同时，需要从 C ABI 层提供 First-touch Handle 生命周期管理接口，以支持跨线程安全的显卡句柄持有与自省查询。

## What Changes

- **扩展 `cpptlm_device_info_t` 结构体**（`include/abi/cpptlm_emulator.h`）：
  - 增加 `uint64_t visible_vram_size`（CPU 可直接寻址的 Framebuffer / BAR2 窗口大小）
  - 增加 `uint64_t invisible_vram_size`（GPU 独占内部显存大小）
  - 增加 `uint64_t va_region_size`（GPU 虚拟地址空间范围）
  - 增加 `uint32_t gpu_id`（多卡拓扑中的唯一 GPU 标识）
  - 增加 `uint16_t gfx_version`（硬件体系结构版本，如 RDNA3 GFX1100）
  - 增加 `uint16_t bdf`（PCI 拓扑位置: Bus:Dev.Func，打包规则: bus<<8 | dev<<3 | func）
  - 增加 `uint64_t bar_sizes[6]`（显式公开全部 6 个 BAR 窗口尺寸）
- **扩展 `DGpuBoard::DeviceInfo` 内部类**（`include/tlm/gpu/dgpu_board_shell.hh`）：
  - 同步补充上述显存与拓扑字段，并在 `load_soc_config()` 中支持从板卡 JSON 配置解析这些属性。
- **新增 3 个 C ABI 接口**（`include/abi/cpptlm_emulator.h` 与 `src/abi/cpptlm_emulator.cc`）：
  - `cpptlm_emulator_open(uint32_t dev_id, cpptlm_emulator_handle_t* out_handle)`：First-touch 方式创建或获取设备实例句柄。
  - `cpptlm_emulator_close(cpptlm_emulator_handle_t handle)`：安全释放句柄。
  - `cpptlm_emulator_get_adapter_info(cpptlm_emulator_handle_t handle, cpptlm_device_info_t* out_info)`：基于 Handle 跨线程查询适配器属性。
- **新增单元测试**：
  - `test/test_dgpu_adapter_info.cc`：验证 DeviceInfo 扩展字段正确从配置加载，并通过 ABI 正确导出。

## Capabilities

### New Capabilities

- `dgpu-board-adapter-info`: 扩展 `DGpuBoard` 与 C ABI 的适配器信息暴露能力，支持 visible/invisible 显存划分、VA 范围与 Handle 级生命周期管理。

### Modified Capabilities

（无）

## Impact

- **受影响代码**：
  - `include/abi/cpptlm_emulator.h`
  - `src/abi/cpptlm_emulator.cc`
  - `include/tlm/gpu/dgpu_board_shell.hh`
  - `src/tlm/gpu/dgpu_board_shell.cc`
  - `test/test_dgpu_adapter_info.cc` (新增)
- **跨仓协同**：为 UsrLinuxEmu 仓的 `kcpptlm-backend-binding-with-handle-and-adapter-info` change 提供底层 ABI 供给。
