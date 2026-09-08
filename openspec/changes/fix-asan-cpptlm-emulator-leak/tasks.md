# Tasks: fix-asan-cpptlm-emulator-leak

## 1. 根因修复：DGpuBoard soc schema 完整化

- [x] 1.1 在 `configs/dgpu_board_v1.json` line 32 (`modules[0]` soc 子模块) 添加 `"connections": []` 字段
- [x] 1.2 验证：本地 ASan build (`build-asan`) 中 `validateConfig(soc_cfg)` 返 true
- [x] 1.3 验证：DGpuBoard::load_soc_config 后 pcie_ep/sdma/cp/tmu/sq/cq/gpu 实例全部创建

## 2. 根因修复（实施中发现）：gpu 模块内联 params + SimModule 双激活幂等守卫

- [x] 2.1 `configs/dgpu_board_v1.json` gpu 模块由 `config` 模板引用改为内联 params
      (gpc_count/tpc_per_gpc/cu_per_tpc/cu_template)，消除外层 GpuCluster 空 cu_template
      异常链 (TpcCluster throw → instantiateAll 异常路径泄漏 706KB)
- [x] 2.2 `src/tlm/cluster/{gpu,gpc,tpc,compute}_cluster.cc` 的 `simulate_instantiate`
      加幂等守卫：`ModuleFactory::instantiateAll` Step 4.5 与 `SimModule::simulate_instantiate`
      递归双激活子 SimModule，第二次激活重新生成子树并覆盖 instances map，
      第一批子树泄漏 (333KB)。守卫消除泄漏 (fix-asan 根因 2)。
- [x] 2.3 `src/abi/cpptlm_emulator.cc` 加 registrar (REGISTER_OBJECT + REGISTER_CHSTREAM)：
      cpptlm_tests Catch2 binary 不调 main.cpp 的 REGISTER_ALL，registry 为空导致
      SOC 实例化失败 → 测试失败 → cleanup 跳过 → 泄漏。registrar 使 lib 加载即注册。

## 3. 测试健壮性：RAII EmulatorHandleGuard

- [x] 3.1 新建 `test/test_cpptlm_emulator_handle_helpers.hh`：定义 `EmulatorHandleGuard` RAII 类（封装 create + destroy）
- [x] 3.2 在 `test/test_cpptlm_emulator_msix.cc` 改用 `EmulatorHandleGuard emu(0);` 替换裸 `cpptlm_emulator_create_by_id/destroy` 调用
- [x] 3.3 在 `test/test_cpptlm_emulator_msix.cc` 的 lookup_register TEST_CASE 同样改用 RAII

## 4. 验证：ASan 零泄漏

- [x] 4.1 重新配置 + 构建：`cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DUSE_ASAN=ON -DCPPTLM_WITH_PTX_EMU=OFF && cmake --build build-asan -j$(nproc)`
- [x] 4.2 跑 `ctest --test-dir build-asan -R "test_cpptlm_emulator" --output-on-failure` → 3/3 PASS
- [x] 4.3 验证：test_cpptlm_emulator_msix 4/4 PASS, test_cpptlm_emulator_lookup_register 1/1 PASS
- [x] 4.4 跑 `ASAN_OPTIONS=detect_leaks=1 ./bin/cpptlm_tests "[abi]"` 验证 0 bytes leaked (83/17 全绿)

## 5. 全回归验证

- [x] 5.1 跑 Release build 全部 63 测试：`ctest --test-dir build-release --output-on-failure -j4` → 100% pass, 零回归
- [x] 5.2 跑 Debug ASan OFF 测试：`ctest --test-dir build --output-on-failure -j4` → 100% pass (63/63)
- [x] 5.3 跑 ASan 全量：`ctest --test-dir build-asan --output-on-failure -j4` → 100% pass (63/63)

## 6. 提交与 PR

- [ ] 6.1 提交 1：`fix(asan): resolve test_cpptlm_emulator leaks (config schema + gpu params + simmodule idempotency + registry registrar)`
- [ ] 6.2 提交 2：`test(cpptlm-emulator): use RAII guard for exception-safe cleanup`
- [ ] 6.3 提交 3：`docs(openspec): fix-asan-cpptlm-emulator-leak change artifacts`
- [ ] 6.4 push 到 fix/asan-cpptlm-emulator-leak 分支
- [ ] 6.5 开 PR 到 main，标题 "fix(asan): resolve test_cpptlm_emulator memory leaks (pre-existing 53+ days)"
- [ ] 6.6 验证 CI：Build (Debug, ASan=ON) 转 pass, 其他 4 个 job 保持 pass
- [ ] 6.7 merge PR 后验证 main CI 5/5 pass
