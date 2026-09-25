# v872 audit closure — SIDPLAY render transaction, SynthMode, SID808

Closes the actionable P1 cluster and P2-1 from the v871 audit. All work is verified
by new behavioral/pinning tests plus the full CTest suite (399/399, zero regressions).

## SIDPLAY (C64 render transaction)

### P1-6 — passive PHI2 no longer forges `cpu().state().jammed`
`C64Phi2Machine` gains an explicit `setCpuExecutionSuppressed()` flag honoured in
`tickPhi2()`: CIA/VIC/open-bus and the cycle counter still advance while the 6510 is
frozen, without forging a KIL/JAM fault. `runPsidVbiPassivePhi2Cycles()` uses it
(scoped) instead of the real jammed flag, so a genuine jam stays distinguishable.
Files: `c64_phi2_machine.h`, `c64_psid_runtime.h`.

### P1-4 — rollback-failure recovery policy
On a `rollbackRenderTransaction()==false` (bounded platform journal overflow), the
kernel calls the new `C64Runtime::resyncPlatformFromAuthoritativePhi2()` to re-seed
the platform mirror from the fully-restored authoritative PHI2 state, latches
`c64RenderContaminated_` + a recovery count, and publishes both to telemetry.
Files: `c64_psid_runtime.h`, `ArpSIDDSPKernel.hpp`.

### P1-5 — failure/drop counters reset per tune handoff
New `resetC64FailureCountersForHandoff_()` (invoked in both handoff branches) makes
the run-failure / dropped-play counters read "since load" instead of session-cumulative.
File: `ArpSIDDSPKernel.hpp`.

### P2-1 — continuous-machine (RSID / playAddress==0) telemetry
New `c64Continuous{BudgetHit,CpuJam,UnsupportedOpcode,IncompleteRun}Count_` populated
from the continuous run result and published to telemetry, so free-running partial
commits are visible instead of silent. File: `ArpSIDDSPKernel.hpp`.

### Behavioral proof (new tests)
- `C64SidplayRollbackBehavioralV872Tests` — forced failed **VBI** play under an open
  transaction; asserts PHI2 RAM/cycle, runtime SID sink, and platform mirror all
  restore (P1-1/P1-3/P1-9). Passive-stepping honesty (P1-6) and the resync recovery
  primitive (P1-4). Source-string guards for the kernel P1-4/P1-5/P2-1 wiring.
- `C64SidplayCiaMultiSidRollbackV872Tests` — forced failed **CIA-service** rollback
  (KIL play, `serviceComplete==false`) restores every surface (P1-2); two-SID play
  writing `$D400`+`$D420`+`$D418` and reading `$D41B` rolls back per-chip banks, D418
  and the read-approximation model (P1-3/P1-9).

## SynthMode

