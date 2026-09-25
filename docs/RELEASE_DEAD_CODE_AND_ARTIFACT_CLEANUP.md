# PASS285 — Release Dead-Code / Dead-Artifact Cleanup

This closure cleans the release package after the compile-guard and DIGI `$D418` work.

## Removed release-root artifacts

The release root no longer ships historical audit/progression files:

- `AUDIT_FINDINGS_CLOSURE_MATRIX_PASS56.md`
- `AUDIT_FINDINGS_CLOSURE_MATRIX_PASS56.json`
- `AUDIT_PROGRESSION.md`
- `docs/pass-history/*`

The active audit matrix is retained only as validation input under:

- `docs/audit/AUDIT_FINDINGS_CLOSURE_MATRIX.md`
- `docs/audit/AUDIT_FINDINGS_CLOSURE_MATRIX.json`
- `docs/audit/AUDIT_PROGRESSION.md`

## Removed stale script entrypoints

Old pass-numbered script entrypoints were removed and replaced with stable names:

- `scripts/verify_source_tree.py`
- `scripts/check_audit_closure.py`
- `scripts/run_full_ctest_preflight.sh`
- `scripts/macos_full_build_install_clear_au_logic_cache.sh`

Removed stale aliases/scripts:

- `scripts/verify_source_tree_pass48.py`
- `scripts/check_audit_closure_pass56.py`
- `scripts/run_full_ctest_preflight_pass68.sh`
- `scripts/run_full_ctest_preflight_pass83.sh`
- `scripts/macos_full_build_install_clear_au_logic_cache_pass59.sh`
- `scripts/macos/full_build_install_clear_au_logic_cache_pass59.sh`
- `scripts/macos/macos_full_build_install_clear_au_logic_cache_pass59.sh`

## Build/install cleanup

`build.sh --install-auv2` now delegates the component copy/sign/install operation to the canonical installer:

```bash
scripts/macos/install_auv2_component.sh
```

This removes duplicate install logic from the root build helper.

## Regression guards

The release keeps the existing guards and updates them for stable script names:

- release-root clean guard
- source comment/code merge guard
- source tree guard
- full preflight script guard
- macOS production script guard
- audit closure matrix guard

