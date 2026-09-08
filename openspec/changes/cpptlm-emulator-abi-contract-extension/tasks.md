# Tasks: cpptlm-emulator-abi-contract-extension

## 1. ABI 参数边界校验测试扩展

- [ ] 1.1 扩展 `test/test_cpptlm_emulator_abi.cc`：新增 pcie_config 非法 width=3 → -EINVAL 用例（characterization：现有实现是否已校验？若否，characterize 为当前行为 + 升 follow-up change 补实现）
- [ ] 1.2 同文件：新增 backdoor NULL buf + len>0 → -EINVAL 用例
- [ ] 1.3 同文件：在文件头部加 `// T-bs-4-deferred:` 注释指针，明确本测试文件不含真实 dGPU 数据路径测试，待 DGpuBoard shell 完整化后另立 follow-up

## 2. msix 错误码路径扩展

- [ ] 2.1 扩展 `test/test_cpptlm_emulator_msix.cc`：新增 msix_init table_size=0 → -EINVAL 用例
- [ ] 2.2 同文件：msix_init table_size>2048 → -EINVAL 用例
- [ ] 2.3 同文件：msix_update/clear_pending vector_id 越界 → -EINVAL 用例（per `cpptlm_emulator.cc` 实际行为 characterize）

## 3. Registry 并发安全测试

- [ ] 3.1 扩展 `test/test_cpptlm_emulator_abi.cc` 或新建 `test/test_cpptlm_emulator_registry_concurrent.cc`（保持与 ABI 测试在同一二进制 `cpptlm_tests`，file(GLOB) 自动发现，per test/README.md）：4 reader × 1000 reads 并发 + 1 writer 反复 create/destroy。线程内 atomic 计错，主线程断言（per UsrLinuxEmu archived design.md D3 线程断言纪律）。注意：create 可能返回 nullptr（per `cpptlm_emulator.cc:67-81` 已有 lifecycle 测试），writer 需容忍；reader get_device_info 对无效 dev_id 返回 -ENOENT 不计入"错误"

## 4. create/destroy NULL 句柄与重复 destroy 测试

- [ ] 4.1 扩展 `test/test_cpptlm_emulator_abi.cc`：destroy(nullptr) 不崩溃（已有？核对现有 lifecycle 测试 :79）
- [ ] 4.2 同文件：重复 destroy 同一 emu 句柄不崩溃（no-op 或幂等）

## 5. Callback 注册路径 smoke 测试

- [ ] 5.1 扩展 `test/test_cpptlm_emulator_abi.cc`：cpptlm_emulator_register_callbacks 注册 4 类 callback + user_ctx 非 NULL → 返回 0（或已有契约值）
- [ ] 5.2 同文件：注册 callback 后 destroy emu 不导致 user_ctx 悬空访问（per-handle storage 由 std::function + lambda 捕获保证生命周期，per `cpptlm_emulator.cc:10` 注释）
- [ ] 5.3 同文件：明确 `// T-bs-4-deferred:` 注释：callback 实际触发链路（dGPU → MSI-X → registered intr_cb）待 DGpuBoard shell 完整化后另立 `cpptlm-emulator-dgpu-e2e-tests` change

## 6. 验证与收尾

- [ ] 6.1 构建：`cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON` + `cmake --build build -j$(nproc)`
- [ ] 6.2 跑 ABI 子集：`./build/bin/cpptlm_tests "[abi]"` 全绿（既有 + 新增）
- [ ] 6.3 跑全量：`./build/bin/cpptlm_tests` 全绿（零回归；catch2 `file(GLOB test_*.cc)` 自动发现新增文件）
- [ ] 6.4 若 1.x/2.x 任一用例触发红灯（实现缺校验）→ 记录到 design.md D1 后续 follow-up，本 change 不修实现
- [ ] 6.5 TSan 验证（可选）：若并发测试 3.1 触发 TSan 报警 → 升 follow-up；否则记录为已通过
- [ ] 6.6 勾选本 tasks.md 全部条目
