# dgpu-board-adapter-info Delta

## ADDED Requirements

### Requirement: 板卡适配器详细拓扑信息暴露
CppTLM 的 C ABI (`cpptlm_emulator.h`) 及 `DGpuBoard` 外壳 MUST 暴露包含显存层次划分（Visible 与 Invisible VRAM）、GPU 虚拟地址空间大小、GPU ID 及 BDF 拓扑的完整设备描述。

#### Scenario: 查询包含显存划分的设备信息
- **WHEN** 调用 `cpptlm_emulator_get_device_info` 或 `cpptlm_emulator_get_adapter_info`
- **THEN** 返回的结构体中 `visible_vram_size` 大于 0、`invisible_vram_size` 正确反映独占内部显存大小、`va_region_size` 反映虚拟地址空间范围，且 `bar_sizes[6]` 完整列出所有 BAR 的窗口大小

#### Scenario: 配置文件缺省时的默认值回退
- **WHEN** 板卡配置 JSON 文件中未显式提供拓扑字段并调用 `load_soc_config`
- **THEN** 系统自动提供工业级默认回退值（Visible VRAM >= 256MB，Invisible VRAM >= 1GB，VA 空间 >= 48-bit）且不报错

### Requirement: First-touch Handle 生命周期管理
CppTLM 的 C ABI MUST 提供基于 `cpptlm_emulator_handle_t` 的设备句柄创建、销毁与基于句柄的信息查询接口。

#### Scenario: 创建 Handle 并查询信息
- **WHEN** 调用 `cpptlm_emulator_open(dev_id, &handle)` 成功返回句柄，随后调用 `cpptlm_emulator_get_adapter_info(handle, &info)`
- **THEN** 返回 0，info 结构体成功获取与指定 `dev_id` 设备一致的详细拓扑信息

#### Scenario: 释放 Handle 与失效访问校验
- **WHEN** 调用 `cpptlm_emulator_close(handle)` 释放设备句柄
- **THEN** 句柄被注销，后续针对该句柄的任何查询操作均返回 -EINVAL
