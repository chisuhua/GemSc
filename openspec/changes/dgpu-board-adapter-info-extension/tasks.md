# Tasks: dgpu-board-adapter-info-extension

## 1. CppTLM C ABI 接口与类型扩展

- [ ] 1.1 在 `include/abi/cpptlm_emulator.h` 中扩展 `cpptlm_device_info_t` 结构体：追加 `visible_vram_size`、`invisible_vram_size`、`va_region_size`、`gpu_id`、`gfx_version`、`bdf`、`bar_sizes[6]` 字段
- [ ] 1.2 在 `include/abi/cpptlm_emulator.h` 中定义 `cpptlm_emulator_handle_t` 类型 (`uint64_t`)，并声明 3 个新增函数原型：`cpptlm_emulator_open`、`cpptlm_emulator_close`、`cpptlm_emulator_get_adapter_info`
- [ ] 1.3 在 `include/tlm/gpu/dgpu_board_shell.hh` 中同步扩展 `DGpuBoard::DeviceInfo` 结构体字段（含 `gfx_version`）

## 2. Shell 拓扑解析与 Handle 映射实现

- [ ] 2.1 在 `src/tlm/gpu/dgpu_board_shell.cc` 的 `DGpuBoard::load_soc_config` 中增加对显存拓扑字段的 JSON 解析与缺省默认值填充
- [ ] 2.2 在 `src/abi/cpptlm_emulator.cc` 中实现 Handle 管理哈希表及 `cpptlm_emulator_open/close/get_adapter_info` 函数实现
- [ ] 2.3 更新 `src/abi/cpptlm_emulator.cc` 中原有的 `cpptlm_emulator_get_device_info` 实现，填充新增的显存与拓扑字段

## 3. 测试与验证

- [ ] 3.1 编写 `test/test_dgpu_adapter_info.cc`：验证 Handle 创建/释放、Adapter 信息字段非空及其合理性
- [ ] 3.2 编译 CppTLM 测试套件并运行 `./build/bin/cpptlm_tests "[abi]"`，确保全部测试通过且既有 ABI 测试零回归
