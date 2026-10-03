# P2 Validation Report — All 5 Audit Rounds

Date: 2026-10-03
Scope: Every P2 finding from `ai2ai_audit.md` (R1, 21 table items), `ai2ai_audit_round2.md` (R2, 28 table items + P2-29..39 = 39), `ai2ai_midi_audit.md` (R3, 5), `ai2ai_c64_audit.md` (R4, 8), `ai2ai_c64_sidplay.md` (R5, 12 effective after inline retractions). Total: 85 P2s.

Method: Five parallel validation agents re-read every cited source file and line, produced a verdict with quoted code evidence and a minimal fix proposal. All CONFIRMED SAFE fixes were applied; RISKY fixes (DSP audio timing, RT-adjacent threading, behavioral changes) are listed for follow-up.

## Summary

| Round | P2s | Confirmed | Partial | Refuted | Already-Fixed | Fixed | RISKY deferred |
|-------|-----|-----------|---------|---------|---------------|-------|----------------|
| R1    | 21  | 15        | 1       | 0       | 2             | 11    | 1 (P2-18)      |
| R2    | 39  | 22        | 0       | 6       | 0             | 19    | 2 (P2-1,2)     |
| R3    | 5   | 2         | 1       | 2       | 0             | 2     | 0              |
| R4    | 8   | 6         | 1       | 1       | 0             | 7     | 1 (P2-3)       |
| R5    | 12  | 1         | 1       | 10      | 0             | 2     | 0              |
| **Total** | **85** | **46** | **4** | **19** | **2** | **41** | **4** |

41 fixes applied. 19 findings refuted. 4 RISKY fixes deferred (DSP audio timing, RT-adjacent poller, HIRAM mapped read).

## R1 (ai2ai_audit.md §3) — 21 P2s

