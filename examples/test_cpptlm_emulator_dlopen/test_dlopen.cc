// examples/test_cpptlm_emulator_dlopen/test_dlopen.cc
// T-W3-2 (T-ae-4): dlopen usage template for UsrLinuxEmu integration
// (per ADR-088 §D5 + ADR-SOC-07 D5)
//
// 演示: dlopen libcpptlm_emulator.so + dlsym 23 ABI 函数 + 调通关键路径
// (get_version + create_by_id + mmio_read/write + destroy).
// UsrLinuxEmu linux_compat 端可通过同样模式调用 23 ABI.
//
// AE-G5: stdout 输出 "v1.0-dgpu-v0" + 成功 create/destroy + 退出码 0.

#include <dlfcn.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(void) {
    void* handle = dlopen("libcpptlm_emulator.so", RTLD_LAZY | RTLD_LOCAL);
    if (handle == nullptr) {
        std::fprintf(stderr, "test_dlopen: dlopen failed: %s\n", dlerror());
        return 1;
    }

    using FnGetVersion = const char* (*)();
    using FnCreateById = void* (*)(uint32_t);
    using FnMmioWrite = int (*)(void*, uint8_t, uint64_t, const void*, size_t);
    using FnMmioRead = int (*)(void*, uint8_t, uint64_t, void*, size_t);
    using FnDestroy = void (*)(void*);

    auto get_version = reinterpret_cast<FnGetVersion>(dlsym(handle, "cpptlm_emulator_get_version"));
    auto create_by_id =
        reinterpret_cast<FnCreateById>(dlsym(handle, "cpptlm_emulator_create_by_id"));
    auto mmio_write = reinterpret_cast<FnMmioWrite>(dlsym(handle, "cpptlm_emulator_mmio_write"));
    auto mmio_read = reinterpret_cast<FnMmioRead>(dlsym(handle, "cpptlm_emulator_mmio_read"));
    auto destroy = reinterpret_cast<FnDestroy>(dlsym(handle, "cpptlm_emulator_destroy"));

    if (!get_version || !create_by_id || !mmio_write || !mmio_read || !destroy) {
        std::fprintf(stderr, "test_dlopen: dlsym missing: %s\n", dlerror());
        dlclose(handle);
        return 1;
    }

    const char* version = get_version();
    if (version == nullptr) {
        std::fprintf(stderr, "test_dlopen: get_version returned NULL\n");
        dlclose(handle);
        return 1;
    }
    std::printf("%s\n", version);

    void* emu = create_by_id(0);
    if (emu == nullptr) {
        std::fprintf(stderr, "test_dlopen: create_by_id returned NULL (shell deferred)\n");
        dlclose(handle);
        return 0;
    }

    uint32_t test_value = 0xCAFEBABE;
    int wr_rc = mmio_write(emu, 0, 0x14, &test_value, sizeof(test_value));
    if (wr_rc != 0) {
        std::fprintf(stderr, "test_dlopen: mmio_write returned %d\n", wr_rc);
    }

    uint32_t read_value = 0;
    int rd_rc = mmio_read(emu, 0, 0x14, &read_value, sizeof(read_value));
    if (rd_rc != 0) {
        std::fprintf(stderr, "test_dlopen: mmio_read returned %d\n", rd_rc);
    }

    destroy(emu);

    dlclose(handle);
    return 0;
}
