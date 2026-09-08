## Context

PTX-EMU 子模块在 d5a58cf5（HSK-9 spec mirror）已经向前推进到 `ICOMPUTE_API_VERSION=1` 阶段。PTX-EMU 端 `device_api.h` 已加 `PTXEMU_API_VERSION 1` 宏 + `static_assert(PTXEMU_API_VERSION == 1, ...)`，但 CppTLM 端 `IComputeDevice`（`include/tlm/gpu/i_compute_device.hh`）**没有镜像的 `ICOMPUTE_API_VERSION=1` 常量**，L97 仅占位 `sizeof(...)>0` 无意义断言。

HSK-9 公告（`docs/superpowers/specs/2027-02-09-hsk-9-icompute-api-v1-sm-rewrite.md`）明确冻结 15 个虚方法签名 + `IPtxEmuDevice` 12 方法签名不变 + `attach_timing` 保留为 deprecated stub，PTX-EMU 端 `set_instr_descriptor_buf` / `sm_context_cpptlm_inject` 改造 + 5 attach_timing 测试重定位都是 PTX-EMU 仓 side work（已写进 d5a58cf5 spec 但代码未实现）。

本 change 关联的 archive `2026-09-05-cpptlm-dgpu-d1-cdna-isa-sm-rewrite` 21 tasks 实际只完成 3 个，Task 3.5 P0（`get_thread_state` 返回 `int` 应为 `ThreadState`）从未应用；该 archive 不复活（Oracle 评审 verdict C），执行轨道在 `HSK-9-baseline-tracker.md` 子波 1/2/3 + PR #23/#24。本 change 仅承接 HSK-9 跨仓契约钉死 + PTX-EMU 消费端改造的**窄范围**收尾工作（5-8 工作日，per Oracle 评审）。

## Goals / Non-Goals

**Goals:**
- 在 CppTLM 端 `IComputeDevice` 钉死 `ICOMPUTE_API_VERSION=1` 跨仓版本号 + 真实 15 虚方法签名逐条 static_assert
- 在 PTX-EMU 端实现 `set_instr_descriptor_buf()` + 把 `attach_timing` 改为 deprecated stub（PTX-EMU owner ack 项）
- 移除 PTX-EMU 端 `sm_context_cpptlm_inject` 的 3 vendor 接口依赖
- 2 个 PTX-EMU attach_timing 测试重定位到 `tests/legacy-attach_timing/` 并标 `[[deprecated]]`（d5a58cf5 实测仅 2，HSK-9 公告"5 个"过时；owner ack 后定数）
- 协调下一次 PTX-EMU submodule bump 让 PTX-EMU 侧改动落地
- 新增 `docs/cross_repo/HSK-9-2027-02-09-cpptlm-sm-rewrite.md` 镜像文档
- `HSK-9-baseline-tracker.md` 追加 consumer 子波条目

**Non-Goals:**
- 不修改 `IComputeDevice` 15 虚方法签名（HSK-9 冻结）
- 不修改 `IPtxEmuDevice` 12 方法签名（HSK-9 冻结）
- 不修改 23 ABI 冻结头（`include/abi/*`）
- 不复活 archive `2026-09-05-cpptlm-dgpu-d1-cdna-isa-sm-rewrite`
- 不实现 SM functional tail（MatrixCore MFMA 20 指令真值 / LsuLDS bank-conflict 检测 / BitExactGate / 11-preserved 7 个 stub 补齐）—— 留给 `HSK-9-baseline-tracker.md` 子波 4+
- 不删 `include/tlm/gpu/{wavefront,minimal_warp_scheduler,vector_regfile}_tlm.hh` 3 个 deprecated 头（Task 16 物理删除属于 archive tail，本 change 不动）

## Decisions

### Decision 1: 版本号常量放在 `i_compute_device.hh` 顶部命名空间 `cpptlm::gpu` 外部

