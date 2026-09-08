# Design: cpptlm-emulator-abi-contract-extension

## Context

UsrLinuxEmu 的 2026-09-07 archived change 显式延后"CppTLM ABI 契约测试"到本 change。Cpptlm_emulator.h 19 函数 ABI（v1.0-dgpu-v0 冻结，ADR-088 §D5 + ADR-SOC-07 D5）当前测试覆盖：

| 测试文件 | 用例数 | 覆盖范围 |
|---------|-------|---------|
| `test/test_cpptlm_emulator_abi.cc` | 6 | version、registry、NULL 句柄、create/destroy lifecycle、link-time 验证、msix/lookup ENOSYS |
| `test/test_cpptlm_emulator_msix.cc` | 4 | msix_init/update_pending/clear_pending/lookup_register forwards |

**关键限制**：`cpptlm_emulator.cc:7-10` 注释明确：
> DGpuBoard shell 当前不直接暴露 msix_*/lookup_register 方法（deferred T-bs-4 follow-up）；set_*_callback 使用 std::function + 不带 user_ctx，与 ABI 函数指针签名不匹配，这里用 lambda 捕获 user_ctx（per-handle storage）

→ 真实 dGPU 数据路径（mmio round-trip 到 board、config space round-trip、callback 实际触发）当前不可测。本 change 做"shell 完整化之前能做的范围"。

## Goals / Non-Goals

**Goals:**
- 扩展 ABI 契约测试：参数边界（mmio invalid bar/offset/len、pcie_config width ∈ {1/2/4}、backdoor 边界、msix table_size/mask/vector 边界）
- Registry 并发安全（多线程 `get_device_count` + `get_device_info`）
- create/destroy NULL 句柄与重复 destroy（no-op 路径）
- Callback 注册路径 smoke test（不验证实际触发——T-bs-4 deferred）
- msix 错误码路径（NULL 句柄已有，扩展参数边界）
- 显式 `// T-bs-4-deferred:` 注释指针：DGpuBoard e2e 数据路径测试待 shell 完整化后另立 change

**Non-Goals:**
- 不实现 DGpuBoard shell 暴露（T-bs-4 follow-up）
- 不动 `src/abi/cpptlm_emulator.cc` 产品代码（除非测试发现真实参数校验缺失——若如此升为 follow-up change 而非本 change 混入）
- 不改 ABI 签名（19 函数冻结 v1.0-dgpu-v0）
- 不做 performance/stress 基线（per 设计独立提案）
- 不写 callback 实际触发测试（DGpuBoard shell deferred）

## Decisions

### D1: 全部 characterization-first
现有 ABI 实现已有基本 NULL 句柄校验（参见 `cpptlm_emulator.cc:42-66`）。新增参数边界测试若发现实现缺少校验 → 红灯 → 升级为 follow-up change（不混进本 change）。备选：直接改实现补校验 + 同步测试——被否：本 change 单测定位。

### D2: Registry 并发安全测试
现有 `cpptlm_emulator.cc:43-51` registry 用 `std::mutex registry_mu_` + `std::unordered_map` + `std::atomic next_dev_id_`。测试：
- 4 reader × 1000 reads 调用 `cpptlm_emulator_get_device_count` / `get_device_info`
- 1 writer 在 reader 期间 `create` + `destroy`（模拟 device lifecycle 抖动）
- 断言：reader 无崩溃、无不一致（info 返回的 dev_id 来自 create 后的有效 entry；destroy 后该 dev_id 再 get_device_info → -ENOENT）
- 线程内 atomic 计错，主线程断言（Catch2 线程断言纪律——见 UsrLinuxEmu archived design.md D3）

### D3: Callback 注册 smoke test（不验证触发）
callback 实际触发依赖 DGpuBoard shell 暴露（deferred）。本 change 只验证：
- `cpptlm_emulator_register_callbacks` 注册 4 类 callback + user_ctx 不返回错误
- 注册后立即 destroy device 不 crash（user_ctx 生命周期安全——per-handle storage，参见 `cpptlm_emulator.cc:10` 注释）
- 显式标记"callback 实际触发"为 T-bs-4 deferred（用 `// T-bs-4-deferred:` 注释）

### D4: 参数边界用例来源
ABI 头文件契约 + cpptlm_emulator.cc:100+ 实现逻辑交叉：
- `mmio_read/write`：emu NULL → -EINVAL 已有（test line 50-51）；新加：emu 非 NULL 但 load_soc_config 失败场景（emulator 返回 nullptr）→ 通过 `create(nullptr)` 后立即用返回的 emu 调用 mmio，期望 -EINVAL 或 -ENODEV（视实现而定，characterization 测出实际行为）
- `pcie_config_*` width 参数：1/2/4 合法范围之外（如 width=3）→ 期望 -EINVAL
- `backdoor_*`：NULL buf + len>0 → -EINVAL
- `msix_init`：table_size 0 / >2048 → -EINVAL（per spec REQ ABI）
- `msix_update/clear_pending`：vector_id 越界 → -EINVAL

### D5: cpptlm_emulator_create 行为依赖文件系统
`cpptlm_emulator.cc:53-72` resolve_profile_path 扫描 `configs/dgpu_board_*.json` 和 fallback `configs/dgpu_board_v1.json`。测试如依赖 profile 文件存在 → flakiness。新加测试应在每个 TEST_CASE 内显式 cwd 或 mock profile——**简单做法**：所有 create 测试在 `cd build/bin && ./bin/cpptlm_tests` 上下文运行，但 README 说 cpptlm_tests 是 Catch2 单二进制。备选：所有 create 测试断言接受 nullptr 或无效 profile 路径导致 create 返回 nullptr 的现状，并 characterizate 此行为。

## Risks / Trade-offs

- [DGpuBoard shell 仍 deferred → 真实 e2e 数据路径测试不可做] → 接受；本 change 明示 T-bs-4 follow-up 触发条件。
- [参数边界测试若发现实现缺校验 → 红灯 → 升 follow-up] → 接受；本 change 不混入实现改动。
- [cpptlm_emulator_create 依赖 cwd/profile 文件 → 测试 flakiness] → 所有 create 测试基于现有测试先例（test_cpptlm_emulator_abi.cc:67-81 已有 create/destroy lifecycle），延续同一上下文假设。
- [Registry 并发测试在 TSan 下可能暴露 mutex 覆盖不足] → 实施后立即跑 TSan；报警则升 follow-up。
- [Callback smoke test 仅验证注册路径，不验证触发] → 注释明示 deferred，避免 false confidence。

## Migration Plan

纯测试扩展：构建 → ctest/build/bin/cpptlm_tests `[abi]` 通过即完成；无部署/回滚。回滚 = revert 测试文件。

## Open Questions

（无——DGpuBoard shell deferred 是已知事实，per `cpptlm_emulator.cc:7-10` 注释。本 change 范围明确缩到 shell 完整化之前能做的部分。）
