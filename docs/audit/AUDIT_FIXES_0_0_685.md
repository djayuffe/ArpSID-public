# ArpSID 0.0.685 (pass325) — Audit Fixes

This release applies the **test-verifiable, regression-safe** subset of the
external P0/P1/P2 audit against `0.0.684-pass324`. Every change below was gated
by the full `ctest` suite (281/281 green) plus AUv2 build + `auval`.

## Applied

| ID | Area | Change |
|----|------|--------|
| P0-1 | RT safety | `startPureSid1Q1RecordCapture` now returns `bool` and **fails closed**: it never reassigns the capture vector unless the render-thread capture is provably drained (`waitPureSid1Q1CaptureQuiescent_`), with a `try/catch` around allocation. `copyAndStopPureSid1Q1RecordCapture` fails closed identically. The AU adapter propagates the result; the ViewController already gated on it. |
| P0-2 | RT safety | Every resize/`assign` of `_digiRecordMonoFloatBuffer_v182_` (PureSID, OutputTap, AudioQueue start paths + the PureSID stop/copy path) is now guarded by `_digiWaitForRecordTapCallbacksToDrain_v185_`, so a record buffer can never be resized while a tap callback may still hold its storage. |
| P1-1 | RT safety | `AQCapture` getters (`sampleRate`/`channels`/`requestedUID`/`actualUID`) now read under `queueMutex_`; the `NSString` getters copy-out under lock. Getters are UI-thread-only, so this cannot stall the audio callback. |
| P1-2 | RT safety | `AQCapture::start()` no longer invokes the user message callback while holding `queueMutex_`. Messages are collected under the lock and flushed after release via an RAII flusher (destructor ordering guarantees unlock-before-deliver), removing the re-entrancy/deadlock hazard. |
| #8 / Patch B | C64 telemetry | An unsupported opcode during RSID PHI2 play now sets a dedicated `unsupportedOpcodeHit` (and `approximateOpcodeHit`) result flag instead of being mislabeled as `instructionBudgetHit`. `cpuJammed` is still asserted so the play-complete gate is unchanged. |
| #9 / Patch C | C64 exactness | `notifyTimedWriteOverflow` / `notifyDroppedMultiSidWrites` use saturating 32-bit addition, so a long pathological session can no longer wrap an exactness counter back to zero and silently clear the downgrade condition. |
| P2-1 | Telemetry | `ScopeTripleBuffer` exposes `hasPublished()` so consumers can distinguish "no telemetry yet" from "a genuine zero snapshot". |
| UX | Shared GUI | All rotary controls render at 82% of their previous visual radius while retaining the original hit area. Knobs now expose keyboard/fine-scroll adjustment, discoverable reset gestures, focus handling, and native slider accessibility. |

## Deliberately NOT changed (with reasons)

These audit items were evaluated against the real tree and **not** applied,
because they would break the project's tested contract, depend on external
reference assets, or are design decisions rather than defects:

- **Finding #1 (strict-RSID BRK refusal).** Attempted and reverted: tests
  `C64Phi2RsidExactInitV605` ("RSID init with BRK-halt sentinel succeeds") and
  `AuditCompleteClosureV683` ("BRK sentinel init completes compatibly") lock in
  the *deliberate* design that RSID BRK-sentinel init succeeds and is recorded as
  an `InitBrkSentinel` exactness downgrade (honest-telemetry approach). Changing
  it to a hard refusal contradicts that contract and stops tunes that currently
  play. This is a philosophy difference, not a bug.
- **P1-4 (`max()`→add on the 5 telemetry accessors).** In this tree the PHI2
  bridge and the legacy sink count approximations on *mutually-exclusive
  execution paths* (the bridge never forwards reads to the sink), so `max()` is a
  defensible "which path saw approximations" and the downgrade bit fires either
  way. The audit assumed simultaneous double-counting, which does not occur here.
- **#2/#3/#4/#5 (VBI/continuous-RSID/PSID-CIA timing-model redesigns) and #6
  (NMOS 6502 decimal exactness).** These are cycle-exact timing-model changes /
  hardware-exactness work that require a hardware/VICE reference and would turn
  the green cycle-exact suite red with no in-environment way to verify the new
  behavior is *more* correct. Several are explicitly compatibility-vs-physical
  design tradeoffs.
- **#11/#12/#19 (Klaus-Dormann / Wolfgang-Lorenz / VICE-trace / HVSC corpora).**
  Require external ROMs/test corpora not present in this tree.

Build verification for this release: `281/281 ctest`, VST3 validator `47/47`,
AUv2 component/render/editor-lifecycle smoke tests passed, and strict `auval`
succeeded for all five AUv2 flavors. AUv3/Logic bundles build and pass strict
codesign validation; live PlugInKit registration requires a real Apple
Development identity, which is not installed on this build machine.
