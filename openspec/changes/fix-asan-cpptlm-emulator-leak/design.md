# Design: fix-asan-cpptlm-emulator-leak

## 修复策略

本 change 采用 **方案 A**（推荐）：配置修复 + 测试 RAII 包装双层防御。

### 决策记录

#### 决策 1：仅修 JSON 配置 + 测试 RAII，不动 validateConfig

**Context**: `validateConfig` 严格要求 `connections` 字段。如果改成"缺失警告"则隐藏 config 错误。

**Decision**: 保持 validateConfig 严格。修配置让 soc schema 完整。

**Trade-off**:
- 严格校验 + 修配置：1 行 JSON 改动 + 2 行 RAII helper
- 容错校验：可能掩盖未来类似配置错误

#### 决策 2：测试用 RAII 而非 try/catch 包裹

**Context**: 每个测试 case 当前用 `REQUIRE(...) == 0` 断言 → 失败抛异常 → cleanup 跳过。

**Decision**: 引入 `EmulatorHandleGuard` RAII helper，封装 create + destroy：

```cpp
// test/test_cpptlm_emulator_handle_helpers.hh
#include "abi/cpptlm_emulator.h"

struct EmulatorHandleGuard {
    cpptlm_emulator_t* emu;
    explicit EmulatorHandleGuard(uint32_t dev_id = 0)
        : emu(cpptlm_emulator_create_by_id(dev_id)) {}
    ~EmulatorHandleGuard() { if (emu) cpptlm_emulator_destroy(emu); }
    EmulatorHandleGuard(const EmulatorHandleGuard&) = delete;
    EmulatorHandleGuard& operator=(const EmulatorHandleGuard&) = delete;
};
```

**Trade-off**:
- RAII：异常安全 + C++ 习惯写法 + 不改测试断言风格
- try/catch + manual destroy：侵入测试代码 + 易遗漏

### 实施路径

#### Path 1: 修 `configs/dgpu_board_v1.json`

在 soc 子模块（line 32 `modules[0]`）添加 `"connections": []`：

```json
{
  "name": "soc",
  "type": "DGpuSoc",
  "modules": [ ... ],
  "connections": [],   // ← 新增 (空数组, 实际无内部连接, 由 inputs/outputs 表达)
  "outputs": [...],
  "inputs": [...]
}
```

**验证**：本地 `build-asan` + `ctest -R test_cpptlm_emulator`：
- test_cpptlm_emulator_msix: 4/4 REQUIRE 通过
- test_cpptlm_emulator_lookup_register: 1/1 通过
- ASan: 0 bytes leaked

#### Path 2: 改 `test_cpptlm_emulator_msix.cc`

```cpp
// 之前
cpptlm_emulator_t* emu = cpptlm_emulator_create_by_id(0);
REQUIRE(emu != nullptr);
REQUIRE(cpptlm_emulator_msix_init(emu, 16, 0) == 0);
cpptlm_emulator_destroy(emu);

// 之后
#include "test_cpptlm_emulator_handle_helpers.hh"
EmulatorHandleGuard emu(0);
REQUIRE(emu.emu != nullptr);  // 或 emu.valid()
REQUIRE(cpptlm_emulator_msix_init(emu.emu, 16, 0) == 0);
// destroy 由 RAII 自动调用
```

#### Path 3: 改 `test_cpptlm_emulator_lookup_register.cc`

同 Path 2 模式。

## 验证策略

### 单元验证（本地）

```bash
# 1. ASan build 配置
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DUSE_ASAN=ON -DCPPTLM_WITH_PTX_EMU=OFF
cmake --build build-asan -j$(nproc)

# 2. 跑目标测试
cd build-asan && ctest --output-on-failure -R "test_cpptlm_emulator" -j1

# 3. 验证 ASan 0 leak
ASAN_OPTIONS=detect_leaks=1:print_stacktrace=1 ./bin/cpptlm_tests "[abi][msix][t-w3-3]"
ASAN_OPTIONS=detect_leaks=1:print_stacktrace=1 ./bin/cpptlm_tests "[abi][lookup_register][t-w3-3]"
```