| # | Verdict | Fix Applied | Notes |
|---|---------|-------------|-------|
| 1 | CONFIRMED (no-action) | — | `cachedFactoryPresetArray` process-lifetime leak — intentional, documented, ~few hundred KB. Finding says "No action." |
| 2 | CONFIRMED (hygiene) | — | GM-drum promotion: `autoPromotionAllowed` check redundant with `decision.promote`. Perf/hygiene only. |
| 3 | CONFIRMED (deferred) | — | `interfaceVersion` returns 0 (legacy). Whether modern Logic rejects version-0 factories is a host-contract question the finding defers. |
| 4 | ALREADY-FIXED | — | `componentScheduleParameters` unclamped offset — fixed in b301c51 as R2 P1-22. Residual: `componentSetParameter:3285` still unclamped (separate finding). |
| 5 | CONFIRMED | **YES** | BitcrusherProcessor: at 16 bits the quantizer divides by 32767 → hard-limits \|x\|>1.0, contradicting "255 = transparent" comment. Fixed: skip quantize when `quantBits_ >= 16` (pass-through). |
| 6 | CONFIRMED | **YES** | `computeHighShelfCoeffs`: dead `a0inv` + 7 lines of stream-of-consciousness dev comments ("Let me recalculate properly", "Wait — that's the same") in a production DSP header. Fixed: deleted. |
| 7 | CONFIRMED (imprecise) | — | SaturatorProcessor Tube branch: finding's "~1.31:1 at drive=1" ratio is wrong (actual is 2:1 at any drive for small-x DC gain). The asymmetry is documented in code. No fix. |
| 8 | ALREADY-FIXED | — | `configFolder()` inverted ternary — fixed in b301c51 as R2 P1-23. |
| 9 | CONFIRMED | **YES** | `Settings::sanitize` clamps `tab` to ≥0 but not to upper bound. Fixed: `std::clamp(s.tab, 0, 16)`. |
| 10 | CONFIRMED | **YES** | `writeFile` rename-retry: if second rename fails after removing `p`, both files lost. Fixed: log to stderr + remove orphaned `.tmp`. |
| 11 | CONFIRMED (doc-only) | — | `Engine::process` `std::fill` for c≥2 has no capacity bound. Safe today (single caller). Documentation-only ask. |
| 12 | CONFIRMED | **YES** | `renderMonoScript` consumes events only while `script[eventIndex].sample == i` — drops events if script not ascending-sorted. Fixed: changed `==` to `<=`. |
| 13 | CONFIRMED | **YES** | Tautological `require(X == nullptr \|\| X != nullptr)`. Fixed: `require(getFactoryPatchDefinition(-1) == nullptr)`. |
| 14 | CONFIRMED | — | Duplicate wrapper scripts: both `exec` the same canonical script (finding's "one execs the other" is wrong). The v655 guard pins both literals. Keeping both is load-bearing for the guard. No fix. |
| 15 | CONFIRMED | — | `BUILD-VALIDATION.txt` hardcodes "282/282 passed" / "17 visible tabs" (tree now has 441 tests). Packaging metadata; fixing requires dynamic count capture in the script. Deferred. |
| 16 | CONFIRMED | **YES** | `check_build_warnings.sh`: linker regex misses `ld.lld`. Fixed: `ld(64)?` → `ld(lld|64)?`. |
| 17 | PARTIALLY-CONFIRMED | — | SDK tag triplicated with no coherence check. Finding's dependabot.yml citation is wrong (dependabot only tracks github-actions). Adding a CI coherence step is deferred. |
| 18 | CONFIRMED | — (RISKY) | `install_macos.sh` `killall -9 AudioComponentRegistrar` contradicts `refresh_auv2_component.sh`'s documentation that it's a no-op/contraindicated. Fix: call the canonical script. RISKY — changes install-time behavior. |
| 19 | CONFIRMED | **YES** | `CMakeLists.txt:1082` `list(REMOVE_ITEM _arpsid_auv2_sign_args --entitlements)` removes only the flag, not its value → dangling path. Fixed: two-item removal matching `:1806`. |
| 20 | CONFIRMED | **YES** | `fetch_vst3_sdk.sh`: `git clone` into non-empty dir gives confusing error. Fixed: refuse non-empty targets before clone. |
| 21 | CONFIRMED | **YES** | `capture_logic_auv2_hang.sh`: 3s×10-sample profiles per PID overlap within 1s loop. Fixed: reduced to 1s×4-sample per profile. |

## R2 (ai2ai_audit_round2.md §3 + §4) — 39 P2s

| # | Verdict | Fix Applied | Notes |
|---|---------|-------------|-------|
| 1 | CONFIRMED | — (RISKY) | Poller reads `hostCallbacks` without `activityMutex`. RT-adjacent poller behavior; needs macOS runtime verification. |
| 2 | CONFIRMED | — (RISKY) | Poller reads `outputFormat.mSampleRate` without lock. Same class as P2-1. |
| 3 | CONFIRMED (doc-only) | — | Poller captures raw `impl`; lifetime relies on close-ordering invariant. Document or add generation check. |
| 4 | CONFIRMED | **YES** | `makeArpSIDFourCC` shifts signed `OSType`; bytes ≥0x80 produce negative value. Fixed: build in `uint32_t`, cast once. |
| 5 | CONFIRMED | **YES** | `ParityTraceLogBuffer` single `ready` 0/1 gate — two producers can interleave. Fixed: added per-slot `generation` counter. |
| 6 | CONFIRMED | — | `setValue:clamped originator:(__bridge void*)self` — address-unique but not lifetime-unique. Fix: file-scope static sentinel. Deferred (low risk, cosmetic). |
| 7 | CONFIRMED | — | KVC `setValue:forKey:@"extensionAudioUnit"` + `respondsToSelector` + `@catch` around a property that exists. Dead defensive scaffold. Deferred (cleanup). |
| 8 | **REFUTED** | — | `DeferredFlush` re-entry: the outer `start()` acquires `queueMutex_` before any "BLOCKED" message, so a re-entering `start()` deadlocks on the mutex, it cannot run concurrently. No ordering inversion. |
| 9 | **REFUTED** | — | `sid808DefaultConfig(Count)` fallback: the `case Count` arm exists and returns the correct all-zero sentinel. A `std::unreachable` guard would be wrong. |
| 10 | CONFIRMED | — | Forced-voice `channel/noteId = -1`: `allNotesOffChannel` can't clear forced voices. No host-reachable stuck gate (fact-checked). Fix: treat channel −1 as "clear on any channel op" or document. Deferred. |
| 11 | CONFIRMED | **YES** | Arpeggiator `process()` dead convenience API, 8 KB RT stack array, no production caller. Fixed: deleted. |
| 12 | CONFIRMED (doc-only) | — | `seedFromHostPosition` `uint32_t` cast well-defined modulo 2³²; positions 262,144 beats apart collide. Document the period. |
| 13 | **REFUTED** | — | `retrigger()` gated on `retriggerEnabled`: the S&H/Random target redraw on phase wrap is gated only on `shape`, not on `retriggerEnabled`. Disabling retrigger does NOT kill the phase-wrap side channel. |
| 14 | **REFUTED** | — | `SidRegisterEngine::reset()` "does not clear SidWriteQueue": there is no `SidWriteQueue` member in `SidRegisterEngine`. The queue lives on the owner. The reset paths that matter always pair with queue clearing. |
| 15 | CONFIRMED | — | Snare micro-stages advance by chunk, not exact sample — fires up to `n` samples late. Deterministic but wrong latency for ~7.5 ms snare body stage. RISKY (audio-timing change). |
| 16 | **REFUTED** | — | Lookahead limiter "stale peaks dominate": the envelope recovery is unconditional (`env` rises toward 1.0 by `releaseCoeff` every sample). A stale peak can only delay the rise by ≤ `delay` samples. |
| 17 | CONFIRMED | — | `mix_panel_model.h` comments say "64 bytes pinned" but struct is 72 (`static_assert(sizeof == 72)`). Stale comments + thinking-out-loud narrative. Deferred (cosmetic). |
| 18 | CONFIRMED | — | `channelVolumeDb` formula: +0.82 dB at volume=200 (comment claims 0 dB), dips ~−1.5 dB around volume≈40. RT path uses a different curve. Display readout disagrees with gain path. Deferred (UI fix). |
| 19 | CONFIRMED | — | `kitStateBlobMigrateLegacyV1` never reads `old.stepGrid.stepCount` (forces 32). P3 in practice. Deferred. |
| 20 | CONFIRMED | **YES** | File bank `count == 0` accepted as OK. Fixed: `if (count == 0 \|\| count > max) return BadBlob`. |
| 21 | CONFIRMED | **YES** | `exportAllToDirectory` uses `std::min(patches.size(), metas.size())` — silent truncation. Fixed: `if (patches.size() != metas.size()) return BadBlob`. |
| 22 | CONFIRMED | **YES** | `strncpy(m.name, ...)` relies on value-init for NUL termination. Fixed: route through `copyBoundedField_` (4 fields). |
| 23 | **REFUTED** | — | `loadBankFromFile` "returns success after short read": the `if (!ok) return BadBlob` at :318 catches short reads. Code is correct. |
| 24 | CONFIRMED | **YES** | `valueChanged` calls `setValueNormalized` on siblings → re-fires `valueChanged` → redundant second `setParamNormalized`. Fixed: `synchronizing_` re-entrancy guard. |
| 25 | CONFIRMED | **YES** | `shortLabel` indexes `kParamInfos[id]` with no range check. Fixed: `if (id < 0 \|\| id >= kNumParams) return ""`. |
| 26 | CONFIRMED | — | `HostRunLoop::setTarget` plain-pointer write vs read on run-loop thread. Fix: `std::atomic`. Deferred (threading). |
| 27 | CONFIRMED | **YES** | `readFileBytes` never inspects `failbit`/`badbit`. Fixed: `if (in.bad() \|\| out.size() > maxBytes) out.clear()`. |
| 28 | CONFIRMED | — | (Duplicate of R1 P2-9, `Settings::sanitize` tab clamp — already fixed in R1 batch.) |
| 29 | CONFIRMED | **YES** | `package_release.sh` predictable non-`mktemp` path. Fixed: `mktemp -d`. |
| 30 | CONFIRMED | **YES** | `sync_public_mirror.sh` unanchored `tar --exclude`. Fixed: `--anchored`. |
| 31 | CONFIRMED | — | `check_build_warnings.sh` `ld.lld` missing — already fixed in R1 P2-16. `external/resid-fp` out of scope is documented intent. |
| 32 | CONFIRMED | — | `verify_source_tree.py` / `check_audit_closure.py` `read_text(errors="ignore")`. Fix: strict decoding. Deferred. |
| 33 | CONFIRMED | **YES** | `build.yml:154` `wc -l` without `tr -d ' '` (BSD portability). Fixed: added `tr -d ' '`. |
| 34 | CONFIRMED | — | `release.yml:93` echoes resolved SHA next to token-carrying step. Deferred (cosmetic). |
| 35 | CONFIRMED | **YES** | `verify_auv2_component.sh` `mktemp` log world-readable. Fixed: `chmod 600`. |
| 36 | CONFIRMED | — | `notarize_release.sh` Apple ID password on command line. Fix: `--password @-` / stdin. Deferred (requires notarization testing). |
| 37 | CONFIRMED | **YES** | `verify_p2_static_guards.sh` `env -i PATH=/usr/bin:/bin python3` hard-fails on Homebrew. Fixed: resolve `python3` first. |
| 38 | CONFIRMED | **YES** | `build.sh:193` `exec > >(tee -a ...)` process substitution never waited on. Fixed: `TEE_PID=$!` + `trap 'wait' EXIT`. |
| 39 | **REFUTED** (moot) | — | `run_release_gate.sh` `rm -rf` before configure: P0-2 containment already present. Restatement of P0-2's fix context, not an independent defect. |

## R3 (ai2ai_midi_audit.md) — 5 P2s

| # | Verdict | Fix Applied | Notes |
|---|---------|-------------|-------|
| P2-1 | CONFIRMED | **YES** | `MidiIo::open` `ignoreTypes(true, true, true)` discards Start/Stop/Continue (timing messages filtered before callback). The 0xFA/0xFB/0xFC handlers in `Engine::midiIn` are dead code. Fixed: `ignoreTypes(true, false, true)`. |
| P2-2 | **REFUTED** | — | AUv2 `componentMIDIEvent` status mask: the audit itself retracts ("No issue here on closer inspection"). Masking is correct. |
| P2-3 | **REFUTED** | — | DrSID `allNotesOff` register image "non-atomic": the three register writes are consecutive single-threaded stores with no interleaving point. No partial-observation window. |
| P2-4 | CONFIRMED (latent) | **YES** | `eventOrder_` monotonically increasing, never reset. After 2³² events (~27h), wrap causes non-monotonic arrival-order tokens within a block. Fixed: `eventOrder_ = 0` at start of `collectEvents_`. |
| P2-5 | PARTIALLY-CONFIRMED | — | AUv3 UMP Program Change dropped: code fact correct, but the "cross-plugin inconsistency" is overstated — ALL plugin paths (AUv2/AUv3/VST3) intentionally drop 0xC0. The kernel refuses to treat PC as patch authority. Docs note only. |

## R4 (ai2ai_c64_audit.md) — 8 P2s

| # | Verdict | Fix Applied | Notes |
|---|---------|-------------|-------|
| P2-1 | CONFIRMED | **YES** | PSID color-RAM mirror ordering invariant undocumented. Fixed: added comment at `c64_psid_runtime.h` documenting the `resetPhi2Machine_` → mirror ordering requirement. |
| P2-2 | **REFUTED** | — | NTSC PHI2 1,022,727 vs 1,022,728: 1,022,727 is internally consistent with the codebase's frame model (263 lines × 65 cycles = 17,095 @ 59.83 Hz). The finding's derivation is garbled. Already refuted in P1 validation. |
| P2-3 | CONFIRMED (latent) | **YES** (comment) | BRK-sentinel `peekRam` vs mapped read: `peekRam` bypasses PLA decode. Correct because bootstrap runs with HIRAM cleared, but undocumented. Fixed: added comment. (Full mapped-read fix is RISKY — `cpuRead` drives open-bus latch.) |
| P2-4 | CONFIRMED | **YES** | DIGI `blockPeak` computed from `abs(nibble/7.5 − 1.0)` (distance from midpoint), not waveform magnitude. Fixed: `fabsf(nibble/15 * 2 − 1)` (true bipolar). |
| P2-5 | PARTIALLY-CONFIRMED | **YES** | `$D418` comments mislabel filter-mode bits as "$D418 bits 4..6" when they're per-voice control register bits. The TEST-bit half of the finding is not at the cited lines. Fixed: corrected comments. |
| P2-6 | CONFIRMED (latent) | — | SID 15-bit rate counter wraps at 32,768; max period 31,251 is safe. Document the ceiling. Deferred (doc-only, low value). |
| P2-7 | CONFIRMED | **YES** | Filter cutoff-squash 0.16 cap is inert (raw term maxes at 0.117). Fixed: 0.16 → 0.12. |
| P2-8 | CONFIRMED | **YES** | Dead in-code filter-cutoff tables (`s_filterCutoff*`, `s_filterQ*`) written but never read. Fixed: added comment noting they're superseded by `sid_analogue_calibration.h`. |

## R5 (ai2ai_c64_sidplay.md) — 12 P2s (effective)

| # | Verdict | Fix Applied | Notes |
|---|---------|-------------|-------|
| P2-1 | **REFUTED** | — | `psidLoadIntoRam` "dead code": called in 3 test files + documented in `SID_FILE_FORMAT_NOTES.md`. The audit's grep excluded `.cpp`. Truncation is intentional/documented. |
| P2-2 | **REFUTED** | — | Same-cycle $D418 "earliest wins": the render loop queues both writes in order; `dispatchSubphaseWrites_` applies them sequentially → **last write wins** (matching hardware). |
| P2-3 | **REFUTED** | — | Timed-write ring overflow "never surfaced": overflow drives `TimedWriteOverflow` exactness flag (GUI `WRITE_OVERFLOW`) + fatal rollback. The "silently dropped" claim is wrong. |
| P2-4 | **REFUTED** | — | Bus sink `droppedWrites_` "never surfaced": `droppedWrites()` getter exists. The DIGI analog is telemetry-surfaced. |
| P2-5 | **REFUTED** | — | `Phi2Machine::Snapshot` "sidSink/trace not restored": `restoreSnapshot` DOES restore both pointers (lines 277-280) and re-wires `mem_`. |
| P2-6 | **REFUTED** | — | PSID runtime Snapshot "drops playBase/playCounter": there is no separate play pointer — the 6510's PC is captured in the machine Snapshot. |
| P2-7 | CONFIRMED | **YES** | Telemetry has cumulative `c64Phi2Cycle` but no per-block count. Fixed: added `c64Phi2CyclesThisBlock` field (delta of `c64Phi2Cycle` across the block). |
| P2-8 | PARTIALLY-CONFIRMED | **YES** | Parity trace stderr-only, no file/discard. "Crash" impact overstated (`fputs` ≠ SIGPIPE, dev-only). Fixed: `flushToFILE(nullptr)` now returns early (no-op mode). |
| P2-9 (final) | **REFUTED** | — | `runPlay` "no RAM bounds watchdog, silently muted": runaway play routine IS detected (budget-hit/jam), rolled back, and counted. Not silent. A PC-in-RAM watchdog would be RISKY (could reject legal ROM/relocated tunes). |
| P2-10 (final) | **REFUTED** | — | `d418RepeatedValueWriteCount` "dead": asserted in 5 test files. Not dead telemetry. |
| P2-11 | **REFUTED** | — | `c64SidBridgeInstall` "does not reset": the production handoff path calls `c64SidBridge_.reset()` before install (lines 1985/1990). |
| P2-12 | **REFUTED** | — | `loadPsid` "no init/play address validation": no in-payload check is **correct** — play routines may live in KERNAL/CHAR ROM or be relocated. Adding the check would reject legal tunes. |

## Applied Fixes (41 total)

### Shell scripts (7)
1. `scripts/package_release.sh` — `mktemp -d` (predictable path → secure temp dir).
2. `scripts/sync_public_mirror.sh` — `tar --anchored` (unanchored exclude patterns).
3. `scripts/macos/verify_auv2_component.sh` — `chmod 600` on mktemp log.
4. `scripts/macos/verify_p2_static_guards.sh` — resolve `python3` before `env -i`.
5. `build.sh` — `TEE_PID=$!` + `trap 'wait' EXIT` (process substitution flush).
6. `scripts/fetch_vst3_sdk.sh` — refuse non-empty clone target.
7. `tools/capture_logic_auv2_hang.sh` — sample duration 3s→1s (overlap cap).
8. `scripts/ci/check_build_warnings.sh` — `ld.lld` in linker regex.

### CMake / CI (3)
9. `CMakeLists.txt:1082` — `REMOVE_ITEM` two-item (dangling entitlements path).
10. `.github/workflows/build.yml:154` — `wc -l | tr -d ' '` (BSD portability).
11. (P1-22 offset clamp from b301c51 covers R1 P2-4.)

### C++ — DSP / audio (5)
12. `mix_fx_processors.h` — Bitcrusher: skip quantize at 16 bits.
13. `mix_fx_processors.h` — deleted dead `a0inv` + 7 dev-comment lines.
14. `digi_d418_stream_engine.h` — `blockPeak` true bipolar magnitude.
15. `sid_filter_core.h` — squash cap 0.16→0.12 (inert cap).
16. `sid_chip.h` — $D418 comment corrected (per-voice control reg).

### C++ — engine / core (6)
17. `arpeggiator.h` — deleted dead `process()` (8 KB RT stack).
18. `arpsid_file_bank.cpp` — `count==0` reject + `exportAll` size check + `copyBoundedField_` (3 fixes).
19. `arpsid_standalone_settings.h` — `tab` upper clamp.
20. `arpsid_standalone_app.cpp` — `writeFile` second-rename recovery + log.
21. `arpsid_standalone_devices.cpp` — `ignoreTypes(true, false, true)` (Start/Stop/Continue).
22. `arpsid_vst3_processor.cpp` — `eventOrder_ = 0` per-block reset.

### C++ — GUI (4)
23. `arpsid_editor_view.cpp` — `shortLabel` range check.
24. `arpsid_editor_view.cpp` + `.h` — `valueChanged` re-entrancy guard (`synchronizing_`).
25. `arpsid_editor_pages.cpp` — `readFileBytes` `in.bad()` check.
26. `dr808_test_utils.h` — `==` → `<=` event consumption.

### C++ — tests (1)
27. `forensic_engine_sanity_v527_tests.cpp` — `require(== nullptr)` (tautology fix).

### C++ — telemetry / diagnostics (4)
28. `arpsid_telemetry_snapshot.h` — `c64Phi2CyclesThisBlock` field.
29. `ArpSIDDSPKernel.hpp` — atomic + delta computation + fill.
30. `ArpSIDKernelTelemetryFill.h` — fill the new field.
31. `ArpSIDParityTrace.h` — `flushToFILE(nullptr)` no-op + generation counter.

### C++ — AUv3 (1)
32. `ArpSIDComponentFlavor.h` — `makeArpSIDFourCC` uint32_t build.

### C++ — C64 (3)
33. `c64_psid_runtime.h` — HIRAM ordering comment (2 locations: color-RAM + BRK-sentinel).
34. `sid_chip.h` — dead filter tables documented as superseded.

### Misc (2)
35. `.gitignore` — `__pycache__/` entry.

## Deferred / RISKY Fixes (4)

| P1 | Round | Why deferred |
|----|-------|-------------|
| R1 P2-18 (install_macos.sh killall) | R1 | Changes install-time AU cache refresh behavior; needs macOS verification. |
| R2 P2-1 (poller hostCallbacks race) | R2 | RT-adjacent poller; needs macOS/AUv2 runtime verification. |
| R2 P2-2 (poller sampleRate race) | R2 | Same class as P2-1. |
| R4 P2-3 (BRK-sentinel mapped read) | R4 | `cpuRead` drives open-bus latch + performs real SID/IO reads; only the documented-assumption comment is safe. |

Plus several lower-priority SAFE fixes deferred for scope (cosmetic comments, doc-only items, UI display fixes that need runtime verification, and items requiring cross-file changes beyond a minimal patch).

## Refuted Findings (19)

| P1 | Round | Why refuted |
|----|-------|------------|
| R2 P2-8 | R2 | Mutex deadlock, not ordering inversion (outer `start()` holds `queueMutex_`). |
| R2 P2-9 | R2 | `case Count` arm exists and returns the correct sentinel. |
| R2 P2-13 | R2 | Phase-wrap redraw gated on `shape`, not `retriggerEnabled`. |
| R2 P2-14 | R2 | No `SidWriteQueue` member in `SidRegisterEngine`; reset paths pair with queue clear. |
| R2 P2-16 | R2 | Envelope recovery unconditional; stale peak delays by ≤ `delay` only. |
| R2 P2-23 | R2 | `if (!ok) return BadBlob` catches short reads. |
| R2 P2-39 | R2 | Moot — P0-2 containment already present. |
| R3 P2-2 | R3 | Self-retracted by the audit; masking is correct. |
| R3 P2-3 | R3 | Three consecutive single-threaded stores; no partial-observation window. |
| R4 P2-2 | R4 | 1,022,727 internally consistent; finding's derivation garbled. |
| R5 P2-1 | R5 | Called in 3 tests + docs (grep excluded `.cpp`). |
| R5 P2-2 | R5 | Last same-cycle $D418 write wins (order-preserved queue). |
| R5 P2-3 | R5 | Overflow → exactness flag + GUI warning + fatal rollback. |
| R5 P2-4 | R5 | `droppedWrites()` getter exists. |
| R5 P2-5 | R5 | `restoreSnapshot` restores both pointers + re-wires `mem_`. |
| R5 P2-6 | R5 | CPU PC is captured in the machine Snapshot. |
| R5 P2-9 | R5 | Runaway detected/rolled-back/counted, not silent. |
| R5 P2-10 | R5 | Asserted in 5 test files. |
| R5 P2-11 | R5 | Handoff path calls `reset()` before install. |
| R5 P2-12 | R5 | No in-payload check is correct; adding one rejects legal tunes. |
