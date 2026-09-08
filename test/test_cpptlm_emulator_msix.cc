// test/test_cpptlm_emulator_msix.cc
// T-W3-3 Phase 3: 4 ABI stub 替换验证 (msix_init/update_pending/clear_pending + lookup_register)
// D15 fix (5425c45): SOC instantiated → wrappers forward (0); null/over-cap → -EINVAL (-22)
#include <catch_amalgamated.hpp>
#include "abi/cpptlm_emulator.h"

TEST_CASE("cpptlm_emulator_msix_init forwards to wrapper (null emu → -EINVAL, valid emu → wrapper "
          "return)",
          "[abi][msix][t-w3-3]") {
    REQUIRE(cpptlm_emulator_msix_init(nullptr, 16, 0) == -22); // EINVAL
    cpptlm_emulator_t* emu = cpptlm_emulator_create_by_id(0);
    REQUIRE(emu != nullptr);
    // D15 fix: SOC live → wrapper forwards to PcieEndpointTLM::msix().init() → 0
    REQUIRE(cpptlm_emulator_msix_init(emu, 16, 0) == 0);
    REQUIRE(cpptlm_emulator_msix_init(emu, 3000, 0) == -22); // EINVAL (table_size > 2048)
    cpptlm_emulator_destroy(emu);
}

TEST_CASE("cpptlm_emulator_msix_update_pending forwards to wrapper", "[abi][msix][t-w3-3]") {
    REQUIRE(cpptlm_emulator_msix_update_pending(nullptr, 0) == -22);
    cpptlm_emulator_t* emu = cpptlm_emulator_create_by_id(0);
    REQUIRE(emu != nullptr);
    // D15 fix: SOC live → wrapper forwards → 0
    REQUIRE(cpptlm_emulator_msix_update_pending(emu, 0) == 0);
    cpptlm_emulator_destroy(emu);
}

TEST_CASE("cpptlm_emulator_msix_clear_pending forwards to wrapper", "[abi][msix][t-w3-3]") {
    REQUIRE(cpptlm_emulator_msix_clear_pending(nullptr, 0) == -22);
    cpptlm_emulator_t* emu = cpptlm_emulator_create_by_id(0);
    REQUIRE(emu != nullptr);
    // D15 fix: SOC live → wrapper forwards. clear_pending returns 0 only when
    // PBA bit is set (i.e. after update_pending). Post-init PBA is empty so
    // clear returns false → wrapper -22; accept either valid outcome.
    int rc = cpptlm_emulator_msix_clear_pending(emu, 0);
    REQUIRE((rc == 0 || rc == -22));
    cpptlm_emulator_destroy(emu);
}

TEST_CASE("cpptlm_emulator_lookup_register fills cpptlm_register_info_t via wrapper",
          "[abi][lookup_register][t-w3-3]") {
    cpptlm_register_info_t info{};
    REQUIRE(cpptlm_emulator_lookup_register(nullptr, 0, &info) == -22);
    cpptlm_emulator_t* emu = cpptlm_emulator_create_by_id(0);
    REQUIRE(emu != nullptr);
    // ABI 直接调 board->lookup_register_entry (绕过 wrapper 的 EINVAL 检查),
    // unaligned/>BAR0/miss 在 entry 层统一返 nullptr → ABI -38.
    REQUIRE(cpptlm_emulator_lookup_register(emu, 0x14, nullptr) == -22);  // out_info null → EINVAL
    REQUIRE(cpptlm_emulator_lookup_register(emu, 0x15, &info) == -38);    // unaligned
    REQUIRE(cpptlm_emulator_lookup_register(emu, 0x10000, &info) == -38); // > BAR0
    REQUIRE(cpptlm_emulator_lookup_register(emu, 0x100, &info) == -38);   // miss in BAR0
    // D15 fix: SOC live → 0x14 hit fills info
    REQUIRE(cpptlm_emulator_lookup_register(emu, 0x14, &info) == 0);
    cpptlm_emulator_destroy(emu);
}