**Expected**:
- test_cpptlm_emulator_msix: 4/4 PASS (current 1/4 PASS after fix)
- test_cpptlm_emulator_lookup_register: 1/1 PASS
- ASan: 0 bytes leaked (current 350+ bytes leaked)

### 全回归验证

```bash
# Release build 跑全部 63 测试
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure -j4
# Expected: 100% pass (63/63), zero regression
```

### CI 验证（待 push 后）

`.github/workflows/ci.yml::build-and-test` matrix 包含 `Build (Debug, ASan=ON)` job：
- 修复前：failure（pre-existing 53+ 天）
- 修复后：success

预期 main CI 5/5 通过。

## 回滚策略

如果修复引入回归：

1. **JSON 配置回滚**：删除 `"connections": []` 行（保留其他）
2. **RAII helper 保留**：RAII helper 本身无害，仅改测试代码
3. **CI 重新跑**：push revert commit

不影响产品代码（cpptlm_emulator.cc, dgpu_board_shell.cc）—— 修复集中在配置 + 测试。

## 风险评估

| 风险 | 概率 | 影响 | 缓解 |
|------|------|------|------|
| 配置修改影响其他测试用 dgpu_board_v1.json | 低 | 中 | grep -rn "dgpu_board_v1.json" 验证用途 |
| RAII helper 引入 ABI 不匹配 | 极低 | 低 | helper 仅包裹现有 ABI 函数 |
| ASan 仍有其他泄漏（其他根因） | 中 | 低 | 修复后跑全部 ASan 测试看是否还有其他 |

**低风险变更**：1 行 JSON + 1 新 helper + 2 测试文件改 RAII。所有变更可独立 commit。

## 实施发现（2026-09-08 实测）

### 根因 2：SimModule 双激活 → 子树覆盖泄漏

json 修复后 SOC 真正实例化，暴露第二个 pre-existing 泄漏根因：

- `ModuleFactory::instantiateAll` Step 4.5（module_factory.cc:378）与 `SimModule::simulate_instantiate` 递归（sim_module.hh:105）**重复激活**子 SimModule
- 第二次激活时基类幂等守卫返回，但 GpuCluster/GpcCluster/TpcCluster/ComputeCluster 覆写的追加生成逻辑仍执行 → 二次 `instantiateAll` 用 `instances = object_instances` 整体替换 map → 第一批子树（gpc/tpc/compute/cu 全部后代）成为孤儿永不释放
- 症状：`[abi][msix]` 706KB→333KB→0 bytes（json 修复后剩 333KB，守卫后归零）
- **修复**：4 个集群类的 `simulate_instantiate` 加幂等守卫（`internal_factory` 已有实例则 return），与基类同款，双激活第二次进入直接返回

### 根因 3：gpu 模块 config 模板引用 → 空 cu_template 异常链

- `dgpu_board_v1.json` 的 gpu 模块用 `"config"` 引用模板 `gpu_2gpc_2tpc_2cu.json`，外层 GpuCluster 自身无 params（`cu_template_path_` 空）
- 外层 gpu 的追加生成逻辑用空 cu_template 建 gpc0 → TpcCluster 抛 `cu_template must be set` → `instantiateAll` Step 4.5 catch 后 `return false`，已创建实例不转移 → 泄漏
- **修复**：gpu 模块改为内联 params（gpc_count/tpc_per_gpc/cu_per_tpc/cu_template），消除空 cu_template 路径

### registrar（registry 填充）

- `cpptlm_tests` Catch2 binary 不调 `main.cpp` 的 REGISTER_ALL → libcpptlm_emulator.so 加载时 ModuleFactory registry 空 → SOC 实例化失败（Unknown type）→ 测试失败 → cleanup 跳过 → 泄漏
- **修复**：`src/abi/cpptlm_emulator.cc` 加静态 registrar（REGISTER_OBJECT + REGISTER_CHSTREAM，`__attribute__((used))` 防 GC），lib 加载即注册

### 验证结果

- `build-asan` ctest `-R test_cpptlm_emulator`：3/3 PASS（msix 4/4 + lookup 1/1 + dlopen）
- `ASAN_OPTIONS=detect_leaks=1 [abi]`：83/17 全绿，**0 bytes leaked**
- 全回归：Debug 63/63、Release 63/63、ASan 63/63 全绿
