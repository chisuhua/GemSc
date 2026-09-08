# HSK-9 — `ICOMPUTE_API_VERSION=1` SM 重构版 (CppTLM 端镜像)

> **镜像来源**: `docs/superpowers/specs/2027-02-09-hsk-9-icompute-api-v1-sm-rewrite.md` (权威 Spec)
> **镜像状态**: 同步发布于 HSK-9 Phase 1-2 完成后 (2027-02-09)
> **OpenSpec change**: [`hsk9-icompute-api-v1-consumer-pinning`](../changes/hsk9-icompute-api-v1-consumer-pinning/proposal.md)

## 关联权威 Spec

- 权威 Spec 路径: `docs/superpowers/specs/2027-02-09-hsk-9-icompute-api-v1-sm-rewrite.md` (CppTLM 仓内权威版本)
- 跨仓 Spec (PTX-EMU 端): `external/PTX-EMU/docs/superpowers/specs/2027-02-09-hsk-9-icompute-api-v1-sm-rewrite.md` (如果存在)
- 触发阶段: SM 重构 Gate 后 (per `architecture/15-sm-microarchitecture-design.md` §15.6.3)
- 14 天协调窗口: 2027-02-09 → 2027-02-23

## 摘要

CppTLM 端 GPU 算力侧重构为 SM 微架构 (`IComputeDevice` 15 方法 + 12 子模块 + 8 Bundle),
删除 3 vendor 接口依赖 (PipelineTLM / ScoreboardTLM / TensorCoreTLM), 引入
`set_instr_descriptor_buf` 同步通道。PTX-EMU 端配套改造 `attach_timing` deprecated
stub + producer-side `set_instr_descriptor_buf`。

## CppTLM 端落地动作

- [x] **Phase 1 — IComputeDevice 契约钉死** (PR #25 commit `463f69ef`, 2027-02-09)
  - `include/tlm/gpu/i_compute_device.hh`: 加 `ICOMPUTE_API_VERSION=1` 宏 + 15 条逐方法签名 `static_assert` (per HSK-9 §3 + Oracle Round 4 Fix 5)
  - `test/test_i_compute_device_interface.cc`: 加 1 个 `TEST_CASE` 验证 `ICOMPUTE_API_VERSION==1`
  - Build + [icompute] tag tests + 全量 44785/44785 assertions 全 pass
- [x] **Phase 2 — PTX-EMU 端 consumer patch** (PTX-EMU 仓 commit `c1fd5ed5`, 待 PTX-EMU PR owner review)
  - `include/ptxemu/instruction_descriptor.hh` (NEW): POD mirror 1:1 mirror `cpptlm::gpu::InstrDescriptor`
  - `src/ptxemu/device_api_impl.cc`: 加 `set_instr_descriptor_buf` 实现 + `attach_timing` body 改 no-op stub
  - `include/ptxemu/device_api.h`: `attach_timing` 加 `[[deprecated]]` 属性 (header 声明, 非签名变更)
  - 2 个 attach_timing 测试 → `tests/legacy-attach_timing/` + rename + [[deprecated]] marker
  - `ctest -R attach_timing_legacy` 100% pass (2/2 tests)
- [ ] **Phase 3 — docs/cross_repo mirror + tracker + sync check** (in progress, 本 PR)
- [ ] **Phase 4 — submodule re-bump** (deferred, 等 PTX-EMU Phase 2 PR owner 合并后再做单独 atomic commit)

## Subwave 4 (HSK-9 consumer pinning)

详细见 OpenSpec change `hsk9-icompute-api-v1-consumer-pinning`:

- Tasks 1.1-1.6 (CppTLM 端 contract pinning): ✅ 完成 (PR #25 commit `463f69ef`)
- Tasks 2.1-2.3 + 2.7-2.9 (PTX-EMU 端 producer + test relocation): ✅ 完成 (PTX-EMU 仓 commit `c1fd5ed5`, pending PTX-EMU owner review)
- Tasks 2.4-2.6 (step_b 重构 + vendor 字段清理): ⏸ deferred to subwave-2.5 (PTX-EMU owner ack 后, 单独 PR)
- Phase 3 (docs + tracker + sync): 📝 in progress (PR #25 Phase 3)
- Phase 4 (submodule re-bump): ⏸ deferred, 等 PTX-EMU PR 合并

## 关联 ADR / 文档

- `docs/soc_arch/adr/ADR-SOC-15-cdna-real-isa-roadmap.md` §3 D3 R3 (HSK 协调纪律)
- `docs/soc_arch/architecture/15-sm-microarchitecture-design.md` §15.5-§15.6 (SM 微架构)
- `docs/superpowers/specs/HSK-9-baseline-tracker.md` (基线数据 + Subwave 4 引用本镜像)