镜像 PTX-EMU 侧 `PTXEMU_API_VERSION` 的位置（`include/ptxemu/device_api.h` L24，全局宏），CppTLM 端 `ICOMPUTE_API_VERSION` 也作为全局 `#define`，紧跟 `#include "i_compute_device.hh"` 之前。**备选**：放在 `cpptlm::gpu` 命名空间内。**选全局**——与 PTXEMU_API_VERSION 对称，跨仓查找更直接。

### Decision 2: `attach_timing` 在 PTX-EMU 侧改 deprecated stub 而非删除

HSK-9 公告显式冻结 `IPtxEmuDevice` 12 方法签名**不变**。`attach_timing` 在 `external/PTX-EMU/include/ptxemu/device_api.h` 公共头**声明处**加 `[[deprecated("use IComputeDevice::set_instr_descriptor_buf instead; attach_timing will be removed in HSK-10")]]` 属性（**属性非签名变更**，12 方法冻结保持），body 改 no-op stub。**备选**：直接删除 `attach_timing`。**选 deprecated stub + 头文件属性**——保持 ABI 表面冻结（HSK-9 协调要求），给下游已链接的旧代码留过渡期，同时让编译器在调用点 emit `[[deprecated]]` 警告。**Owner ack 项**：头文件添加 `[[deprecated]]` 需 PTX-EMU owner ack（不破坏 HSK-9 冻结契约，但属于公共头变更）。

### Decision 3: 5 个 attach_timing 测试重定位到 `tests/legacy-attach_timing/` 而非删除

测试源码保留但加 `[[deprecated]]` 标签 + 目录前缀 `legacy-`，跑通但 emit deprecation warning。**备选**：直接删除。**选重定位**——PTX-EMU 旧下游可能依赖这些测试作为参考实现；deprecation 周期给社区迁移缓冲。

### Decision 4: 镜像文档放在 `docs/cross_repo/` 而非 `docs/superpowers/specs/`

HSK-9 权威 spec 已在 `docs/superpowers/specs/2027-02-09-hsk-9-icompute-api-v1-sm-rewrite.md`（Oracle Round 3 评审 + 14 天反馈窗口 + 已发布渠道），archive proposal 承诺的"新增 docs/cross_repo/HSK-9 镜像"是绑定 HSK 协议链的**操作层文档**（只列 CppTLM 端落地动作 + 跨仓协调清单），不是重复 spec。**备选**：复制到 `docs/superpowers/specs/`。**选 `docs/cross_repo/`**——避免权威 spec 重复，操作清单与权威 spec 单一来源分离。

### Decision 5: 不修改 `i_compute_device.hh` 15 方法顺序 / 签名 / 命名

HSK-9 §3 表逐字列举 15 方法顺序与签名，PTX-EMU 端 `IPtxEmuDevice` 12 个 preserved 签名同构。**任何 reorder / rename / signature change 都违反 HSK-9 协调纪律**，必须开新 HSK 公告（例如 HSK-10）才能动。

## Risks / Trade-offs

- **[Risk] HSK-9 14 天反馈窗口（截止 2027-02-23）** — CppTLM 端 ICOMPUTE_API_VERSION 钉死必须早于窗口关闭；PTX-EMU 端 consumer 改造建议下一窗口（HSK-10 触发）落地。**Mitigation**：本 change 分两阶段 commit，CppTLM 端先合（contract enforcement），PTX-EMU 端协调 submodule bump 后合。
- **[Risk] PTX-EMU submodule bump 协调失败** — 5 个 attach_timing 测试重定位 + set_instr_descriptor_buf 实现属 PTX-EMU 仓 owner ack 范围，需 PTX-EMU 端 PR review。**Mitigation**：本 change 的 PTX-EMU 端 commit 在 `external/PTX-EMU/` 内**仅准备 patch**（`fix/ptxemu-set-instr-descriptor-buf` branch），不直接 bump CppTLM 子模块；待 PTX-EMU owner 合并后另起 submodule bump change。
- **[Risk] 镜像文档漂移** — `docs/cross_repo/HSK-9-2027-02-09-cpptlm-sm-rewrite.md` 与权威 `docs/superpowers/specs/2027-02-09-hsk-9-icompute-api-v1-sm-rewrite.md` 长期可能不同步。**Mitigation**：在 `AGENTS.md` STRUCTURE 路径表中**添加**对镜像文档的反引号引用（让 `docs_sync_check.sh` 扫描器捕获），并在执行 Phase 3 Task 3.5 时**无条件**完成 AGENTS.md 更新（不是条件性）。**注意**：`scripts/test/docs_sync_check.sh` 的 `VIRTUAL_PATHS` 数组是**豁免清单**（路径不在扫描范围），不应把镜像路径加进去——会与"删除镜像 → --strict 失败"的检测逻辑自相矛盾。
- **[Risk] Oracle 没在 spec 评审** — 本 change 的 design.md 未走 Oracle Round 3 评审。**Mitigation**：scaffold 建好后发 rdd-builder P1 走 Oracle review。

