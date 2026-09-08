## ADDED Requirements

### Requirement: set_instr_descriptor_buf implemented
The PTX-EMU `device_api_impl` class SHALL provide a public non-pure-virtual implementation of `set_instr_descriptor_buf(const InstrDescriptor* buf, uint32_t count)`, matching the HSK-9 spec interface added in `IComputeDevice` (not `IPtxEmuDevice`). The `const InstrDescriptor*` pointer signature is mandatory (matches `i_compute_device.hh:84` and HSK-9 §3 verbatim).

#### Scenario: set_instr_descriptor_buf stores buffer pointer
- **WHEN** a caller invokes `set_instr_descriptor_buf(buf, count)` with `buf != nullptr` and `count > 0`
- **THEN** the implementation SHALL store the buffer pointer and count for later consumption by `sm_exe_once`
- **AND** the implementation SHALL emit a debug log line `"set_instr_descriptor_buf: count=<N>"` at LOG_TRACE level

#### Scenario: set_instr_descriptor_buf rejects null buffer
- **WHEN** a caller invokes `set_instr_descriptor_buf(nullptr, count)` with `count > 0`
- **THEN** the implementation SHALL log a warning `"set_instr_descriptor_buf: null buffer ignored"` and return without crashing
- **AND** subsequent `sm_exe_once` calls SHALL fall back to the default per-instruction decode path

### Requirement: attach_timing marked deprecated
The PTX-EMU `IPtxEmuDevice::attach_timing(IScoreboard*, IPipelineLatencyProvider*, ITensorCoreTiming*)` method SHALL be marked `[[deprecated("use IComputeDevice::set_instr_descriptor_buf instead; attach_timing will be removed in HSK-10")]]` in the public header.

#### Scenario: attach_timing still callable
- **WHEN** existing CppTLM code calls `attach_timing(s, p, t)` (e.g. legacy integration tests, third-party tooling)
- **THEN** the call SHALL still compile and link (backward-compatibility preserved per HSK-9 12-method signature freeze)
- **AND** the compiler SHALL emit a `[[deprecated]]` warning at the call site

#### Scenario: attach_timing body is a no-op stub
- **WHEN** the deprecated `attach_timing` is invoked at runtime
- **THEN** the implementation SHALL be a no-op (do not store pointers, do not call into the legacy pipeline)
- **AND** the implementation SHALL emit a one-time warning log `"attach_timing is deprecated; IComputeDevice::set_instr_descriptor_buf replaces this path"`

### Requirement: sm_context_cpptlm_inject removed from 3 vendor interfaces
The PTX-EMU `sm_context_cpptlm_inject.{h,cpp}` module SHALL NOT depend on `IScoreboard`, `IPipelineLatencyProvider`, or `ITensorCoreTiming` interfaces. The execution path SHALL route through `IComputeDevice::exe_once` instead.

#### Scenario: No 3 vendor interface references
- **WHEN** `git grep "IScoreboard\\|IPipelineLatencyProvider\\|ITensorCoreTiming" external/PTX-EMU/src/ptxsim/core/sm_context_cpptlm_inject.{h,cpp}` is run
- **THEN** the command SHALL return no matches (zero references)

#### Scenario: sm_exe_once routes through IComputeDevice
- **WHEN** `sm_context_cpptlm_inject::sm_exe_once(uint32_t sm_id)` is called (1-parameter signature per `i_compute_device.hh:74`)
- **THEN** the implementation SHALL call `IComputeDevice::sm_exe_once(uint32_t sm_id)` on the bound CppTLM device (1 parameter, matching HSK-9 §3 verbatim)
- **AND** the implementation SHALL NOT call `IScoreboard::set_mask` / `IPipelineLatencyProvider::lookup` / `ITensorCoreTiming::get_latency_mnk` (per HSK-9 spec, these calls are deleted from the consumer side)

### Requirement: attach_timing tests relocated to legacy directory with rename
The PTX-EMU test suite SHALL move the `attach_timing`-related test files to `external/PTX-EMU/tests/legacy-attach_timing/` directory, with each test source file (a) gaining a `[[deprecated]]` annotation at file scope, (b) being renamed with `attach_timing_legacy_` prefix so that `ctest -R "attach_timing_legacy"` reliably matches them. At d5a58cf5 there are exactly 2 such tests; PTX-EMU owner may confirm or extend the list during review.

#### Scenario: Test files in legacy directory
- **WHEN** the test suite is enumerated at PTX-EMU d5a58cf5
- **THEN** at minimum 2 test files SHALL exist under `external/PTX-EMU/tests/legacy-attach_timing/`:
  - `tests/integration/cpptlm/test_attach_timing_consumer_e2e.cpp` (relocated + renamed to `attach_timing_legacy_test_attach_timing_consumer_e2e.cpp`)
  - `tests/unit/ptxemu/test_device_api_attach_timing.cpp` (relocated + renamed to `attach_timing_legacy_test_device_api_attach_timing.cpp`)
- **AND** no test file matching `*attach_timing*` SHALL exist outside `external/PTX-EMU/tests/legacy-attach_timing/` (i.e. removed from `tests/unit/` and `tests/integration/`)

#### Scenario: Deprecated annotation present
- **WHEN** each legacy test file is inspected
- **THEN** the first non-comment line SHALL be a comment block stating `// [[deprecated]] attach_timing is deprecated; IComputeDevice::set_instr_descriptor_buf replaces this path. Will be removed in HSK-10.`
- **AND** the test file SHALL compile and run with a deprecation warning

#### Scenario: Legacy tests still pass under rename
- **WHEN** `ctest -R "attach_timing_legacy"` is run from `external/PTX-EMU/build-standalone`
- **THEN** all relocated tests (renamed with `attach_timing_legacy_` prefix so the `-R` regex matches reliably) SHALL pass (backward-compatibility preserved)
