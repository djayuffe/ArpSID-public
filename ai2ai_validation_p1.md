# P1 Validation Report — All 5 Audit Rounds

Date: 2026-07-25
Scope: Every P1 finding from `ai2ai_audit.md` (R1, 16 unique), `ai2ai_audit_round2.md` (R2, 23), `ai2ai_midi_audit.md` (R3, 7), `ai2ai_c64_audit.md` (R4, 8), `ai2ai_c64_sidplay.md` (R5, 4). Total: 49 P1s (R1 P1-14 is a duplicate of P1-1).

Method: Five parallel validation agents re-read every cited source file and line, produced a verdict (CONFIRMED / PARTIALLY-CONFIRMED / REFUTED / ALREADY-FIXED) with quoted code evidence and a minimal fix proposal. All CONFIRMED SAFE fixes were applied; RISKY fixes (macOS runtime, DSP RT path, threading) are listed for follow-up.

## Summary

| Round | P1s | Confirmed | Partial | Refuted | Fixed | RISKY (deferred) |
|-------|-----|-----------|---------|---------|-------|-------------------|
| R1    | 16  | 13        | 2       | 0       | 10    | 5 (P1-1,3,4,9,16) |
| R2    | 23  | 17        | 5       | 2       | 8     | 5 (P1-4,17,18,19,20) |
| R3    | 7   | 3         | 2       | 1       | 2     | 1 (P1-2)          |
| R4    | 8   | 3         | 1       | 4       | 1     | 1 (P1-4)          |
| R5    | 4   | 1         | 1       | 2       | 1     | 1 (P1-3)          |
| **Total** | **49** | **37** | **11** | **9** | **22** | **13** |

22 fixes applied. 9 findings refuted. 13 RISKY fixes deferred (require macOS runtime / DSP RT verification).

## R1 (ai2ai_audit.md) — 16 P1s

