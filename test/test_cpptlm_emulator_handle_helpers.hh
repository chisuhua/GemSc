// test/test_cpptlm_emulator_handle_helpers.hh
// RAII 守卫: 封装 cpptlm_emulator_create_by_id / cpptlm_emulator_destroy,
// 保证 REQUIRE 断言失败 (Catch2 抛异常) 时 handle 仍被释放, 不产生 ASan 泄漏.
// Author: CppTLM Team
// Date: 2026-09-08
#ifndef CPPTLM_TEST_CPPTLM_EMULATOR_HANDLE_HELPERS_HH
#define CPPTLM_TEST_CPPTLM_EMULATOR_HANDLE_HELPERS_HH

#include "abi/cpptlm_emulator.h"

// create + destroy 的 RAII 包装: 析构自动 destroy, 异常安全, 不可拷贝.
struct EmulatorHandleGuard {
    cpptlm_emulator_t* emu;

    explicit EmulatorHandleGuard(uint32_t dev_id = 0)
        : emu(cpptlm_emulator_create_by_id(dev_id)) {}

    ~EmulatorHandleGuard() {
        if (emu) {
            cpptlm_emulator_destroy(emu);
        }
    }

    EmulatorHandleGuard(const EmulatorHandleGuard&) = delete;
    EmulatorHandleGuard& operator=(const EmulatorHandleGuard&) = delete;

    // create 失败时返回 false (create_by_id 可能因配置/SOC 初始化失败返回 nullptr)
    bool valid() const { return emu != nullptr; }
};

#endif // CPPTLM_TEST_CPPTLM_EMULATOR_HANDLE_HELPERS_HH
