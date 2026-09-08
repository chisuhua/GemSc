# Spec: cpptlm-emulator-asan-cleanup (delta)

## ADDED Requirements

### Requirement: Configuration schema compliance
`configs/dgpu_board_v1.json` 的 soc 子模块 MUST 满足 `ModuleFactory::validateConfig` 的 schema 要求（含 `modules` array + `connections` array）。

#### Scenario: validateConfig passes for soc submodule
- **WHEN** `ModuleFactory::instantiateAll(soc_cfg)` 被调用，soc_cfg 是 soc 子模块配置
- **THEN** 所有必需字段（`modules`, `connections`）必须存在且类型正确
- **AND** `validateConfig` 返回 true
- **AND** 子模块（如 pcie_ep）正确 instantiate

### Requirement: ASan zero-leak guarantee for cpptlm_emulator tests
在 ASan Debug build 下，所有 `test_cpptlm_emulator_*` 测试 MUST 零内存泄漏。

#### Scenario: ASan reports zero leaks after fix
- **WHEN** 运行 `ASAN_OPTIONS=detect_leaks=1 ./bin/cpptlm_tests "[abi][msix][t-w3-3]"`
- **THEN** ASan 报告 `0 bytes leaked in 0 allocation(s)`
- **AND** 无 `Direct leak` / `Indirect leak` 报告

### Requirement: Exception-safe cleanup
测试中任何 REQUIRE/CHECK 失败抛 Catch2 异常时，DGpuBoard 资源 MUST 正确释放（不依赖测试函数后续代码执行）。

#### Scenario: REQUIRE failure triggers cleanup
- **WHEN** 测试函数创建 emu 后 REQUIRE 失败抛异常
- **THEN** RAII 包装自动调用 `cpptlm_emulator_destroy(emu)`
- **AND** board->shutdown() 执行
- **AND** sim_thread join 完成
- **AND** soc_ 析构（释放所有子 SimModule）
- **AND** ASan 零泄漏

### Requirement: API contract preservation
`cpptlm_emulator_create_by_id` + `cpptlm_emulator_destroy` 配对语义 MUST 保持不变（无 API 改动）。

#### Scenario: API signature unchanged
- **WHEN** 现有 cpptlm_emulator C ABI 调用方代码升级到修复后版本
- **THEN** 不需修改调用方代码
- **AND** ABI 函数签名/行为不变