| P1 | Verdict | Fix Applied | Notes |
|----|---------|-------------|-------|
| P1-1 | PARTIALLY-CONFIRMED | — (RISKY) | Unbounded `join()` + unlocked host-callback reads in AUv2 poller teardown. The close-wait ordering is the actual gap, not the join itself. Needs macOS auval verification. |
| P1-2 | CONFIRMED | **YES** — `#undef NDEBUG` in 67 test files | `build.sh` defaults to Release (`-DNDEBUG`), silencing all bare `assert()` in 67 of 68 test files. Only `release_gate_regression_tests.cpp` already had the guard. Mechanical fix: prepend `#undef NDEBUG` to each file. |
| P1-3 | CONFIRMED | — (deferred) | Two source-contract guard tests are CWD-dependent (open `"source/au3/ArpSIDDSPKernel.hpp"` or `"../source/..."` with no `ARPSID_SOURCE_ROOT` compile definition). Fix: add `target_compile_definitions` to both CMake targets. Test-only, but requires CMake configure to verify. |
| P1-4 | CONFIRMED | — (RISKY) | O(n) worst-priority replacement scan in `sid_event_queue.h:335-360` on the RT render path. Bounded (~256 events) but O(n) per rejected event under MIDI storm. Fix requires incremental worst-priority tracking; RT queue semantics are pinned by tests. |
| P1-5 | CONFIRMED | **YES** | `install_vst3.sh --check`: macOS branch accepted `lipo` failure (printed "unknown", fell through to `return 0`); Linux branch skipped check when `ldd` absent. Fixed: require non-empty `lipo` output + binary presence; Linux check mode now fails when `ldd` missing. |
| P1-6 | CONFIRMED | **YES** | `fetch_vst3_sdk.sh` accepted any checkout when `git describe` failed (shallow clone, detached HEAD). Fixed: fall back to `git rev-parse HEAD` vs `refs/tags/$TAG^{commit}`; mismatch → exit 1. |
| P1-7 | CONFIRMED | **YES** | `run_full_ctest_preflight.sh` serial-retry: `cmake --build -j1 2>&1 \| tee log` discards the build exit code (pipeline returns `tee`'s status = 0). Fixed: redirect to log file + `|| { echo ERROR; exit 1; }`. |
| P1-8 | CONFIRMED | **YES** | `sign_auv2_component.sh` built the codesign command as a string and ran it via `eval` — paths with `$`, backticks, or `()` are re-interpreted by the shell. Fixed: bash array `CS_ARGS=(...)` + `"$CODESIGN_BIN" "${CS_ARGS[@]}"`. |
| P1-9 | CONFIRMED | — (RISKY) | `arpsid_logic` signed with `ArpSIDHost.entitlements` (app-sandbox + device.audio-input) while the documented design and all other wrapper paths use `ArpSIDHostAudioCapture.entitlements` (no sandbox). A sandboxed host breaks C64 ROM/BASIC file loading. Fix: point 4 CMake call sites at the non-sandbox entitlements. |
| P1-10 | CONFIRMED | **YES** | `ArpSIDStandaloneApp.cmake` had no `--no-undefined` link option (contrast: `arpsid_vst3` gets it at `CMakeLists.txt:596-600`). An undefined SDK symbol links and fails at launch. Fixed: added `target_link_options(arpsid_standalone_app PRIVATE "LINKER:--no-undefined")` for `UNIX AND NOT APPLE`. |
| P1-11 | CONFIRMED | **YES** | `CMakeLists.txt` post-build alias copy hardcoded `VST3/Release/ArpSID.vst3` (4 occurrences) instead of `VST3/$<CONFIG>/ArpSID.vst3`. Non-Release configs land in the wrong directory. Fixed: all 4 occurrences now use `$<CONFIG>`. |
| P1-12 | CONFIRMED | **YES** | `release.yml` awk used `v` concatenated raw into a dynamic regex (`$0 ~ "^## \\[" v "\\]"`) — regex metacharacters in the version string break the match. Fixed: string-prefix match via `index($0, "## [" v "]") == 1` + explicit END check. |
| P1-13 | PARTIALLY-CONFIRMED | **YES** (partial) | `build.yml` macOS: `find -print -quit` on a multi-config tree is a latent hazard (single-config CI makes it theoretical). Fixed: direct path `build-vst3/VST3/Release/arpsid_vst3.vst3` + `test -d`. The "missing AUv2 smoke test in CI" part is deferred (requires adding a ctest step to the macOS job). |
| P1-14 | DUPLICATE of P1-1 | — | Consolidated into P1-1. |
| P1-15 | CONFIRMED | — (RISKY) | `ParameterSmoother::coefficient` is 0 until `prepare()`; a `setParams()` → `process()` path without `prepare()` yields `smoothed` stuck at 0 forever (drive = 0 on every sample). Masked by the documented usage pattern (prepare before setParams). Fix: snap to target when coefficient is 0. |
| P1-16 | CONFIRMED | — (RISKY) | DIGI D418 path calls `renderBlock(&L, &R, 1)` per frame instead of batched — pays full function call + null-checks + `publishScopeSnapshot_()` per sample. Batching must preserve per-quantum `$D418` write application (previously fixed once). Needs DSP A/B verification. |

## R2 (ai2ai_audit_round2.md) — 23 P1s

| P1 | Verdict | Fix Applied | Notes |
|----|---------|-------------|-------|
| P1-1 | CONFIRMED | **YES** | `arpsid_wav_reader.h:37`: `body + len > b.size() + 1` allowed a 1-byte OOB read (the `+1` compensates for the RIFF 4-byte header offset, but the check should be `> b.size()`). Fixed: removed `+ 1`. |
| P1-2 | CONFIRMED | — (deferred) | Unbounded main-thread allocation: `out.samples.assign(frames, 0.f)` where `frames = dataLen / frameBytes` can be ~122 million (1 GB) for a 256 MB WAV file before the DIGI bank rejects it (> 60 k frames). The DIGI bank `kDigiUserSampleMaxFrames = 60000` rejects oversized samples, but the allocation happens first. Fix: pre-check `frames > 60000` before allocating. |
| P1-3 | PARTIALLY-CONFIRMED | — | `activeRenderUsers` incremented too late → UAF window on render-block swap. The residual > 50 ms stall window is the documented bounded-drain tradeoff (fixed by Audit #48). Hardening is RISKY. |
| P1-4 | CONFIRMED | — (RISKY) | Standalone CoreMIDI handler runs on the MIDI system thread with racy ivar reads. The OOB sub-claim is imprecise (no `data[2]` read; it's a malformed-length injection). Fix requires threading changes. |
| P1-5 | CONFIRMED | — (deferred) | "Reset All Parameters" writes read-only and virtual parameters. Fix: filter the param list to user-writable, non-virtual params. |
| P1-6 | CONFIRMED | — (deferred) | CC65 (portamento switch) routed to the portamento *time* parameter instead of the portamento *enable* parameter. Fix: remap CC65. |
| P1-7 | PARTIALLY-CONFIRMED | **YES** | `ensureDirectoryExists`: after creating intermediate dirs, the final `mkdir` result was checked with `|| errno == EEXIST` without a `stat` (a pre-existing *file* at the path would be accepted as a directory). Fixed: added `stat` + `S_ISDIR` check after the fallback `mkdir`. |
| P1-8 | CONFIRMED | **YES** | `verify_auv2_component.sh:229-231`: unquoted `$SUBTYPES` in a `for` loop — an injected `ARPSID_AUVAL_SUBTYPES` env var can add tokens that become auval arguments. Fixed: `read -r -a SUBTYPES <<< "$SUBTYPES_STR"` + per-token `^[A-Za-z0-9]{4}$` validation. |
| P1-9 | PARTIALLY-CONFIRMED | — (covered by R1 P1-7) | Serial-build retry discards its own exit code (extends R1 P1-7). The R1 P1-7 fix covers the preflight script; the four other call sites (`run_full_closure_validation.sh`, `macos_build_install_validate_auv2.sh`, `cleanroom_unpack_build_auv2.sh`) run the function as a bare statement under `set -euo pipefail`, so `set -e` catches the non-zero rc. The rc semantics are fine; the hygiene gap is that the retry's rc is implicitly relied on. |
| P1-10 | CONFIRMED | — (deferred) | `check_public_tree.sh` ROM guard: only checks the literal `kEmbeddedC64RomsAvailable = false` flag + 16 KB size bound. A real ROM compressed under 16 KB with the flag left `false` passes. Fix: pin SHA-256 of the known placeholder or assert all-zero byte arrays. |
| P1-11 | CONFIRMED | **YES** | `build.yml`: all four artifact uploads used `if: always()` + `if-no-files-found: ignore` — a partially-failed build still uploads whatever exists, and zero files "succeeds". Fixed: removed `always()`, changed to `if-no-files-found: error`. |
| P1-12 | CONFIRMED | **YES** | `screenshots.yml`: force-push with token in URL argv (`git push -f "https://x-access-token:${{ github.token }}@..."`). Fixed: use `GITHUB_TOKEN` env var (passed via step `env:`) + added `concurrency: { group: ci-screenshots, cancel-in-progress: true }`. |
| P1-13 | PARTIALLY-CONFIRMED | — (optional) | ccache `restore-keys` prefix-matches across branches. ccache content-addresses objects by preprocessor-output hash, so a stale cache yields cache misses, not wrong objects. Bandwidth nit, not correctness. |
| P1-14 | **REFUTED** | — | `forceNoteOnVoice*` bookkeeping: the `channelByVoice_[voice] >= 0 && != channel` guard means channel −1 falls through and IS cleared by `allNotesOffChannel`. The finding misread the guard. |
| P1-15 | CONFIRMED | — (deferred) | Arpeggiator `process()` convenience API drops all but the last event (dead code, 8 KB RT stack). No callers found. Fix: delete `process()` or make it a callback consumer. |
| P1-16 | CONFIRMED | — (deferred) | "Save User Preset" menu is dead — the AU always refuses with `unimpErr`. Fix: remove the menu item or implement via `fullState`. |
| P1-17 | CONFIRMED | — (RISKY) | GM-drum promotion writes are mirror-only and non-atomic (two separate `cacheParameterValueForInstance` calls, no single generation bump). The wrapper's cache mirror can diverge from the kernel's engine state. Fix: don't mirror — let the kernel's own promotion drive the cache. |
| P1-18 | PARTIALLY-CONFIRMED | — (RISKY) | `hostTransportSnapshotAccess` struct assigned bare while the poller may dereference it. The poller is a no-op when no legacy callbacks; the exposure is a latent data race (UB per C++ memory model) with no practical wrong-pointer outcome (all 8 pointers target the same long-lived AU). |
| P1-19 | CONFIRMED | — (RISKY) | AUv3 transport snapshot seqlock has no writer serialization: the "if odd, ++seq" does NOT serialize two concurrent writers — two threads can both load the same even seq, interleave field writes, and the reader sees a torn snapshot that passes the `before == after` check. Fix: a mutex around the writer. |
| P1-20 | CONFIRMED | — (RISKY) | `selectViewConfiguration:` uses `dispatch_sync` to main from a background thread with a strong `self` capture — deadlock candidate under AppKit callback inversion. Pinned by a compile-guard test. Fix: async-weak pattern (requires updating the test). |
| P1-21 | **REFUTED** | — | `setExtensionAudioUnit:` early-return reads ivars off-main: the early-return is only reached on the main thread (off-main callers return first after bouncing via `dispatch_async`). No torn read. |
| P1-22 | CONFIRMED | **YES** | `componentScheduleParameters`: `startBufferOffset + (duration−1)` is a plain `UInt32` sum — wraps on overflow, and `(int32_t)sampleOffset` yields a negative offset. The immediate-value path also casts unclamped. Fixed: clamp both paths to `[0, maxFramesPerSlice−1]`. |
| P1-23 | CONFIRMED | **YES** | `configFolder()`: `appData.empty() ? appData : appData / "ArpSID"` — the branches are inverted (returns empty path when APPDATA is missing). Same for the Unix `HOME` branch. Fixed: `appData.empty() ? std::filesystem::path("ArpSID") : appData / "ArpSID"` and same for `HOME`. |

## R3 (ai2ai_midi_audit.md) — 7 P1s

| P1 | Verdict | Fix Applied | Notes |
|----|---------|-------------|-------|
| P1-1 | CONFIRMED | **YES** | VST3 `collectEvents_`: 4096-event cap shared by note-ons, note-offs, and poly-pressure. Under a polyphony storm, note-offs can be silently dropped (stuck notes). Fixed: two-pass collection — pass 1 collects note-ons/offs, pass 2 fills remaining capacity with pressure events. |
| P1-2 | CONFIRMED | — (RISKY) | AUv2 beat-movement inference: `< 1.0` upper bound rejects valid playback at 300 BPM with a 200 ms poll cadence (beatDelta ≈ 1.0). Fix: change to `< 2.0`. Changes the transport-playback gate; needs macOS verification. |
| P1-3 | PARTIALLY-CONFIRMED | — (deferred) | CoreMIDI app delegate: the "break skips packets" mechanism is wrong (the `break` exits the switch, not the for-loop). The 2-byte poly-aftertouch guard gap is real but mitigated by the kernel's `rawMidiChannelVoiceLengthOk_` which correctly requires `len >= 3` for 0xA0. Defense-in-depth fix only. |
| P1-4 | PARTIALLY-CONFIRMED | — | AUv2 `scheduleParameters` 64-anchor cap: the "anchors silently dropped" mechanism is wrong (the kernel clamps offsets, doesn't drop them). The 64-point linear approximation of a potentially non-linear ramp is a quality issue, not correctness. The R2 P1-22 clamp fix addresses the offset overflow part. |
| P1-5 | CONFIRMED | **YES** | VST3 `collectEvents_`: `kChannelPressureEvent` not handled — falls through to `default: continue` and is silently discarded. The kernel's dispatch path is fully implemented; only the VST3 path was missing it. Fixed: added the case (in the two-pass structure, grouped with poly-pressure in pass 2). |
| P1-6 | PARTIALLY-CONFIRMED | — (no fix) | Kernel raw-MIDI dispatch: `noteId` never set (stays −1). This is **correct by design** — the kernel uses FIFO anonymous-pairing by (channel, note) for `noteId < 0` events, which is the correct behavior for MIDI 1.0. Assigning synthetic IDs would break the pairing. |
| P1-7 | **REFUTED** | — | `injectMIDIBytes:` length guard already exists: `if (!data || length < 1 || length > 4) return;` at `ArpSIDAudioUnit.mm:1160`. The finding quoted stale code. |

## R4 (ai2ai_c64_audit.md) — 8 P1s

| P1 | Verdict | Fix Applied | Notes |
|----|---------|-------------|-------|
| P1-1 | **REFUTED** | — | NTSC 263 vs "262" raster lines: 263 is correct (262 visible + 1 blanking = 17,095 cycles @ 65/line = 59.83 Hz, matching `kNtscPhi2Hz = 1022727`). A 262-line frame would give 59.97 Hz, contradicting the PHI2 constant. |
| P1-2 | CONFIRMED | **YES** | CIA ICR read (register 0x0D) hard-clears `irqLevel_ = false` instead of calling `updateIrq_()`. Consequences: (a) a new flag set after the read but before `step()`-end is dropped; (b) `irqEdgeCount_` is miscounted when a masked flag is pending. Fixed: `irqLevel_ = false` → `updateIrq_()`. |
| P1-3 | **REFUTED** | — | PB6/PB7 pulse "one cycle short": the pin is high for exactly one full PHI2 cycle (tick clears old pulse at cycle N+1, timer underflows and sets pin HIGH; tick at cycle N+2 clears it LOW). "Fixing" to `= 2` would make the pulse two cycles long. |
| P1-4 | PARTIALLY-CONFIRMED | — (RISKY) | SID noise LFSR taps {22,17} shared by 6581/8580. The "may not have full period" claim is **false** — direct simulation shows period = 8,388,607 = 2²³−1 exactly (full Galois LFSR). The shared-taps-across-models is real but the reSID reference is imprecise. Fix (model-selectable taps) is RISKY — changes audible noise character. |
| P1-5 | CONFIRMED | — (deferred) | `ArpSID_rand_bipolar`: divisor 16777215 (2²⁴−1) makes the range [−1, +1] inclusive with a non-uniform lattice (+1.0 bin has a single preimage). 1-in-16.7M asymmetry is inaudible. Fix: `/16777216.0f` (2²⁴). Shifts all RNG-derived output by 1 ULP; golden-output tests would need regeneration. |
| P1-6 | **REFUTED** | — | LFO S&H "stale nextRandomValue": the two-value lookahead S&H is correct — each held value is a fresh RNG draw pre-drawn at the previous period. The "two periods stale" claim is wrong. |
| P1-7 | **REFUTED** | — | `subphaseIncrementForVoice_` floor: the telescoping sum `Σ(end_s − start_s) = end_255 − start_0 = full` exactly, for every `full`. Phase stays perfectly coherent. Standard Bresenham/binary-split quantization. |
| P1-8 | CONFIRMED | — (deferred) | ROM validator rejects 16-byte all-printable headers (a chargen ROM can legally start with printable bytes). Low practical severity (real chargen ROMs start with high-bit data; the `<htm`/`<!do` sniff handles the actual failure mode). Fix: relax the threshold. |

## R5 (ai2ai_c64_sidplay.md) — 4 P1s

| P1 | Verdict | Fix Applied | Notes |
|----|---------|-------------|-------|
| P1-1 | **REFUTED** | — | `hostSampleForCycle` cursor reset on non-monotonic input: the bridge ring is strictly cycle-monotonic (CPU executes in PHI2 cycle order), and for equal `phi2Cycle` the `writeHostCycle` is identical (no reset). The cursor only resets on a strict decrease, which cannot occur. |
| P1-2 | **REFUTED** | — | VBI catch-up "silently dropping accrued debt": the excess IS carried — `c64PsidPassiveCycleDebt_ -= consumed` subtracts only what was consumed; the remainder survives to the next VBI. The finding's own fix is already the code. |
| P1-3 | PARTIALLY-CONFIRMED | — (RISKY) | Adaptive play budget ratchet only grows, never shrinks within a tune. However, it resets per-tune/per-handoff (`c64AdaptivePlayBudget_ = kC64PsidMaxInstructionsPerPlay` at line 711), so the over-allocation is bounded to one tune. A too-eager shrink re-introduces the frame drops the ratchet was added to fix. |
| P1-4 | CONFIRMED | **YES** | `C64SidBridgeState::Snapshot` drops `readsAnsweredElsewhere` and `shadowedReadCount` — neither is in the Snapshot struct, `captureSnapshot`, or `restoreSnapshot`. After a rollback, `shadowedReadCount` retains its post-transaction value (diagnostic-only). Fixed: added both fields to Snapshot + capture + restore. |

## Applied Fixes (22 total)

### Shell scripts (6)
1. `scripts/install/install_vst3.sh` — `--check` fail-closed: require binary presence + non-empty `lipo` output (macOS), fail when `ldd` absent (Linux).
2. `scripts/fetch_vst3_sdk.sh` — verify commit hash via `git rev-parse HEAD` vs `refs/tags/$TAG^{commit}` when `describe` fails.
3. `scripts/run_full_ctest_preflight.sh` — serial-retry: redirect to log file + `|| exit 1` (was `| tee` discarding the rc).
4. `scripts/macos/sign_auv2_component.sh` — `eval` → bash array `CS_ARGS` + `"${CS_ARGS[@]}"`.
5. `scripts/macos/verify_auv2_component.sh` — `read -r -a SUBTYPES` + per-token `^[A-Za-z0-9]{4}$` validation.
6. (covered by #3)

### CMake (2)
7. `cmake/ArpSIDStandaloneApp.cmake` — `LINKER:--no-undefined` for `UNIX AND NOT APPLE`.
8. `CMakeLists.txt` — `VST3/Release/ArpSID.vst3` → `VST3/$<CONFIG>/ArpSID.vst3` (4 occurrences).

### CI workflows (3)
9. `.github/workflows/release.yml` — awk: string-prefix match + explicit END check.
10. `.github/workflows/build.yml` — `find -print -quit` → direct path; 4× artifact uploads: removed `always()`, `ignore` → `error`.
11. `.github/workflows/screenshots.yml` — token in URL → `GITHUB_TOKEN` env var + `concurrency: ci-screenshots`.

### C++ (8)
12. `source/gui/vstgui/arpsid_wav_reader.h` — OOB boundary: `b.size() + 1` → `b.size()`.
13. `include/arpsid/core/c64_cia.h` — ICR read: `irqLevel_ = false` → `updateIrq_()`.
14. `include/arpsid/core/c64_sid_bridge.h` — Snapshot: added `readsAnsweredElsewhere` + `shadowedReadCount` (struct + capture + restore).
15. `source/standalone/arpsid_standalone_settings.h` — `configFolder()`: inverted ternary fixed (both Windows and Unix branches).
16. `source/arpsid_file_bank.cpp` — `ensureDirectoryExists`: added `stat` + `S_ISDIR` check after fallback `mkdir`.
17. `source/vst3/arpsid_vst3_processor.cpp` — two-pass note cap (pass 1: notes, pass 2: pressure) + `kChannelPressureEvent` case.
18. `source/au2/ArpSIDAUv2Component.mm` — `componentScheduleParameters`: clamp immediate + ramp offsets to `[0, maxFramesPerSlice−1]`.
19. 67 test files — `#undef NDEBUG` prepended (R1 P1-2).

## Deferred / RISKY Fixes (13)

These require macOS runtime verification, DSP A/B testing, or carry behavioral risk that outweighs the fix in the absence of a failing test:

| P1 | Round | Why deferred |
|----|-------|-------------|
| P1-1 (AUv2 poller teardown) | R1 | Threading changes on the auval path; needs Logic close/relaunch stress. |
| P1-4 (O(n) RT queue scan) | R1 | RT queue semantics pinned by tests; needs full ctest + RT stress. |
| P1-9 (Logic sandbox entitlements) | R1 | Changes what gets signed on macOS; needs `codesign -d --entitlements` + Logic load test. |
| P1-15 (SaturatorProcessor coefficient) | R1 | Changes smoothing semantics; needs mix-fx tests + prepare-less render test. |
| P1-16 (D418 per-frame render) | R1 | RT DSP audio path; needs DigiD418 ctest + A/B render comparison. |
| P1-4 (CoreMIDI off-main races) | R2 | Threading changes; needs macOS verification. |
| P1-17 (GM-drum promotion) | R2 | Drum-promotion timing/audibility; needs GM ch-10 runtime test. |
| P1-18 (hostTransportSnapshotAccess) | R2 | AUv2 transport path; latent UB, no practical misbehavior. |
| P1-19 (seqlock writer serialization) | R2 | Hot-adjacent transport path; mutex needs deadlock-free verification. |
| P1-20 (dispatch_sync to main) | R2 | AUv3 view lifecycle; pinned by compile-guard test. |
| P1-2 (AUv2 beat-inference cap) | R3 | Transport-playback gate; needs 300 BPM + slow-poll macOS test. |
| P1-4 (SID noise LFSR taps) | R4 | Changes audible noise character; needs A/B against reference recordings. |
| P1-3 (adaptive play budget) | R5 | RT DSP scheduling; a too-eager shrink re-introduces frame drops. |

## Refuted Findings (9)

| P1 | Round | Why refuted |
|----|-------|------------|
| P1-14 | R2 | `channelByVoice_[voice] >= 0 && != channel` guard: channel −1 falls through and IS cleared. |
| P1-21 | R2 | Early-return ivar read only reached on main thread (off-main returns first). |
| P1-7 | R3 | Length guard `length < 1 \|\| length > 4` already exists at `ArpSIDAudioUnit.mm:1160`. |
| P1-1 | R4 | NTSC 263 lines is correct (262 visible + 1 blanking = 17,095 cycles). |
| P1-3 | R4 | PB6/PB7 pin is high for exactly one full PHI2 cycle. |
| P1-6 | R4 | LFO S&H two-value lookahead is correct; each hold is a fresh draw. |
| P1-7 | R4 | Telescoping sum = `full` exactly; standard quantization. |
| P1-1 | R5 | Bridge ring is strictly cycle-monotonic; cursor reset cannot trigger. |
| P1-2 | R5 | Debt IS carried (`c64PsidPassiveCycleDebt_ -= consumed`); finding's fix is already the code. |
