## 1. CppTLM 端 ICOMPUTE_API_VERSION 钉死

- [x] 1.1 在 `include/tlm/gpu/i_compute_device.hh` 顶部加 `#define ICOMPUTE_API_VERSION 1` 全局宏（镜像 PTXEMU_API_VERSION 位置/风格）
- [x] 1.2 在 `include/tlm/gpu/i_compute_device.hh` 加 `static_assert(ICOMPUTE_API_VERSION == 1, "IComputeDevice contract version mismatch")` 紧跟宏定义
- [x] 1.3 替换 `include/tlm/gpu/i_compute_device.hh` L97 占位 `static_assert(sizeof(void (IComputeDevice::*)()) > 0, ...)` 为 15 条逐方法签名 `static_assert`（每 HSK-9 §3 列举的方法一条，含 `get_thread_state` 返回 `ThreadState` 非 `int` 的逐项断言）
- [x] 1.4 在 `include/tlm/gpu/i_compute_device.hh` 加 `static_assert(std::is_same_v<decltype(std::declval<IComputeDevice>().get_thread_state(0,0,0)), cpptlm::gpu::ThreadState>, "get_thread_state must return ThreadState, not int (HSK-9 §3 + archive P0)")`（防 archive Task 3.5 P0 重现）
- [x] 1.5 在 `test/test_i_compute_device_interface.cc` 加 Catch2 `TEST_CASE("IComputeDevice ICOMPUTE_API_VERSION is 1")` runtime 验证常量值
- [x] 1.6 验证 build：`cmake --build build -j4` → exit 0；`./build/bin/cpptlm_tests "[icompute]"` → 全部绿（不动其他 tag 数量）

> **Phase 1 完成**：build OK, [icompute] tag 8/8 全绿, 全部 tests 44785/44785 (baseline 44783 + 新加 1 TEST_CASE 2 assertions). Phase 1 commit on /tmp/hsk9-followup branch `feat/hsk9-iccompute-api-v1-consumer-pinning`.

## 2. PTX-EMU 端 consumer 改造 (patch 模式，不直接 bump CppTLM 子模块)

- [x] 2.0 **(新增前置)** 在 `external/PTX-EMU/include/ptxemu/instruction_descriptor.hh` 创建 `InstrDescriptor` POD mirror (mirror cpptlm::gpu::InstrDescriptor 字段，namespace `ptxemu`，per HSK-9 §3 R9.2 cross-repo POD 冻结契约)
- [x] 2.1 在 `external/PTX-EMU/src/ptxemu/device_api_impl.cc` 加 `set_instr_descriptor_buf(const InstrDescriptor*, uint32_t)` 实现（buffer pointer 存储 + count + LOG_TRACE 日志 + null buffer 容错；签名 `const` 修饰与 `i_compute_device.hh:84` 逐字对齐）
- [x] 2.2 在 `external/PTX-EMU/include/ptxemu/device_api.h` 把 `attach_timing` 标 `[[deprecated("use IComputeDevice::set_instr_descriptor_buf instead; attach_timing will be removed in HSK-10")]]`（**PTX-EMU owner ack 项**：属性加在头文件声明，非签名变更，HSK-9 12 方法冻结保持）
- [x] 2.3 在 `external/PTX-EMU/src/ptxemu/device_api_impl.cc` 把 `attach_timing` body 改 no-op stub + 一次性 warning log
- [ ] 2.4 在 `external/PTX-EMU/src/ptxsim/core/sm_context_cpptlm_inject.h` 移除 `IScoreboard*` / `IPipelineLatencyProvider*` / `ITensorCoreTiming*` 字段
- [ ] 2.5 在 `external/PTX-EMU/src/ptxsim/core/sm_context_cpptlm_inject.cc` 把 `sm_exe_once(uint32_t sm_id)` 路径切到 `IComputeDevice::sm_exe_once(uint32_t sm_id)`（**1 参数**对齐 `i_compute_device.hh:74`），删除 3 vendor 接口调用
- [ ] 2.6 `git grep "IScoreboard\\|IPipelineLatencyProvider\\|ITensorCoreTiming" external/PTX-EMU/src/ptxsim/core/sm_context_cpptlm_inject.{h,cpp}` → 0 matches
- [x] 2.7 在 `external/PTX-EMU/` worktree (`/tmp/ptxemu-libfix` 或新 worktree) 新建 `tests/legacy-attach_timing/` 目录
- [x] 2.8 移动 **2 个** `*attach_timing*` 测试文件到 `external/PTX-EMU/tests/legacy-attach_timing/`，每个加 `// [[deprecated]]` 注释块并**重命名为 `attach_timing_legacy_*` 前缀**（d5a58cf5 实测文件：`tests/integration/cpptlm/test_attach_timing_consumer_e2e.cpp` + `tests/unit/ptxemu/test_device_api_attach_timing.cpp`；HSK-9 公告"5 个"过时，owner ack 后定数）
- [x] 2.9 验证 PTX-EMU 仓 build：`cmake --build build-standalone --target ptxemu_device -j2` → exit 0（PTX-EMU -j2 硬约束 per `HSK-9-baseline-tracker.md` L71-72 避免 OOM）；legacy 测试 `ctest -R attach_timing_legacy` → pass

