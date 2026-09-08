## ADDED Requirements

### Requirement: ICOMPUTE_API_VERSION=1 constant defined
The system SHALL define the preprocessor macro `ICOMPUTE_API_VERSION` with the integer value `1` in the global namespace, visible to any translation unit that includes `include/tlm/gpu/i_compute_device.hh`.

#### Scenario: Constant is defined
- **WHEN** a translation unit includes `include/tlm/gpu/i_compute_device.hh`
- **THEN** `ICOMPUTE_API_VERSION` SHALL expand to the integer literal `1`
- **AND** `static_assert(ICOMPUTE_API_VERSION == 1, "IComputeDevice contract version mismatch")` SHALL compile without error

#### Scenario: Constant matches PTX-EMU counterpart
- **WHEN** CppTLM and PTX-EMU headers are both included in the same translation unit **AND the translation unit is built with `CPPTLM_WITH_PTX_EMU=ON`** (PTXEMU_API_VERSION is only defined under PTX-EMU submodule, which is OFF by default per `CMakeLists.txt` L50)
- **THEN** `ICOMPUTE_API_VERSION` (CppTLM) SHALL have the same integer value as `PTXEMU_API_VERSION` (PTX-EMU, defined in `external/PTX-EMU/include/ptxemu/device_api.h`)
- **AND** a `static_assert(ICOMPUTE_API_VERSION == PTXEMU_API_VERSION, ...)` SHALL compile without error (the cross-repo static_assert SHALL be guarded by `#ifdef CPPTLM_WITH_PTX_EMU` to allow OFF-mode builds)

### Requirement: IComputeDevice has 15 method-signature static_asserts
The CppTLM `IComputeDevice` class SHALL enforce 15 method-signature `static_assert`s (one per HSK-9 §3 method) preventing any future change to method signatures, parameter types, or return types without bumping `ICOMPUTE_API_VERSION`. C++ has no idiomatic compile-time virtual-method-count expression, so per-method signature asserts are the practical enforcement mechanism.

#### Scenario: Per-method signature assertion set
- **WHEN** a translation unit includes `include/tlm/gpu/i_compute_device.hh`
- **THEN** the header SHALL contain 15 `static_assert` declarations, each verifying one of the 15 HSK-9 §3 method signatures (using `std::is_same_v<decltype(&IComputeDevice::method), ExpectedSignature>` or equivalent)
- **AND** the compilation SHALL fail if any of the 15 method signatures (name, parameter types, return type, const-ness) changes

#### Scenario: get_thread_state returns ThreadState not int
- **WHEN** the `get_thread_state` static_assert is evaluated
- **THEN** it SHALL verify the return type is `cpptlm::gpu::ThreadState`
- **AND** it SHALL fail compilation if return type is changed to `int` (preventing archive `cpptlm-dgpu-d1-cdna-isa-sm-rewrite` Task 3.5 P0 regression)

### Requirement: Placeholder static_assert replaced
The CppTLM `IComputeDevice` header SHALL NOT contain a placeholder `static_assert(sizeof(...) > 0, ...)` or any non-contract-enforcing assertion.

#### Scenario: No meaningless assertions
- **WHEN** the source of `include/tlm/gpu/i_compute_device.hh` is inspected
- **THEN** every `static_assert` in the file SHALL enforce a contract (version, method count, or method signature)
- **AND** no `static_assert(sizeof(X) > 0, ...)` placeholder SHALL remain

### Requirement: CppTLM HSK-9 contract test coverage
The CppTLM test suite SHALL include a test case that validates the `ICOMPUTE_API_VERSION=1` contract at compile time and runtime.

#### Scenario: Compile-time version check test
- **WHEN** `test/test_i_compute_device_interface.cc` is compiled
- **THEN** the test file SHALL include a `static_assert(ICOMPUTE_API_VERSION == 1, ...)`
- **AND** the test SHALL have a Catch2 `TEST_CASE` named "IComputeDevice ICOMPUTE_API_VERSION is 1" that asserts the constant value at runtime
