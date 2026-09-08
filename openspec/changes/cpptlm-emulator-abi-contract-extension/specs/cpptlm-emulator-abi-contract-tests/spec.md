# cpptlm-emulator-abi-contract-tests Delta

## ADDED Requirements

### Requirement: ABI 参数边界校验契约
`cpptlm_emulator_*` 19 函数 MUST 在参数越界/非法时返回 -EINVAL（NULL 句柄既有契约见 `test_cpptlm_emulator_abi.cc` 已覆盖）；新增契约覆盖 mmio/config/backdoor/msix 的非 NULL 句柄参数边界。

#### Scenario: pcie_config 非法 width 拒绝
- **WHEN** 对合法 emu 句柄调用 `cpptlm_emulator_pcie_config_write/read(emu, 0, width=3, &v)`（合法 width ∈ {1,2,4}）
- **THEN** 返回 -EINVAL

#### Scenario: backdoor NULL buf + len>0 拒绝
- **WHEN** 对合法 emu 调用 `cpptlm_emulator_backdoor_read/write(emu, bar, off, nullptr, len>0)`
- **THEN** 返回 -EINVAL

#### Scenario: msix_init 非法 table_size 拒绝
- **WHEN** 调用 `cpptlm_emulator_msix_init(emu, table_size=0, mask)` 或 `table_size>2048`
- **THEN** 返回 -EINVAL

#### Scenario: msix_update/clear_pending 非法 vector_id 拒绝
- **WHEN** 调用 `cpptlm_emulator_msix_update_pending(emu, vector_id >= table_size)` 或 `clear_pending` 同
- **THEN** 返回 -EINVAL

### Requirement: Registry 并发安全契约
`cpptlm_emulator_get_device_count` 与 `cpptlm_emulator_get_device_info` MUST 在 device lifecycle 抖动（并发 create/destroy）期间保持线程安全——reader 不崩溃、返回的 info 在该 dev_id 有效时正确、无效时返回 -ENOENT。

#### Scenario: 多 reader + 单 writer 并发安全
- **WHEN** 4 reader 线程各 1000 次调用 `cpptlm_emulator_get_device_count` 与 `get_device_info`，期间 1 writer 线程反复 `create` + `destroy`
- **THEN** reader 无崩溃、无崩溃；reader 捕获的 info 在对应 dev_id 有效时各字段合法，无效时返回 -ENOENT（线程内 atomic 计错，主线程断言；per UsrLinuxEmu archived design.md D3 线程断言纪律）

### Requirement: create/destroy NULL 句柄与重复 destroy 安全
`cpptlm_emulator_destroy` MUST 对 NULL 句柄与同一 emu 句柄重复 destroy 安全（no-op 或幂等），不崩溃、不泄漏。

#### Scenario: destroy(NULL) 安全
- **WHEN** 调用 `cpptlm_emulator_destroy(nullptr)`
- **THEN** 无崩溃（no-op）

#### Scenario: 重复 destroy 同一 emu 句柄
- **WHEN** 同一 `cpptlm_emulator_t*` 连续调用 `cpptlm_emulator_destroy` 两次（中间无重新 create）
- **THEN** 第二次不崩溃（no-op 或幂等）

### Requirement: Callback 注册路径 smoke 测试
`cpptlm_emulator_register_callbacks` MUST 支持 4 类 callback + user_ctx 注册，注册成功且后续 destroy 不导致 user_ctx 悬空访问。**Callback 实际触发链路（dGPU → MSI-X → registered intr_cb）属于 T-bs-4 deferred**（per `cpptlm_emulator.cc:7-10` 注释，DGpuBoard shell 未暴露）；待 shell 完整化后在 CppTLM 仓另立 follow-up change。

#### Scenario: 注册 4 类 callback 成功
- **WHEN** 对合法 emu 调用 `cpptlm_emulator_register_callbacks(emu, intr_cb, err_cb, reset_cb, power_cb, &ctx)`，全部非 NULL
- **THEN** 返回 0（或已注册标记）

#### Scenario: 注册后 destroy 不导致 user_ctx 悬空
- **WHEN** 注册 callback + user_ctx 后立即 `cpptlm_emulator_destroy(emu)`
- **THEN** 无崩溃（per-handle storage 由 std::function + lambda 捕获 user_ctx 保证生命周期；per `cpptlm_emulator.cc:10` 注释）

#### Scenario: 重复注册覆盖前次
- **WHEN** 对同一 emu 两次注册 4 类 callback（user_ctx 不同）
- **THEN** 第二次返回 0 且后续触发应使用第二次的 user_ctx（实际触发验证 deferred 至 T-bs-4）

### Requirement: ABI 表面 link-time 一致性 characterization
**T-bs-4-deferred**（per `cpptlm_emulator.cc:7-10` 注释）：DGpuBoard shell 未暴露 msix_*/lookup_register 的真实 dGPU 数据路径实现，本 change 不测试真实 mmio round-trip / config space round-trip / callback 触发链路。已有 link-time 验证（`test_cpptlm_emulator_abi.cc` "ABI: 23 symbols are linked"）保持作为 fallback；待 shell 完整化后在 CppTLM 仓另立 `cpptlm-emulator-dgpu-e2e-tests` change 补 e2e 数据路径契约。

#### Scenario: 19 ABI 符号链接验证（保持现有契约）
- **WHEN** 链接 `cpptlm_emulator` SHARED 库构建测试二进制并取函数指针
- **THEN** 19 个符号全部非 NULL（已有 `test_cpptlm_emulator_abi.cc:97-182`）
