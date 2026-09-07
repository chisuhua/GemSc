#include "abi/cpptlm_emulator.h"
#include "catch_amalgamated.hpp"

TEST_CASE("ABI: open/get_adapter_info/close lifecycle", "[abi][adapter_info]") {
    cpptlm_emulator_handle_t handle = 0;
    int rc = cpptlm_emulator_open(0, &handle);
    REQUIRE(rc == 0);
    REQUIRE(handle != 0);

    cpptlm_device_info_t info{};
    rc = cpptlm_emulator_get_adapter_info(handle, &info);
    REQUIRE(rc == 0);
    REQUIRE(info.vendor_id == 0x10DE);
    REQUIRE(info.device_id == 0x1234);
    REQUIRE(info.visible_vram_size >= 256ULL * 1024 * 1024);
    REQUIRE(info.invisible_vram_size >= 1ULL * 1024 * 1024 * 1024);
    REQUIRE(info.va_region_size >= (1ULL << 48));
    REQUIRE(info.gfx_version == 1100);
    REQUIRE(info.bdf == 0x0008);

    rc = cpptlm_emulator_close(handle);
    REQUIRE(rc == 0);

    rc = cpptlm_emulator_get_adapter_info(handle, &info);
    REQUIRE(rc == -EINVAL);

    rc = cpptlm_emulator_close(handle);
    REQUIRE(rc == -EINVAL);
}

TEST_CASE("ABI: open with invalid handle pointer", "[abi][adapter_info]") {
    REQUIRE(cpptlm_emulator_open(0, nullptr) == -EINVAL);
}

TEST_CASE("ABI: get_adapter_info with null pointer", "[abi][adapter_info]") {
    cpptlm_emulator_handle_t handle = 0;
    REQUIRE(cpptlm_emulator_open(0, &handle) == 0);
    REQUIRE(cpptlm_emulator_get_adapter_info(handle, nullptr) == -EINVAL);
    REQUIRE(cpptlm_emulator_close(handle) == 0);
}
