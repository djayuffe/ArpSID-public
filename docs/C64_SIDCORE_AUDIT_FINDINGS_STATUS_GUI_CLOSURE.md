# PASS302 — C64/SIDCORE Audit Findings Status/GUI Closure

This release closes the next concrete audit findings that were still actionable without falsely claiming transistor-level CIA/VIC/SID exactness.

## Closed

- Added explicit runtime/API separation between:
  - `rsidStrictStatusCode()` — 0/1/2 strict-PHI2 status.
  - `rsidPlaybackModeCode()` — 1 strict, 2 compatible.
  - `rsidExactnessDowngradeMask()` — full `RsidExactnessDowngrade` bitmask.
  - `rsidExactPlaybackActive()` — deprecated alias meaning physically exact/no-known-downgrade only.
- Extended the GUI diagnostic snapshot to schema v4 with explicit RSID fields:
  - `rsidStrictStatusCode`
  - `rsidPlaybackModeCode`
  - `rsidExactnessDowngradeMask`
  - legacy `rsidExactPlaybackActive` alias
- Updated the C64 STATE diagnostic panel to show strict status, playback mode, and downgrade mask separately.
- Kept compatible-mode PHI2 execution from being displayed as strict/physical.
- Added regression coverage proving the GUI/HUD status contract cannot collapse back to boolean exactness.

## Intentionally not hidden

The following are still represented as downgrades rather than falsely claimed exact:

- missing real ROMs / HLE vectors
- non-cycle-exact CIA model
- non-cycle-exact VIC bus-steal model
- SID readable register approximation
- deterministic open-bus approximation
- legacy/compatible execution paths

## New test

- `source/tests/c64_strict_status_gui_contract_v729_tests.cpp`

