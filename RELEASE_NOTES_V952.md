# ArpSID v952 — runtime behavior/parity closure

V952 closes the final audited Classic/BitPerfect, DrSID/SID808, state-root and
parameter-ingress gaps with executable behavior coverage.

## Runtime fixes

- SID808 KIT step zero is re-armed at setup/teardown, state-root apply,
  transport start/stop, render-mode transition, and sequencer disable.
- A stopped follow-host transport no longer auditions or consumes KIT step zero.
- Queue-full parameter fallback carries a coherent per-parameter generation,
  applies through the ordinary execution-owner side-effect path, and suppresses
  older queued generations after the latest fallback wins.
- Complete AU3 and Phase2 state-root installation reconciles Filter Drive,
  ARP/SEQ tempo policy, limiter and reverb adapter state before final projection.
  The AU3 realtime path still only swaps a producer-canonicalized root; no new
  allocating root canonicalization or extra root mutation was added there.

## Parameter contract and host metadata

- `sanitizeNormalizedParamValue()` now enforces parameter-specific boolean,
  enum, sequence-length, note/controller, program/bank, modulation-source and
  SID-byte semantics.
- Legacy equal-width/floor-binned waveform, filter and arp-octave values are
  canonicalized without changing the selected DSP meaning.
- AUv2 and VST3 discrete step counts use the shared cardinality contract. AU3
  boolean units use the shared boolean contract.
- Incorrect local metadata for filter, arp, LFO and sequencer length was removed.

## Mode/telemetry decision

C64 SID Player remains a component/player authority, not a new internal synth
render mode. The three-mode resolver stays BitPerfect, SID-register and DrSID;
`psidActive`/C64 runtime telemetry plus the unified audible-authority UI expose
the product authority separately.

## Validation

- New `RuntimeBehaviorClosureV952Tests` renders the real kernel and verifies
  SID808 KIT audio after Play, Stop→Play and AU reset; stopped-transport gating;
  dirty-fallback side effects/ordering; and semantic normalization.
- Production mixed signed/unsigned loops found by the warning audit were fixed.
- Clean local source build passed.
- Local AUv2, AUv3 and standalone bundle compilation/linking passed (not installed).
- Full local CTest passed: 465/465.

Platform AU/Logic runtime, VST3 SDK/toolchain, signing/notarization and installer
validation remain external release sign-off items.
