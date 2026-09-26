# ArpSID realtime ownership map

This file documents the practical ownership split used by the RT-safety
patches. It is a release-maintenance guide, not a user manual.

## Render / audio thread owned

These paths must remain lock-free, allocation-free, and bounded:

- `ArpSIDDSPKernel::process()` and helpers reached from it
- `drainPendingStateRestore_()`
- `applyStateRootCanonical(..., onRenderThread=true)`
- C64 PSID/RSID block rendering and SID timed-write projection
- `SidRuntimeModel::applyStateRootBySwap()`
- `SidRegisterEngine`, `BitPerfectEngine`, `DrSidEngine`, `Sid808Engine`, DIGI render paths
- realtime mailboxes/rings after a slot is consumed by the render side

Render-side state-root apply may only install an already-canonicalized and
already-hydrated root. It may read hydrated values and fixed-size mirrors, but
must not call semantic-vector growth, canonicalization, filesystem, logging,
Objective-C, locks, or heap-owning helper paths.

Parameter-type repair and semantic state-root canonicalization therefore happen
on the non-RT producer. After the prepared root is swapped on render, AU3 and
Phase2 may reconcile persistent adapter-local policy from their staged fixed-size
parameter image; that reconciliation must not mutate/canonicalize the root again.

## Non-RT producer side owned

These paths may allocate/canonicalize before publishing immutable work to render:

- UI parameter edits and preset/bank loading
- `schedulePendingStateRestore()` before mailbox publish
- state-root canonicalization/hydration
- template/blob/kit import/export
- file, URL, security-scoped bookmark, and macOS UI work

The producer side owns any state-root copy until it publishes it through a
mailbox. After publish, ownership is transferred; it must not mutate the slot.

## C64 PSID/RSID ownership

- C64 loading/init policy is non-RT unless explicitly documented otherwise.
- C64 playback execution during render is render-owned.
- RSID strict truth must use the PHI2 machine; Mos6510 legacy/semantic execution
  is compatibility only and must never report strict-clean physical exactness.
- PSID `playAddress == 0` uses the explicit continuous-machine runner, not the
  RSID-only runner.

## Telemetry ownership

Telemetry may summarize render-owned state through atomics/triple buffers. The
GUI consumes snapshots; it must not read live render objects directly.

`SidRuntimeRenderMode::C64Psid` is a telemetry sentinel only. C64 SID Player is a
component flavor and live C64-player state, not a normal synth render mode.
