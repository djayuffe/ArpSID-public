# 0.0.659-pass299 — C64/SIDCORE strict status telemetry completion

This pass closes a real telemetry correctness gap left after the strict/physical naming work.

## Problem

The GUI diagnostic field named `rsidExactPlaybackActive` had legacy semantics and was still derived from approximate/unsupported opcode counters only. That meant a strict PHI2 RSID run with other known downgrades — HLE ROMs, CIA/VIC approximations, SID-read approximations, open-bus observations, etc. — could be displayed as status `1` if no approximate/unsupported opcodes had executed.

That was misleading after the exactness ledger became richer.

## Fix

A compact runtime status is now exposed by `C64Runtime::rsidStrictStatusCode()`:

- `0`: no active strict-PHI2 RSID path
- `1`: strict-PHI2 path active and the exactness ledger has no known downgrades
- `2`: strict-PHI2 path active with one or more known downgrades

`rsidExactPlaybackActive()` remains a deprecated compatibility spelling for `rsidPhysicallyExact()` only.

The render thread now stores the normalized status code directly. The GUI snapshot no longer reclassifies status from approximate/unsupported opcode counters alone.

## GUI/HUD wording

The C64 state panel label and tooltip now describe the field as strict status rather than exact/approx-opcode-only state. Status `2` is labelled as a downgraded PHI2 path, not merely approximate opcode execution.

## Regression

Added `C64RsidStatusCodeV726Tests`, which verifies that a strict PHI2 RSID path with HLE/CIA/VIC downgrades reports status `2`, while `rsidPhysicallyExact()` and deprecated `rsidExactPlaybackActive()` remain false.
