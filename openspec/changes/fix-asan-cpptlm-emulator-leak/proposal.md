## Why

Build (Debug, ASan=ON) 在 main 上从 2026-07-17 起持续 fail，已 53+ 天。本 change 定位并修复该 pre-existing 失败。

**症状**（CI log）：

```
The following tests FAILED:
          5 - test_cpptlm_emulator_msix (Failed)
          6 - test_cpptlm_emulator_lookup_register (Failed)
```

**ASan 报告**（多份 leak trace 累加，本地 `build-asan` + ctest 可复现）：

```
Direct leak of 592 byte(s) in 2 object(s) allocated from:
    #1 ModuleFactory::registerModule<TpcCluster>::lambda
    #6 ModuleFactory::instantiateAll
    #7 GpcCluster::simulate_instantiate
    #8 ModuleFactory::instantiateAll          ← 嵌套 instantiate
    #9 GpuCluster::simulate_instantiate
    #11 DGpuSoc::simulate_instantiate
    #12 DGpuBoard::load_soc_config
    #13 cpptlm_emulator_create_by_id
    #14 CATCH2_INTERNAL_TEST_0  test_cpptlm_emulator_msix.cc:11
```

**根因链**（两段失败叠加）：

1. **JSON schema validation 失败**：
   - `configs/dgpu_board_v1.json` 的 soc 子模块（`board_cfg["modules"][0]`）**缺 `connections` 字段**
   - `ModuleFactory::validateConfig(soc_cfg)`（src/core/module_factory_validate.cc:210-213）检查顶层 config 必须含 `connections` array
   - soc_cfg 通过 → `validateConfig` return false → `instantiateAll` return false
   - `SimModule::simulate_instantiate`（include/core/sim_module.hh:95-97）只 log error **不抛异常**，继续执行 `parsePortConfigs` 和子模块递归
   - 但子模块 instance 在 `instantiateAll` 失败时**未创建** → `getInstance("pcie_ep")` 返 nullptr
2. **测试 REQUIRE 失败时跳过 cleanup**：
   - `test/test_cpptlm_emulator_msix.cc:14/24/36` 用 `REQUIRE(...) == 0` 断言
   - `msix_init/update_pending/clear_pending` 因 ep 不存在返 -38（src/tlm/gpu/dgpu_board_shell.cc:228-238） → REQUIRE 抛 Catch2 异常
   - catch2 REQUIRE 抛异常 → 测试函数立即退出 → **`cpptlm_emulator_destroy(emu)` 跳过执行**
   - board 未 shutdown → `sim_thread_` 仍在跑 → `soc_.reset()` 未执行 → DGpuSoc + 内部所有 SimModule（TpcCluster/GpcCluster/GpuCluster 等）全部泄漏

**未触及的旁路泄漏**：`TmuDispatchProcessorTLM`（configs/dgpu_board_v1.json:47）也未注册到 ModuleFactory（include/chstream_register.hh 的 REGISTER_CHSTREAM 列表中无此项），但此问题在根因链 #1 修复后会自动消失（schema validation 失败前 instantiateAll 会先遇到 unknown type → 但已 log 不 abort）。

**Why now**：53+ 天 main CI 失败阻塞 code-format/PTX-EMU 之外的所有 Debug ASan 测试。修复后：
- `Build (Debug, ASan=ON)` 转绿
- main CI 5/5 通过
- 配套 `test_cpptlm_emulator_*` 测试覆盖真实 ABI 路径（不再是 NULL/stub 路径）

## What Changes

### 1. 配置修复（最小、根因）

在 `configs/dgpu_board_v1.json` 的 soc 子模块（line 32 `modules[0]`）添加 `"connections": []`。

理由：soc 子模块被当作完整 sub-config 传给 `ModuleFactory::instantiateAll`，需要满足顶层 config schema（modules + connections）。当前 JSON 实际只在顶层有 `connections: []`，soc 内部模块间无连接（实际数据路径由 `inputs/outputs` 表达），但 schema validator 仍然要求字段存在。

