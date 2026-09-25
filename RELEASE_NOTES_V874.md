# ArpSID v874 — extra-SID routing / validator / reset closure

Deep timing-authority audit follow-up. Three confirmed, source-verified fixes to the
C64 SID-player path (the rest of the audit was assessed and deliberately deferred —
see "Deferred / not-a-bug" below).

## Fixed

### P0-1 — extra SID at `$DE00-$DFE0` is now routed by the runtime bus
`C64Platform`'s IO read/write/peek routers gated all SID handling behind
`isSid(a)`, which only covers the primary mirror `$D400-$D7FF`. A stereo/3-SID tune
with SID2/SID3 at a legal `$DE00-$DFE0` base parsed fine but its register writes fell
through to unmapped IO and were silently dropped — that chip never entered the SID
authority. The routers now key off `addressInConfiguredSidWindow_(a)`, which matches
the primary mirror OR any configured extra-SID window, so `$DExx/$DFxx` secondary SIDs
route to the correct chip/register. `sidAddressToChipReg_()` already handled configured
bases; only the entry guard was too narrow.

### P0-2 / P1-9 — one SID-base validator shared by parser and runtime
`configurePsidSidBases()` and `sidBasesValidUnique_()` accepted any `$D400-$DFE0`
base with `$20` alignment — including `$D800` (color RAM), `$DC00` (CIA1) and `$DD00`
(CIA2), which the parser rejects. Both now defer to the single source of truth
`ArpSID::psidValidConfiguredSidBase(base, chip)` (primary fixed at `$D400`; extras via
`psidValidExtraSidBase`), eliminating the parser/runtime split-brain.

### P0-3 — primary SID hard-reset on PSID/RSID handoff
The handoff reset loop reset secondary engines (chips 1..4) but skipped chip 0, so the
primary SID kept its internal oscillator phase / envelope pipeline / filter integrator /
noise LFSR across a tune→tune swap (register reseed restores latches, not internal DSP
state) — a load-order-dependent startup transient. Chip 0 is now reset symmetrically;
the register reseed immediately after keeps it consistent with chips 1..4.

## Tests
`c64_extra_sid_de00_routing_v874_tests` — proves `$DE00`/`$DFE0` write routing, the
"not routed until configured" and "CIA writes don't leak into SID" boundaries, and the
tightened validator (accept `$D420/$DE00/$DFE0`; reject `$D800/$DC00/$DD00`; primary must
be `$D400`). Discrimination-verified: reverting the routing guard fails the test.

## SID projection write-authority audit — verdict + scoped fix

A separate deep audit claimed the SynthMode projection path has "multiple write
authorities" (note-on/off, glide, pitch bend, aftertouch bypassing the canonical write).
Verified against source:

- **Audio is correct.** The scheduler/performance helpers feed the audio queue from
  authoritative *voice state* — pitch bend from `v.midiNote`
  ([sid_runtime_synth_performance.h:28](include/arpsid/core/sid_runtime_synth_performance.h)),
  aftertouch from `v.sustainNibble/releaseNibble`, note-on freq from `v.currentSidFreqReg`.
  They do **not** read the queued shadow, so the audit's "bends/aftertouches from a stale
  value" claims are false for the audio path (only the no-active-voice fallback touches it).
- The shadow-skip staleness is in the **safe** direction (redundant write, never a wrong skip),
  and control-critical transitions are **not** coalesced (distinct cycle offsets).
- The real divergence was **telemetry-only**: the C64 mirror was fed from only a subset of
  projection call sites, so it looked incomplete/late in debugging even though the sound was
  right — and a comment overclaimed it as a "deterministic parity" authority.

### Fixed (scoped, telemetry-only)
The C64 telemetry mirror is now driven from the **applied-write observer** of
`renderSidRegisterQueueToStereo()` instead of a subset of push sites, so it reflects exactly
what the audio engine consumed — including the scheduler/performance writes. Single-sourced
(the push-time `mirrorSidProjectionWriteToC64_` call is removed) and **demand-gated**: the
observer only feeds the (bounded) platform CPU queue on blocks where a cockpit is actually
consuming the mirror, using the same `cockpit-demand && audio-active` signal that advances the
mirror CPU (armed one block ahead). No scheduler code touched; no audio-path change.

Test `sid_projection_applied_write_observer_v874_tests` proves the observer sees every applied
write exactly once, in applied order, with all four control-byte transitions preserved.

### NOT done (deliberately, with your sign-off)
The audit's full `SidProjectionWriter` refactor + C64-vs-synth SID-engine split is a large
rewrite of the hottest RT path to fix a non-audible concern. It needs staged work and on-device
audio verification; not warranted for a telemetry issue and not done here.

## Deferred / not-a-bug (assessed, intentionally not changed)
- **P0-4 (re-anchor to host `mSampleTime`)** — the C64 player runs a free-running,
  monotonic sample cursor; its timed writes come from the tune's own play cadence, not
  host transport position. Re-anchoring on a transport jump risks *introducing* audible
  discontinuities via false-positive resets. Not clearly a bug for this architecture.
- **P0-5 (split audio cursor from telemetry cursor)** — latent fragility only; the
  render path always drives the telemetry publisher today. Correct-but-low-value refactor.
- **P1-1/P1-2/P1-3 (3-vs-5 model width, exclusive C64 authority, per-block reseed)** —
  intentional/documented design, not defects.
- **P1-4/P1-6 (carry future writes / roll back partial CIA frames)** — deliberate
  overload/continuity behavior; changing it is a musical-tradeoff decision, and the
  relevant counters already exist.
- **P1-5/P1-7/P1-10 (surface overflow, first-block chunk clamp, per-chip telemetry)** —
  additive diagnostics / minor clamps; deferred pending a decision to prioritize them.
