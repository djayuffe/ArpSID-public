# Release Cleanup Notes

This release keeps root-level files limited to build, install, validation, README, version, manifest, and the canonical `$D418` technical spec.

Historical patch/pass notes and audit logs are not release-root artifacts. They are either removed from the release package or stored under `docs/audit/` when still useful as active validation input.

Canonical helper scripts use stable names:

- `scripts/verify_source_tree.py`
- `scripts/check_audit_closure.py`
- `scripts/run_full_ctest_preflight.sh`
- `scripts/macos_full_build_install_clear_au_logic_cache.sh`

Old pass-numbered script aliases were removed to avoid stale entrypoints.
