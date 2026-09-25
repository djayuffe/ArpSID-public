# ArpSID 0.0.690 pass380 v885 — Absolute final block-local projection mirror closure

## Closure

v885 closes the last identified projection-mirror lifecycle issue after v884:

- Applied-write observer events remain the only normal C64 telemetry mirror authority.
- Projection-mirror writes are now strictly block-local on both paths:
  - discarded when the mirror block is not consumed;
  - also discarded after a consumed mirror advance, so cycle-capped late writes cannot replay in a later host block.
- Ordinary C64 scheduled events remain preserved because `clearScheduledProjectionWrites()` removes only events tagged as `projectionMirror`.

## New regression coverage

- `C64ProjectionMirrorBlockLocalV885Tests`
  - proves a late projection event beyond a cycle-capped consumed block is discarded;
  - proves an ordinary same-cycle scheduled CPU event survives that cleanup.
- `C64ProjectionMirrorAuthorityV885SourceTests`
  - locks the source-level consumed-branch cleanup contract.

## Verified targeted closure suite

The v874-v885 mirror/queue/clock closure tests pass: 14/14.
