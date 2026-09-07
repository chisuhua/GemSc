## ADDED Requirements

### Requirement: Cross-repo HSK-9 mirror document exists
The CppTLM repository SHALL contain a cross-repo mirror document at `docs/cross_repo/HSK-9-2027-02-09-cpptlm-sm-rewrite.md` that links the HSK-9 authoritative spec to the CppTLM-side implementation track.

#### Scenario: Mirror document file exists
- **WHEN** the CppTLM repository tree is enumerated
- **THEN** the file `docs/cross_repo/HSK-9-2027-02-09-cpptlm-sm-rewrite.md` SHALL exist
- **AND** the file SHALL contain both a `## 关联权威 Spec` section and a `## CppTLM 端落地动作` section (content-based criterion, not line count)

#### Scenario: Mirror references authoritative spec
- **WHEN** the mirror document content is inspected
- **THEN** it SHALL contain a `## 关联权威 Spec` section with a relative link to `docs/superpowers/specs/2027-02-09-hsk-9-icompute-api-v1-sm-rewrite.md`
- **AND** the mirror SHALL NOT duplicate the HSK-9 spec content verbatim (only operational metadata + CppTLM-side action items)

#### Scenario: Mirror lists CppTLM-side action items
- **WHEN** the mirror document content is inspected
- **THEN** it SHALL contain a `## CppTLM 端落地动作` section with checkboxes for: ICOMPUTE_API_VERSION 钉死、PTX-EMU consumer 改造 (sub-bump 待 ack)、3 vendor 接口移除、BitExactGate (out-of-scope per Oracle verdict C)
- **AND** each checkbox SHALL be `[ ]` (open) at scaffold time

### Requirement: HSK-9 baseline tracker updated with consumer subwave
The file `docs/superpowers/specs/HSK-9-baseline-tracker.md` SHALL contain a new subwave entry documenting the consumer pinning work.

#### Scenario: Subwave entry present
- **WHEN** the HSK-9 baseline tracker is inspected
- **THEN** it SHALL contain a section titled `## Subwave 4 (HSK-9 consumer pinning)` (or appropriate next number)
- **AND** the entry SHALL reference `openspec/changes/hsk9-icompute-api-v1-consumer-pinning/`
- **AND** the entry SHALL list the 3 phases from `design.md` (CppTLM contract / PTX-EMU consumer / doc mirror + tracker)

### Requirement: AGENTS.md references HSK mirror path (NOT VIRTUAL_PATHS)
The `AGENTS.md` STRUCTURE path table SHALL contain a backtick-quoted reference to `docs/cross_repo/HSK-9-2027-02-09-cpptlm-sm-rewrite.md` so that the existing `scripts/test/docs_sync_check.sh` scanner picks it up. The path SHALL NOT be added to the `VIRTUAL_PATHS` array (which is an exemption list — adding the mirror there would invert the failure semantics).

#### Scenario: AGENTS.md references mirror path
- **WHEN** `AGENTS.md` STRUCTURE section is inspected
- **THEN** it SHALL contain a backtick-quoted reference to `docs/cross_repo/HSK-9-2027-02-09-cpptlm-sm-rewrite.md`
- **AND** the `scripts/test/docs_sync_check.sh` `VIRTUAL_PATHS` array SHALL NOT contain this path

#### Scenario: Sync check fails when mirror is missing
- **WHEN** the mirror file is deleted (simulating accidental removal)
- **THEN** running `bash scripts/test/docs_sync_check.sh --strict` SHALL exit with non-zero status
- **AND** the error message SHALL name the missing path (via the AGENTS.md reference, not via VIRTUAL_PATHS)
