## Why

PTX-EMU 子模块已 bump 到 d5a58cf5（origin/main），该 commit 镜像了 HSK-9 spec（`ICOMPUTE_API_VERSION=1` SM rewrite）。但 CppTLM 端 `IComputeDevice`（`include/tlm/gpu/i_compute_device.hh`）**无 `ICOMPUTE_API_VERSION=1` 常量与真 static_assert**，PTX-EMU 侧 `device_api_impl.cc` 跨仓消费改造也全部未做。HSK-9 14 天反馈窗口已开启，需要在窗口关闭前完成 CppTLM 端契约钉死 + PTX-EMU 端 consumer 改造，闭环 HSK-9 跨仓契约。

## What Changes

- CppTLM 端 `include/tlm/gpu/i_compute_device.hh` 加 `#define ICOMPUTE_API_VERSION 1` + `static_assert(ICOMPUTE_API_VERSION == 1, ...)`（镜像 PTX-EMU 侧 `PTXEMU_API_VERSION` 模式），并把 L97 占位 `sizeof(...)>0` 断言替换为真实 15 虚方法计数检查。
- PTX-EMU 端 `src/ptxemu/device_api_impl.cc` 加 `set_instr_descriptor_buf()` 实现 + `attach_timing` 改 deprecated stub。
- PTX-EMU 端 `src/ptxsim/core/sm_context_cpptlm_inject.{h,cpp}` 移除 `IScoreboard*` / `IPipelineLatencyProvider*` / `ITensorCoreTiming*` 3 vendor 依赖并切到 `IComputeDevice::exe_once()` 路径。
- PTX-EMU 端 2 个 `attach_timing` 相关测试（`tests/integration/cpptlm/test_attach_timing_consumer_e2e.cpp` + `tests/unit/ptxemu/test_device_api_attach_timing.cpp`）重定位到 `tests/legacy-attach_timing/` 并标记 `[[deprecated]]`；实际数量以 owner ack 为准（HSK-9 公告写 5 是过时口径，d5a58cf5 实测仅 2）。
- 新增 `docs/cross_repo/HSK-9-2027-02-09-cpptlm-sm-rewrite.md` 镜像（兑现 archive proposal 承诺；引用 `docs/superpowers/specs/2027-02-09-hsk-9-icompute-api-v1-sm-rewrite.md` 为权威源）。
- 协调下一次 PTX-EMU submodule bump（`IComputeDevice::set_instr_descriptor_buf` + `attach_timing` deprecated stub 上线）。

## Capabilities

### New Capabilities

- `hsk9-icompute-api-v1-contract`: 跨仓 `ICOMPUTE_API_VERSION=1` 契约强制——CppTLM 端常量 + 真 static_assert + PTX-EMU 端版本对齐检查。
- `hsk9-ptxemu-consumer-impl`: PTX-EMU 端 `IComputeDevice` consumer 实现（`device_api_impl.cc::set_instr_descriptor_buf` + `attach_timing` deprecated stub + `sm_context_cpptlm_inject` 移除 3 vendor 依赖）。
- `hsk9-cross-repo-doc-mirror`: `docs/cross_repo/HSK-9-2027-02-09-cpptlm-sm-rewrite.md` 镜像文档，绑定 HSK 协议链。

### Modified Capabilities

（无——本 change 不改任何现有 capability 的 REQUIREMENTS。`sm-microarchitecture` / `pcie-ip-microarch` / `dgpu-board-mvp` 等 spec 仅引用层更新，不改行为。）

## Impact

- **CppTLM 端**:
  - `include/tlm/gpu/i_compute_device.hh`（加常量 + 替换占位断言）
  - `docs/cross_repo/HSK-9-2027-02-09-cpptlm-sm-rewrite.md`（新增）
  - `docs/superpowers/specs/HSK-9-baseline-tracker.md`（追加 consumer 子波条目）
- **PTX-EMU 端**:
  - `src/ptxemu/device_api_impl.cc`（加 `set_instr_descriptor_buf` + 改 `attach_timing` 为 deprecated stub）
  - `src/ptxsim/core/sm_context_cpptlm_inject.{h,cpp}`（移除 3 vendor 接口依赖）
  - `tests/legacy-attach_timing/`（新增目录容纳 2 个重定位测试，owner ack 后定数）
- **不触动**:
  - 23 ABI 冻结头（`include/abi/*`）
  - `IPtxEmuDevice` 12 方法签名（HSK-9 冻结）；`[[deprecated]]` 属性加在头文件声明（**非签名变更**，12 方法冻结保持）
  - `IComputeDevice` 15 方法签名（HSK-9 冻结）
  - 既有 24 个 `test_sm_*` 测试（Subwave 3 已绿 44766/1281）
- **追加（非修改）**:
  - `test/test_i_compute_device_interface.cc` 追加 1 个 TEST_CASE 验证 `ICOMPUTE_API_VERSION=1` 常量（与既有接口契约测试共存）
- **依赖/协调**:
  - 下一次 PTX-EMU submodule bump（HSK-9 14 天反馈窗口语义）
  - 与另一 open change `cpptlm-emulator-abi-contract-extension` 正交（后者明确排除 `include/tlm/gpu/**`）
  - 与 archive `cpptlm-dgpu-d1-cdna-isa-sm-rewrite` 不复活（Oracle verdict C；执行轨道在 `HSK-9-baseline-tracker.md` 子波 1/2/3 + PR #23/#24）