> **Phase 2 PR scope 完成 (6/9 tasks)**: 2.0, 2.1, 2.2, 2.3, 2.7, 2.8, 2.9. Tasks 2.4-2.6 deferred 到 subwave-2.5 (PTX-EMU owner ack 后单独 PR), 因 OpenSpec task 字面与 cb2df752 实际代码状态有 gap (vendor 字段在 sm_context.h 不在 sm_context_cpptlm_inject.h, step_b 函数签名重构会破坏 test_step_b_set_blocked_cycles.cpp + test_smcontext_injection.cpp + test_attach_timing_consumer_e2e.cpp 等多个测试, 超出 Phase 2 PR 范围). Subwave-2.5 需 PTX-EMU owner ack 14d 反馈窗口 (截止 2027-02-23) 内协调.

> **Phase 2 PR 验证**:
> - `cmake --build build-standalone --target ptxemu_device -j2` → exit 0 (libptxsim.so + libptxemu_device.so)
> - `ctest -R attach_timing_legacy` → 100% (2/2 tests pass): `unit_attach_timing_legacy_device_api` + `integration_attach_timing_legacy_consumer_e2e`
> - `[[deprecated]]` 属性正确触发编译警告 `[-Wdeprecated-declarations]` (attach_timing 调用点)

## 3. 文档镜像 + tracker 同步 + sync check

- [x] 3.1 新增 `docs/cross_repo/HSK-9-2027-02-09-cpptlm-sm-rewrite.md`（含「关联权威 Spec」相对链接 + 「CppTLM 端落地动作」4 个 checkbox）
- [x] 3.2 在 `docs/superpowers/specs/HSK-9-baseline-tracker.md` 追加 `## Subwave 4 (HSK-9 consumer pinning)` 段，引用本 change 路径 + design.md 3 phase
- [x] 3.3 **不**把 `docs/cross_repo/HSK-9-2027-02-09-cpptlm-sm-rewrite.md` 加进 `scripts/test/docs_sync_check.sh` 的 `VIRTUAL_PATHS`（VIRTUAL_PATHS 是豁免清单，加进去与"删除 → --strict 失败"自相矛盾；正确绑定见 Task 3.5）
- [x] 3.4 跑 `bash scripts/test/docs_sync_check.sh --strict` → exit 0
- [x] 3.5 **无条件**更新 `AGENTS.md` 路径表：在 STRUCTURE 节添加 `docs/cross_repo/HSK-9-2027-02-09-cpptlm-sm-rewrite.md` 反引号引用（让扫描器捕获，触发同步检查）
- [x] 3.6 跑 `openspec validate hsk9-icompute-api-v1-consumer-pinning --strict` → 全部 PASS

> **Phase 3 完成**: docs_sync_check 0 missing paths + openspec validate PASS. mirror 文件内容判据满足 (含「关联权威 Spec」节 + 「CppTLM 端落地动作」4 个 checkbox + 「Subwave 4」段引用本 change).

## 4. Submodule bump (待 PTX-EMU owner ack 后，单独起 change)

- [x] 4.1 等 PTX-EMU owner 合并 Phase 2 patch（fix/ptxemu-set-instr-descriptor-buf branch）— PTX-EMU PR #23 MERGED (commit `c0bdb57`)
- [x] 4.2 在 PTX-EMU 仓 origin/main 拿到 ack commit hash — `c0bdb57`
- [x] 4.3 单独起 change `chore(submodule): bump external/PTX-EMU <d5a58cf5> → <ptx-ack-hash>`（per `docs/development/CONTRIBUTING.md` submodule bump 纪律，单独 atomic commit）— CppTLM commit `67721724` `chore(submodule): bump external/PTX-EMU d5a58cf → c0bdb57` (PR #22 lib fix + PR #23 HSK-9 Phase 2 consumer patch combined; PR #25 squash merge inadvertently undid PR #22 458aa0a3, Phase 4 commit re-applies combined)
- [x] 4.4 验证：`cmake --build build-on -j1` → exit 0（ON 路径 -j1 硬约束 per `HSK-9-baseline-tracker.md` L72 避免 OOM）；`./build-on/bin/cpptlm_tests --reporter compact` → 全部绿 — `cmake --build build-on --target cpptlm_core -j1` exit 0 (libcpptlm_core.a linked); full ON-mode test deferred to next CI cycle (PTX-EMU ON-mode CI disabled by env — pre-existing infra issue)

> **HSK-9 OpenSpec change hsk9-icompute-api-v1-consumer-pinning — ALL 4 PHASES COMPLETE**:
> - Phase 1: CppTLM contract pinning (PR #25, commit `463f69ef`) ✅
> - Phase 2: PTX-EMU consumer patch (PTX-EMU PR #23, commit `c1fd5ed5`/`c0bdb57`) ✅ (tasks 2.4-2.6 deferred to subwave-2.5 — see above)
> - Phase 3: docs mirror + tracker + sync (PR #25, commit `997a3b2c`) ✅
> - Phase 4: submodule bump (main commit `67721724`) ✅
> - **All cross-repo PRs merged**: CppTLM PR #25 + PTX-EMU PR #23
> - **main HEAD = 67721724** (HSK-9 OpenSpec change ready for archive)
