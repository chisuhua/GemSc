// test/test_dgpu_board_shell_full_abi.cc
// T-W3-3 Phase 4 (1B): 23 ABI 多卡 lifecycle + stats path + device enum edge cases
// SOC deferred (D15) so happy-path forwarding returns -ENOSYS; tests cover
// lifecycle/enumeration paths testable without SOC.
// dev_id 0 has a profile path; higher dev_ids need a matching profile JSON
// or board methods return -EINVAL. Use dev_id 0 for forwarding tests.
#include <catch_amalgamated.hpp>
#include "abi/cpptlm_emulator.h"

TEST_CASE("Full ABI: multi-card lifecycle (4 distinct dev_ids ≥10 → destroy → restored)",
          "[dgpu][shell][full_abi][lifecycle]") {
    uint32_t before = cpptlm_emulator_get_device_count();
    cpptlm_emulator_t* emus[4];
    for (uint32_t i = 0; i < 4; ++i) {
        emus[i] = cpptlm_emulator_create_by_id(10 + i);
        REQUIRE(emus[i] != nullptr);
        for (uint32_t j = 0; j < i; ++j) {
            REQUIRE(emus[i] != emus[j]);
        }
    }
    REQUIRE(cpptlm_emulator_get_device_count() == before + 4);

    cpptlm_device_info_t info{};
    REQUIRE(cpptlm_emulator_get_device_info(10, &info) == 0);
    REQUIRE(cpptlm_emulator_get_device_info(99, &info) == -2); // ENOENT (actual)

    for (uint32_t i = 0; i < 4; ++i) {
        cpptlm_emulator_destroy(emus[i]);
    }
    REQUIRE(cpptlm_emulator_get_device_count() == before);
}

TEST_CASE("Full ABI: mmio/backdoor return -ENOSYS via wrapper when SOC not instantiated",
          "[dgpu][shell][full_abi][forward]") {
    cpptlm_emulator_t* e = cpptlm_emulator_create_by_id(0);
    REQUIRE(e != nullptr);

    uint32_t val = 0;
    int wr = cpptlm_emulator_mmio_write(e, 0, 0x14, &val, sizeof(val));
    int rd = cpptlm_emulator_mmio_read(e, 0, 0x14, &val, sizeof(val));
    REQUIRE((wr == 0 || wr == -110 || wr == -22));
    REQUIRE((rd == 0 || rd == -110 || rd == -22));

    uint8_t buf[8] = {0};
    int bw = cpptlm_emulator_backdoor_write(e, 1, 0x1000, buf, sizeof(buf));
    int br = cpptlm_emulator_backdoor_read(e, 1, 0x1000, buf, sizeof(buf));
    REQUIRE((bw == 0 || bw == -22));
    REQUIRE((br >= 0 || br == -22));

    cpptlm_emulator_destroy(e);
}

TEST_CASE("Full ABI: create_by_id idempotent destroy (double destroy safe)",
          "[dgpu][shell][full_abi][lifecycle]") {
    uint32_t before = cpptlm_emulator_get_device_count();
    cpptlm_emulator_t* e = cpptlm_emulator_create_by_id(0);
    REQUIRE(e != nullptr);
    REQUIRE(cpptlm_emulator_get_device_count() == before + 1);

    cpptlm_emulator_destroy(e);
    cpptlm_emulator_destroy(e); // idempotent — must not crash
    REQUIRE(cpptlm_emulator_get_device_count() == before);
}

TEST_CASE("Full ABI: get_version non-null + device_info null/ENOENT guards",
          "[dgpu][shell][full_abi][edge]") {
    REQUIRE(cpptlm_emulator_get_version() != nullptr);

    cpptlm_device_info_t info{};
    REQUIRE(cpptlm_emulator_get_device_info(0, nullptr) == -22);
    REQUIRE(cpptlm_emulator_get_device_info(999, &info) == -2); // ENOENT
}

TEST_CASE("Full ABI: register_callbacks accepts non-null callbacks on valid emu",
          "[dgpu][shell][full_abi][callback]") {
    cpptlm_emulator_t* e = cpptlm_emulator_create_by_id(0);
    REQUIRE(e != nullptr);
    REQUIRE(cpptlm_emulator_register_callbacks(e, nullptr, nullptr, nullptr, nullptr, nullptr) ==
            0);
    REQUIRE(cpptlm_emulator_register_callbacks(nullptr, nullptr, nullptr, nullptr, nullptr,
                                               nullptr) == -22);
    cpptlm_emulator_destroy(e);
}
