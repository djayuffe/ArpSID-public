# ArpSID 0.0.690 pass380 v896 — sync/BPM/transport/state audit closure (clean) + release packaging

Final verification pass over the remaining unaudited authority domains from
the v893–v895 series: host tempo/BPM plumbing, transport handling, and state
save/restore split-brain. **No defect found** — this closure documents the
audit and ships the packaged release.

## Audited clean — tempo/BPM

- One tempo authority: `runtimeHostSurface_().hostTempo`, sanitized via
  `canonicalClampedTempo` at ingestion; canonical `TempoChange` events flow
  through the single `onTempoChange` sink.
- LFO and arpeggiator tempo sync use the correct law
  (`rateHz = (bpm/60)/division`, NaN/zero-safe, clamped); the projection path
  and the per-block `syncTempoLinkedRuntimeControllers` apply the same
  tempo-sync rule (arp rate < 0.005 → tempo-locked), so the two call sites
  cannot disagree.
- The SID-808 sequencer receives the live BPM every block (stale-swing fix
  retained), not only at sequencer-advance time.

## Audited clean — transport

- `handleTransportDiscontinuity_` detects start/stop edges and host seeks
  with a jitter tolerance calibrated to the ~5 ms AUv2 musical-context
  poller (documented: max(0.10 beats, 1.5 render-blocks)) — no false
  sequencer restarts on poll boundaries, real seeks still detected.
- Transport start clears runtime performance state and seeds the arp PRNG
  from the host beat position (deterministic DAW bounces).
- Transport stop runs the all-notes-off performance reset AND
  `resetTransientRenderState_`, which clears the `SidWriteQueue` — combined
  with the v894 rebase law, no scheduled write can survive a transport edge
  and fire on the wrong timeline.

## Audited clean — state/split-brain

- Every destructive edge clears pending scheduled SID writes in BOTH hosts
  (AU kernel and Phase2/VST): panic/all-notes-off reset, prepare/reset,
  transport stop, and canonical state restore.
- The state-restore path (applyStateRootCanonical → projectStateToBackends)
  carries the documented AU shadow/renderParams staleness fix: restore
  explicitly resets synth voices, clears the queue, and reseeds synth-mode
  realtime state rather than waiting for the next parameter flush.
- Serialization state roots gained no new fields in v893–v895 (the write
  queue is deliberately transient; bootstrap-length constants are static).

## Stale test-fixture refreshes (found by the full 442-suite CTest)

The packaging gate's full CTest surfaced two more pre-v612 RSID fixtures
using flag `$0002` (the C64 BASIC bit, now honestly refused) where a plain
PAL RSID was intended — the same idiom already refreshed in v261/v265 during
v893, missed by the family-prefix sweeps because of their filenames:

- `release_final_closure_v404_tests.cpp` (`ReleaseFinalClosureV404Tests`)
- `release_runtime_closure_v408_tests.cpp` (`ReleaseRuntimeClosureV408Tests`)

Both now use the video-standard bit (`$0004`). No production code changed.

## Verification

- No production-code change in this pass.
- **Full CTest: 442/442 PASS** (first full-suite green run since v871;
  includes `C64SidBitExactV893Tests`, `SidWriteQueueRebaseV894Tests`,
  `SidPulseComparatorParityV895Tests`).
- This pass produces the packaged release zip and reinstalls/validates the
  AUv2 component — see STATUS.md for the zip path/sha256, installed hash,
  and per-subtype strict `auval` results.