### 2. 测试健壮性增强（防御性）

`test/test_cpptlm_emulator_msix.cc` 与 `test/test_cpptlm_emulator_lookup_register.cc` 改用 RAII 包装 `cpptlm_emulator_t*`，确保 REQUIRE 失败抛出时仍执行 cleanup。

引入 `test/test_cpptlm_emulator_handle_helpers.hh` 提供 `EmulatorHandleGuard` RAII 类，封装 `cpptlm_emulator_create_by_id` + `cpptlm_emulator_destroy`，捕获异常安全释放。

### 3. ModuleFactory validate 容错（可选改进）

`src/core/module_factory_validate.cc::validateConfig` 对 `connections` 字段做"软警告"处理：缺失时 warning + 继续实例化（不 return false）。

**Trade-off**：
- 不改 → 严格 schema 校验，需要所有 config 写完整
- 改 → 容错，但隐藏 config 错误

**当前选择**：不修改（保持严格校验），仅修配置 + 测试。

## Capabilities

### New Capabilities

- `cpptlm-emulator-asan-cleanup`: cpptlm_emulator C ABI 测试在 ASan 下零内存泄漏；DGpuBoard 生命周期（create → use → destroy）在测试异常路径下完整清理。

### Modified Capabilities

- `dgpu-board-mvp`: `configs/dgpu_board_v1.json` 的 soc 子模块 schema 符合 `ModuleFactory::validateConfig` 要求（增加 `connections: []`）。
- `host-bypass-and-rc` (无 spec 改动，但 trace path 受影响)：test_cpptlm_emulator_dlopen 现在真正成功而非 NULL/stub 路径。

## Impact

- **受影响文件**（仅配置 + 测试）：
  - `configs/dgpu_board_v1.json` — soc 加 `"connections": []` (1 行)
  - `test/test_cpptlm_emulator_handle_helpers.hh` — 新文件 (RAII helper)
  - `test/test_cpptlm_emulator_msix.cc` — 改用 RAII helper (~10 行)
  - `test/test_cpptlm_emulator_lookup_register.cc` — 改用 RAII helper (~10 行)
- **不受影响**：`include/abi/cpptlm_emulator.h`、`src/abi/cpptlm_emulator.cc`、`include/tlm/gpu/dgpu_board_shell.hh`、`src/tlm/gpu/dgpu_board_shell.cc` 等产品代码（行为正确，仅 test 缺防御）。
- **CI 影响**：Build (Debug, ASan=ON) 从 fail 转 pass；main CI 5/5 100% pass。
- **不是 dlopen 测试的修复**：`test_cpptlm_emulator_dlopen` 走 `dlopen("libcpptlm_emulator.so")` 路径，与本 change 关注的 ABI wrapper 测试不同路径（dlopen 测试本身已 100% pass）。

## Alternative 方案对比

| 方案 | 工作量 | 修复彻底性 | 推荐度 |
|------|--------|----------|--------|
| **A. 修配置 + RAII helper（当前选）** | 2 文件 + 1 新文件 | 根本修复，配置 + 测试双层防御 | ⭐ 推荐 |
| B. 仅修配置（1 行 JSON） | 1 文件 | 修复当前失败，但不防未来类似测试 | 中等 |
| C. CI 加 `ASAN_OPTIONS=detect_leaks=0` | 1 行 CI | 绕过 CI 但不解决根因，掩盖问题 | 不推荐 |
| D. ModuleFactory 容错（让 connections 缺失不 abort） | 1 文件 | 隐藏配置错误，治标 | 不推荐 |
| E. 注册 TmuDispatchProcessorTLM | 1 行 + 类重构 | 配置 schema validation 已先失败，TMU 问题在根因 #1 修复后自动消失 | 不必要 |