### P1-8 — anonymous same-note release policy pinned
The policy was already self-consistent but mis-documented. Anonymous same-note NoteOns
allocate independent voices (`voiceIdentityMatches` refuses to retrigger for nid<0),
and an anonymous NoteOff releases the NEWEST match (LIFO / last-note priority), with the
gate release and held-ledger removal using the same token. Corrected the misleading
"FIFO" comments and documented the pinned policy on `heldTokenForRelease_`.
File: `sid_runtime_voice_policy.h`. Test: `SynthModeAnonymousReleaseLifoV872Tests`
(newest-first release; no stuck gate; anonymous can't steal an id-bearing voice).

### P1-7 — held-replay + release law pinned
`SynthModeHeldReplayReleaseV872Tests` drives the canonical `sidReplayHeldNotes` /
`sidReplayAllNotesOff` law (the single source of truth both AU and VST wrappers call):
held notes replay in press order with distinct non-zero tokens (independent voices),
and a release pass emits a NoteOff for every voice and clears canonical state — no stuck
voice. The full factory-guitar patch-apply chain is an AU-kernel (plugin-build) path;
its allocator-side guarantees are additionally covered here and by v869.

## SID808

### P1-10 — spectral / peak-valley authenticity
`Sid808SpectralAuthenticityV872Tests` lifts the factory-kit guards from envelope shape
to sound identity via a windowed DFT over the rendered 48 kHz output (factory slot 120):
- kick is low-frequency dominant (low band > 4× high band; centroid < 800 Hz) — round
  sub, not a click;
- open hat is spectrally bright (centroid > 2.5 kHz, > 3× the kick centroid) and rings
  longer than the closed hat (more tail high-band energy);
- clap is a genuine multi-burst transient — bursts stand ≥2× above the deep inter-burst
  valleys (peak/valley structure), not one smeared blob.

## Verification
- Full CTest: **399/399 passed, 0 failed** (394 pre-existing + 5 new), reproduced here
  (addresses P2-4). RT-safety lint, render-bridge stack guard, source-tree/dead-file/
  manifest closure guards all green. `scripts/verify_source_tree.py` OK.

## Not closeable in this environment (needs external tooling / plugin build)
- **P2-2** reSIDfp golden-trace harness — requires vendoring reSIDfp as an external SID
  oracle and a trace-comparison rig; multi-day infrastructure, not a source fix.
- **P2-3** C64 physical-exactness blockers — already tracked conservatively via
  `C64_EXACTNESS_BOUNDARIES.md` and the render-published physical-blocker mask; this is a
  labeling stance, not a defect. Exactness labels remain conservative.
- **P2-5** macOS AUv2/AUv3 + auval + Logic/AudioQueue validation — needs the plugin build
  (this config is tests-only) on a macOS AU host; cannot run headless here. Kernel edits
  follow the repo's source-string guard convention and must still pass the plugin build +
  auval before release.
- **P3-1** `ArpSIDDSPKernel.hpp` size / blast radius — a large mechanical refactor,
  orthogonal to correctness; deliberately out of scope for this closure.

---

# v872 P0 audit round — KIT routing, CIA latch, jam telemetry

A second audit pass raised P0s across KIT→SID808 routing, SIDPLAY/PHI2 CIA state,
and the AUv2 wrapper. Each was ground-truthed against the actual source before any
change; findings that did not reproduce are documented as such rather than patched.

## Fixed (verified real, with regression tests)

- **P0 — KIT drum class mis-routed to the wrong SID808 family.** `CompiledKitEvent`
  stores a `KitDrumClass` index (…Rim=5, Tom=6, Cowbell=7, Crash=8); the SID808 render
  path consumed it as a `SidGMDrumClass` (…Cowbell=5, Tom=6, Rim=7). The raw cast
  swapped Rim↔Cowbell and turned Crash (8, no engine case) into silence. Replaced the
  cast with an explicit `kitDrumClassToSidGM()` mapper (`kit_sid808_class_map.h`); Crash
  routes to OpenHat so it fires an audible crash-like cymbal (GM note 49). Test:
  `KitSid808ClassMapV872Tests`.
- **P0-2 — CIA latch integrity falsely reported dirty for custom-tempo tunes.**
  `ciaLatchCorrect` compared `latchA()` against the *default* VBI latch, so any CIA tune
  that programmed its own tempo was flagged dirty / not-clean. Now records the latch
  `installPsidCiaPlaybackBootstrap()` actually installed and validates against it. Test:
  `C64PsidCiaLatchReuseV872Tests::testCustomTempoLatchReportsCorrect`.
- **P1 — VBI failure miscounted CPU jams as budget hits.** A VBI play runs on the PHI2
  machine, but the jam/budget classifier read the *legacy platform* CPU (which runPlay
  never executes), so real KIL/JAM failures incremented the budget-hit counter. Now reads
  `player->phi2Machine().cpu().state().jammed` (before rollback restores it).
- **CIA cold-boot hardening.** `coldBootForSidLoad()` did not reset the CIAs/VIC, so a
  reused `C64Runtime` briefly held the prior tune's CIA1 Timer-A latch after `loadPsid()`.
  A cold boot must reset peripherals; it now does. (End to end the leak was masked —
  `runInit()`'s reset-vector execution reprograms the CIA before the bootstrap heuristic
  runs — so this closes a transient stale-state window rather than an audible bug.) Test:
  `C64PsidCiaLatchReuseV872Tests` (verified to fail without the fix).
- **P1 — `auv2FailRender` under-silenced oversized renders.** It zeroed
  `maxFramesPerSlice` frames; for a TooManyFramesToProcess render (inNumberFrames > max)
  that left a non-zero tail. Now passes the actual frame count (the zero helper already
  clamps to `mDataByteSize`, so this is in-bounds and fully silences the buffer).
- **P1 — GM per-note `velocityScale` ignored.** The direct SID808 MIDI path dropped the
  GM note's `velocityScale`, so GM percussion sharing a SID808 family had no level
  differentiation. Now applied to the note-on velocity.

## Verified NOT an observable bug (documented, not patched)

- **AUv2 `outputFormat` render "race" (flagged P0).** The render path reads exactly one
  field from `outputFormat` — `mChannelsPerFrame` — a single aligned 32-bit value read
  atomically and clamped to [1,2], and every buffer access clamps to the host's
  `mNumberBuffers`. So a concurrent SetProperty yields old-or-new (both valid), with no
  torn read, no out-of-bounds, and no wrong output. The AU contract also forbids format
  changes during active render. No render-critical change made.
- **CIA latch "leak into playback" (flagged P0).** Does not reach playback — `runInit()`
  reprograms the CIA before the bootstrap latch heuristic runs (see above). Only the
  transient post-load window was real; hardened.

## Deferred (real but scoped as follow-ups, with reason)

- Dedicated SID808 Crash/Cymbal family (expand the 8-family engine + factory data);
  Crash is audible today via OpenHat.
- Per-note `tuneOffsetNorm`/`decayScale` shaping for GM toms/congas/bongos/timbales/cuica
  (the per-note SID808 override builder) — a musical enhancement; pitch + velocity already
  differentiate.
- Audit-2 P0-3 (CIA write-count tracking so an intentional $FFFF latch is honoured),
  P1-2 (publish PHI2 inspection on failed VBI), P1-3 (accept RTI-exit CIA completion —
  needs real affected tunes to validate the criteria change), P1-4/P1-5 (telemetry purity;
  clear `loaded_` on late failure).
- Remaining AUv2 hardening (render-block lifetime, ClassInfo validation, MIDI offset
  clamp, transactional ScheduleParameters) — auval passes; these need host stress-tests.

---

# v872 root-cause round — Stop→Play drums, runPlay safety, MIDI clamp

## Fixed (verified real, with regression tests)

- **ROOT CAUSE — "drums stop working after Stop→Play until you reapply the patch."**
  `runtimeRenderHostResetEngines()` resets DrSID/SID808/sidRegister to a blank backend
  but did not re-arm the projection's change-tracking (`firstApply`/`lastMode`/`last*`).
  So the next `projectStateToBackends()` saw "nothing changed" and never rebuilt the
  patch into the freshly-reset backend — silent drums until a manual patch reapply
  forced a full projection. Fixed at the source (covers Play edge, mode transitions,
  panic, and the processor path): the reset now re-arms `firstApply`, guaranteeing a
  full re-projection. Test: `TransportStartBackendReprojectionV872Tests` (drives the
  reset against a mock target; fails without the re-arm).
- **P1-1 — direct `C64Runtime::runPlay()` was not transaction-safe.** A failed/budgeted
  VBI play leaked partial PHI2/sink writes (e.g. a `$D418` write before the routine
  budgets out). The AU render path wraps runPlay in its own transaction, but a direct
  caller/test got contaminated SID state. runPlay() now owns a transaction when none is
  active and rolls back on failure; when the render path already opened one,
  `beginRenderTransaction()` returns inactive so the outer owner keeps sole rollback
  authority (no nested rollback). Test: `C64RunPlayTransactionSafetyV872Tests`.
- **AUv2 MIDI event offset clamp.** `componentMIDIEvent()` cast a `UInt32`
  `inOffsetSampleFrame` straight to `int32_t`; a huge value wrapped negative into
  mode-promotion/ingress. Now clamped to `[0, maxFramesPerSlice-1]` (INT32_MAX fallback).

## Verified NOT present / NOT an observable bug (checked, not patched)

- **"Duplicate `phi2MachineReady_ = true;`" (claimed P2-2)** — there is exactly one such
  assignment in this tree; the duplicate does not exist here.
- **AUv2 `outputFormat` render "race"** — re-confirmed: render reads a single aligned
  32-bit field (`mChannelsPerFrame`), clamped, with all buffer access clamped to the
  host's `mNumberBuffers`. No torn read, no OOB. Not patched.

## Deferred, with reasons

- **CIA intentional-`$FFFF` latch (P0-1) and CRA control-mode preservation (P0-2).**
  Both are real but rare (a PSID-CIA tune deliberately using the slowest Timer-A period,
  or custom CRA mode bits). A correct fix needs to isolate the *tune's* CIA writes from
  init-time resets/writes; the exact init-flow CIA reset behaviour here is subtle
  (the latch reaches `$FFFF` mid-`runInit` via a path not fully pinned down), and getting
  the write-count baseline wrong would break the currently-correct default-tempo path.
  Deferred pending that init-flow trace + real affected `.sid` fixtures. (The higher-impact
  CIA issues — cold-boot reset, custom-latch telemetry — were fixed in the prior round.)
- Audit-2 P1-2 (publish PHI2 inspection/SID mirror on failed VBI), P1-3 (mutating
  validator inside the per-cycle edge-capture loop → use a pure peek), P1-4 (derive PHI2
  vector evidence from the PHI2 machine, not stale platform), P2-1 (`psidVbiFrameCycles`
  for direct RSID) — real but low-impact / diagnostic; deferred.
- Remaining AUv2 hardening (RCU render-block lifetime, no in-place host `AudioBufferList`
  mutation, ClassInfo CF-type validation, transactional ScheduleParameters, param-cache
  states, chunker live play-period, jam authoritative PHI2 CPU, render-notify drop
  counter, close-block formatting) — auval passes; these need host stress-testing to
  validate and are deferred as a hardening batch.

---

# v872 SID808 audit round — hygiene fixes + design-scale assessment

## Fixed (verified, with regression tests)

- **P2-1 — stale test used the pre-v872 bad enum cast.** `kit_mixed_target_runtime_v650`
  still did `static_cast<SidGMDrumClass>(ev.drumClass)` — the exact swap-Rim/Cowbell /
  silence-Crash bug production fixed — so it could mask a routing regression. Replaced
  with `ArpSID::GUI::kitDrumClassToSidGM()`. (Its SID808 event is a Kick, where the cast
  and converter agree, so the fix is behaviour-preserving.)
- **P2-5 — `Sid808Engine::prepare()` did not reset active drum state.** A device /
  sample-rate change left active gates, auto-release timers and microstage schedules
  timed for the OLD rate → stuck / mistimed hits until the host issued an all-notes-off.
  prepare() now clears active state first. Test:
  `Sid808PrepareResetsActiveStateV872Tests` (active voice before prepare, zero after).

## Assessed as DESIGN-SCALE, not quick bugs (deferred with rationale)

The v872 routing P0 (Rim/Cowbell/Crash) is fixed. The remaining audit items are almost
all one design limitation, not separate defects: **the SID808 engine has 8 drum families,
while the GM map exposes ~40 instruments**, so bongos/congas/timbales/cuicas render as
pitched Toms, crash/ride/splash/china as OpenHats, tambourine/cabasa/maracas/guiro as
ClosedHats, agogo/triangle/ride-bell as Cowbells, claves/woodblocks/vibraslap as Rims.

Closing this properly is a **feature**, not a patch, and it must not be rushed into the
shipping render path because it changes the actual synthesised sound (and would churn the
v871/v872 spectral/shape guards). It needs, in order:

1. **GM-note semantic projection layer** (P1-3/P1-4): a base-config-aware function that
   folds the authored `tuneOffsetNorm` / `decayScale` / `velocityScale` into a
   `Sid808HitOverride`, applied in BOTH the direct-MIDI and KIT paths, with user KIT
   overrides winning last. This needs the drum's base config (or engine cooperation),
   because the override REPLACES rather than scales — the reason it wasn't a one-liner.
2. **Split the overloaded Tom** (P1-2) into Tom/Bongo/Conga/Timbale/Cuica sub-profiles
   (distinct pitch-drop / body / attack programs, still on physical voice 0).
3. **Real cymbal families** (P1-1/P1-5/P1-6) — Crash/Ride/Splash/China (+Triangle) with
   profile-bounded frequency ranges, not raw chromatic pitch off OpenHat's canonical note
   (which clamps high GM notes to $FFFF and collapses distinct instruments).
4. **Envelope-scaling refactor** (P1-7…P1-10): stage code should scale from the factory /
   user envelope instead of overwriting it, so factory kit AD/SR actually shapes the sound.

Lower-priority / debatable, also deferred: P1-11 (snare sustain force-clear — may be
intentional one-shot; UI should hide it if so), P1-12 (direct-MIDI identity repair can use
default kit data if the slot wasn't loaded — needs the kit-load-flow contract nailed down),
P1-13 (KIT accent + engine accent double-shaping — decide one accent policy), P2-2/P2-3
(one-shot sustain / triangle pulse-width table cosmetics), P2-4 (scheduled-note overflow
should surface in UI telemetry).

Recommendation: schedule items 1–4 as a dedicated "SID808 GM percussion" feature with its
own spectral/identity test suite, rather than incremental tweaks to the shipping build.

---

# v872 CIA intent-detection round — P0-1 / P0-2 now fixed

Previously deferred (needed the init-flow CIA behaviour pinned down). Resolved via
monotonic CIA write counters, and verified with a probe across the init flow.

- **P0-1 — an intentional Timer-A latch of `$FFFF` was clobbered.** The bootstrap used
  `latchA()!=0xFFFF` as "the tune programmed its tempo", which cannot tell an intentional
  max-period `$FFFF` latch from the reset sentinel — so such a tune was forced to the
  50/60 Hz default.
- **P0-2 — the tune's Timer-A control mode (`$DC0E`) was clobbered.** The bootstrap
  unconditionally forced CRA to continuous (`$11`), destroying one-shot / CNT-source /
  PB6 / serial modes even when the latch was preserved.

**Fix.** `Cia6526` now keeps monotonic write counters for Timer-A latch (`$04/$05`) and
control (`$0E`) writes (intentionally never reset, so a mid-init CIA reset can't corrupt a
delta). `markSidInitStart()` baselines them; `installPsidCiaPlaybackBootstrap()` then
detects tune-authored writes from the delta **at the point it runs** (before its own
writes) rather than from the final latch value. It preserves a tune-written latch
(including `$FFFF`) and, when the tune wrote `$DC0E`, preserves its control mode (adding
only start + force-load) instead of forcing `$11`. The write-count approach was necessary
because a probe showed the delta across the *whole* `runInit` also counts the bootstrap's
own writes — only a start-of-init baseline isolates the tune.

Verified (probe, then regression tests in `C64PsidCiaLatchReuseV872Tests`):

```
noCIA      -> latch 4CF9  CRA 01   (default installed)
latch1234  -> latch 1234  CRA 01   (latch preserved)
latchFFFF  -> latch FFFF  CRA 01   (P0-1 fixed — was 4CF9)
cra09      -> latch 1234  CRA 09   (P0-2 fixed — one-shot preserved, was 01)
```

All 69 CIA/platform/runtime regression targets pass; kernel adapter compiles.