## Migration Plan

**Phase 1 (本 change commit 1, 1d)**: CppTLM 端 ICOMPUTE_API_VERSION 钉死
- `include/tlm/gpu/i_compute_device.hh` 加 `#define ICOMPUTE_API_VERSION 1` + 替换占位 static_assert
- `test/test_i_compute_device_interface.cc` 加 1 个 test case 验证常量值
- commit: `feat(hsk9): pin ICOMPUTE_API_VERSION=1 contract in IComputeDevice`

**Phase 2 (本 change commit 2, 2-3d)**: PTX-EMU 端 consumer 改造（patch 模式）
- `external/PTX-EMU/src/ptxemu/device_api_impl.cc` 加 `set_instr_descriptor_buf` 实现 + 改 `attach_timing` 为 deprecated stub
- `external/PTX-EMU/src/ptxsim/core/sm_context_cpptlm_inject.{h,cpp}` 移除 3 vendor 依赖
- `external/PTX-EMU/tests/legacy-attach_timing/` 新建 + 5 测试重定位
- commit: `feat(hsk9): PTX-EMU consumer impl for ICOMPUTE_API_VERSION=1`
- **不直接 bump CppTLM 子模块指针**，等 PTX-EMU owner ack 后另起 change

**Phase 3 (本 change commit 3, 0.5d)**: 文档镜像 + tracker 同步
- `docs/cross_repo/HSK-9-2027-02-09-cpptlm-sm-rewrite.md` 新增（操作层）
- `docs/superpowers/specs/HSK-9-baseline-tracker.md` 追加 consumer 子波
- `scripts/test/docs_sync_check.sh` 加 HSK 镜像文档路径检查
- commit: `docs(hsk9): cross-repo mirror + tracker subwave entry + sync check`

**Phase 4 (separate change, post-ack)**: submodule bump
- 待 PTX-EMU owner 合并 Phase 2 patch 后，起新 change `chore(submodule): bump external/PTX-EMU <d5a58cf5> → <ptx-ack-hash>`

**Rollback**:
- Phase 1 兼容（仅加常量 + 替换占位断言），无 ABI 变化，revert 单 commit 即可
- Phase 2 是 PTX-EMU 仓 side，rollback 走 PTX-EMU 标准 revert
- Phase 3 文档类，独立 revert 安全

## Open Questions

- ICOMPUTE_API_VERSION=1 的 C++ 头文件宏定义还是 constexpr 变量？倾向 `#define`（与 PTXEMU_API_VERSION 一致）；最终走 Oracle P1 评审。
- PTX-EMU 端 `set_instr_descriptor_buf` 实现是否同步到 master（d5a58cf5 之后）还是需要新 commit hash？等 PTX-EMU owner ack。
- 5 个 attach_timing 测试中是否有 1 个或多个已经 `[[deprecated]]` 标记（可能在 HSK-6 桥接废止阶段已加）？需要 PTX-EMU 仓端 `git log` 核对。
- 镜像文档放在 `docs/cross_repo/` 是否会被 `scripts/test/docs_sync_check.sh` 当前 VIRTUAL_PATHS 规则误识别为 orphan？需先跑 docs_sync_check.sh 验证。
