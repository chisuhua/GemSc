# Proposal: cpptlm-emulator-abi-contract-extension

## Why

UsrLinuxEmu 的 sim_hardware mock 后端测试补全提案（2026-09-07-sim-hardware-mock-test-completion，archived）已落地；其 design.md D6 记录了 UsrLinuxEmu CpptlmBridge 与 CppTLM C ABI 的 1:1 映射，并显式延后"CppTLM 侧 ABI 契约测试"到 Change-3 规划期。当前 CppTLM 端 `test/test_cpptlm_emulator_abi.cc`（6 用例）+ `test/test_cpptlm_emulator_msix.cc`（4 用例）= 10 用例，全部在 NULL/stub/deferred shell 路径——**DGpuBoard shell 真实数据路径的 ABI 集成测试当前 0 覆盖**。本提案做"前置可做"的契约扩展（参数校验、registry 并发、callback 触发链、ABI 表面一致性 characterization），将 DGpuBoard e2e 部分显式记入 T-bs-4 后续工作，从源头消除"ABI 契约只在 stub 层"的盲区。

## What Changes

- 扩展 `test/test_cpptlm_emulator_abi.cc`（+用例）：参数边界（mmio invalid bar 范围、offset/len 校验、pcie_config width ∈ {1/2/4}、backdoor 边界）、registry 并发安全（多线程 cpptlm_emulator_get_device_count）、create/destroy null 句柄重复 destroy。
- 新增 callback 触发链测试（受限于 DGpuBoard shell deferred）：在 shell 未暴露前通过 cpptlm_intr_deliver_cb_t 注册 + 直接调用 wrapper 函数（参见 `cpptlm_emulator.cc:10` 注释：shell deferred 时 wrapper 用 std::function + lambda 捕获 user_ctx）验证注册路径本身不出错；callback 实际触发留在 T-bs-4 follow-up。
- 扩展 `test/test_cpptlm_emulator_msix.cc`（+用例）：msix_init 非法 table_size（0、>2048）、mask 范围、update/clear_pending 的 vector_id 越界负例。
- 显式记录"DGpuBoard shell e2e 数据路径测试"为 deferred：在 `test/test_cpptlm_emulator_abi.cc` 加注释指针到 T-bs-4（`cpptlm_emulator.cc:7-10` 已注明 shell deferred）。
- **无产品代码改动**（characterization + 错误路径补充测试，仅当现有实现缺少参数校验时可能需要补实现——优先做测试，红灯部分另立 follow-up change）。

## Capabilities

### New Capabilities

- `cpptlm-emulator-abi-contract-tests`: CppTLM 19-函数 C ABI 的契约测试集合，覆盖 NULL 句柄（已有）、参数边界校验、registry 并发安全、callback 注册路径、msix 错误码路径——在 DGpuBoard shell 完整化之前的可做范围；shell 完整化后 e2e 数据路径另立 follow-up。

### Modified Capabilities

（无——现有 capability `host-bypass-and-rc`、`dgpu-board-mvp` 等 requirement 不变；本 change 仅扩展 ABI 契约测试覆盖。）

## Impact

- **受影响代码**（仅测试）：
  - `test/test_cpptlm_emulator_abi.cc`（扩展 +用例 + DGpuBoard-deferred 注释指针）
  - `test/test_cpptlm_emulator_msix.cc`（扩展 +用例）
- **不受影响**：`include/abi/cpptlm_emulator.h`、`src/abi/cpptlm_emulator.cc`、`include/tlm/gpu/**`、`src/tlm/gpu/**`（DGpuBoard shell deferred 至 T-bs-4，本 change 不动）。
- **跨仓影响（UsrLinuxEmu）**：无。本提案在 CppTLM 仓独立落地；UsrLinuxEmu 端 sim_hardware mock 测试已归档，无需联动。
- **CI**：现有 Catch2 `cpptlm_tests` 二进制（`file(GLOB test_*.cc)` 自动发现）增加用例数；测试运行 `ctest` 或 `./build/bin/cpptlm_tests "[abi]"`。
- **依赖关系**：依赖 `cpptlm_emulator` SHARED 库（已构建）；无新外部依赖。
- **后置触发**：DGpuBoard shell 完整化（T-bs-4 follow-up）后，应在 CppTLM 仓另立 `cpptlm-emulator-dgpu-e2e-tests` change 补真实数据路径测试；本 change 是前置。

## 关键修正说明

我在 UsrLinuxEmu 提案（archived 2026-09-07）的 design.md D6 曾误判"CppTLM 端 ABI 契约测试零覆盖"——实际有 10 个测试，但全部在 stub/NULL 路径。本提案是对该判断的纠正 + 实质性补全，**而非新增测试基线**。
