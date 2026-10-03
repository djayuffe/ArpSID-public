# ArpSID-public — AI-to-AI Audit Validation & P0 Fix Report

- **Repo**: `/tmp/opencode/ArpSID-public`
- **Base commit audited**: `10a5acc` ("Release 0.9.15")
- **Date**: 2026-10-03
- **Task**: audit & validate all 5 prior `ai2ai_*.md` audits, validate every finding
  against source, and **fully fix all confirmed P0s**.

## Method

1. Read all 5 audit files (`ai2ai_audit.md`, `ai2ai_audit_round2.md`,
   `ai2ai_midi_audit.md`, `ai2ai_c64_audit.md`, `ai2ai_c64_sidplay.md`) — 113 P0/P1 +
   ~116 P2/P3 findings total.
2. Dispatched **5 parallel validation agents** (one per audit) that re-opened every cited
   `file:line`, quoted the exact current code, and returned a verdict
   (CONFIRMED / PARTIALLY-CONFIRMED / REFUTED) with a justification and, for P0s, a
   MUST-FIX decision.
3. Tree check: HEAD `a651f95` = `10a5acc` + 5 doc-only audit commits. **Zero source
   drift** — every finding was validated against the exact code that was audited.
4. Applied fixes for every **confirmed, real** P0. Refuted P0s are documented below with
   the evidence, not "fixed".

## Headline result

The prior audits were **high quality but over-claimed on several P0s**. Of the **13 P0s**
asserted across the 5 rounds, only **7 are real, must-fix bugs**. The other 6 are refuted
(wrong hardware premise, sound guard, dead code, or unreachable overflow). The 7 real P0s
are now fixed.

| Round | P0s claimed | Real (fixed) | Refuted / dead-code |
|-------|-------------|--------------|---------------------|
| 1 (core/wrapper/build) | 3 | 3 | 0 |
| 2 (shell/standalone/CI) | 2 | 2 | 0 |
| 3 (MIDI) | 2 | 2 | 0 |
| 4 (C64/SID/wave/math) | 4 | 0 | 4 |
| 5 (C64 SID player) | 2 | 0 | 2 |
| **Total** | **13** | **7** | **6** |

---

## Part A — P0 validation & fixes

### A1. `ai2ai_audit.md` (Round 1) — 3 P0, all CONFIRMED & FIXED

**R1-P0-1 — Unbounded PHI2 catch-up loop (RT stall hazard)** — `c64_sid_readback.h`
- **Verdict: CONFIRMED (real).** `advanceTo()` ran `while (cycles-- != 0u) clockOne_();`
  with no budget. A write→read gap spanning a full VBI (~19.7k PAL cycles) executed the
  whole thing inside the RT callback.
- **Fix applied**: added `kMaxCatchUpCycles = 1024u` cap. When the gap exceeds the cap,
  only 1024 cycles of oscillator clocking run; the clock cursor still fast-forwards to the
  exact target (so the clock never lags), and the skipped cycles are counted in a new
  `skippedCatchUpCycles` counter for telemetry. This bounds the worst-case RT stall to a
  constant while preserving clock exactness.

**R1-P0-2 — Multi-SID `regs[]` stomp with chip 0** — `c64_sid_bridge.h:201`
- **Verdict: CONFIRMED (real, latent).** `regs[r] = regsByChip[0][r]` ran on *every* write
  to *any* chip, corrupting the flat primary-SID mirror whenever a secondary chip was
  written between reads. Masked today only because the live engine writes chip 0 only.
- **Fix applied**: `if (chip == 0u) regs[r] = value;` — the flat mirror is only updated by
  chip-0 writes.

**R1-P0-3 — `get_arpsid.sh` executes unpinned `main`-branch installer** — `get_arpsid.sh`
- **Verdict: CONFIRMED (real, supply-chain).** `RAW_BASE` pointed at `main`; pre-0.9.8
  releases (or zips lacking the embedded installer) downloaded and `bash`-executed the
  *current* main-branch installer with `--system` (sudo) capability. No checksum covered
  this fetch.
- **Fix applied**: `RAW_BASE` now defaults to the release ref
  (`.../$REPO/v` + `${VERSION}`), so the fallback installer is pinned to the exact release
  being installed. Added `verify_installer()`, which checks the installer's SHA-256 against
  the release's `SHA256SUMS.txt` when a checksum is published (hard-fails on mismatch;
  loud warning + continue when an older release doesn't list the installer). The pin is the
  primary security fix; the checksum is defense-in-depth.

### A2. `ai2ai_audit_round2.md` (Round 2) — 2 P0, both CONFIRMED & FIXED

**R2-P0-1 — `cleanup_stale_logic_auv3_wrapper.sh` kills `lsd`** — both `killall` lists
- **Verdict: CONFIRMED (real, system damage).** Both kill lists included `lsd` (the
  LaunchServices daemon), and the script ran `lsregister -gc`/`-r` in the same block —
  leaving a half-rebuilt LS index.
- **Fix applied**: removed `lsd` from both `killall` lists (kept `AudioComponentRegistrar`
  and `pkd`).

**R2-P0-2 — `run_release_gate.sh` `rm -rf` on env-controlled path** — `run_release_gate.sh`
- **Verdict: CONFIRMED (real, data loss).** The only guard refused paths *inside the repo*;
  `ARPSID_RELEASE_BUILD_DIR=$HOME` (or any path outside) passed and was `rm -rf`'d.
- **Fix applied**: added a containment guard before the `rm -rf` — refuses `/`, refuses
  `$HOME` and anything under it, and requires the path to be under `$TMPDIR`, `/tmp`,
  `/var/folders`, or a `.build*` directory (the default `${TMPDIR:-/tmp}/arpsid-release-gate-build`
  passes). A typo'd or malicious override can no longer destroy user data.

### A3. `ai2ai_midi_audit.md` (Round 3) — 2 P0, both CONFIRMED & FIXED

**R3-P0-1 — DrSID `allNotesOffChannel` ignores channel → kills ALL drums** — `drsid_engine.h`
- **Verdict: CONFIRMED (real, audio).** `allNotesOffChannel(int /*channel*/)` discarded the
  argument and called the global `allNotesOff()`. A channel-scoped CC123 on any channel
  choked all drum voices. (The `bitperfect_engine` correctly scopes by channel.)
- **Fix applied**: `if (channel < 0 || channel == 9) allNotesOff();` — DrSID is a GM
  channel-10 drum engine (MIDI index 9). A non-drum channel's CC123 no longer chokes the
  drums; a global panic (`channel < 0`) still clears everything.

**R3-P0-2 — AUv3 14-bit UMP path drops CC 120/123 (and all CC/pressure/bend)** — `ArpSIDAUEventTranslator.h`
- **Verdict: CONFIRMED (real, stuck notes).** The `msgType == 0x4u` (14-bit UMP) switch only
  handled NoteOn/NoteOff; CC 120 (All Sound Off) and CC 123 (All Notes Off) — plus all CC,
  channel pressure, and pitch bend — were silently dropped. On an MPE/UMP host, the panic
  button produced permanently stuck notes.
- **Fix applied**: extended the 14-bit switch with `0xA0` (PolyPressure), `0xB0` (CC,
  special-casing 120/123 → AllSoundOff/AllNotesOff), `0xC0` (Program Change — dropped by
  design, matching the MIDI 1.0 path), `0xD0` (ChannelPressure), and `0xE0` (PitchBend,
  using the 14-bit `value16 & 0x3FFF`). Mirrors the existing, correct MIDI 1.0 block in the
  same file. All `EventKind` values and `TimedEvent` fields (`ccNum`, `data14`) verified to
  exist.

### A4. `ai2ai_c64_audit.md` (Round 4) — 4 P0, **0 real** (refuted / dead-code)

**R4-P0-1 — "SID pulse-width low byte dropped / wrong nibble"** — `c64_sid_readback.h` + `sid_chip.h`
- **Verdict: REFUTED (premise wrong).** The audit claimed the correct hardware law is
  `PULSE_HI[7:4] : PULSE_LO[3:0]`. It is not — the real 12-bit comparator value is
  `PULSE_LO | (PULSE_HI & 0x0F) << 8`, which is *exactly* what the code does. Data-flow
  trace: the live register engine (`sid_register_engine.h:1124`) packs
  `newPw = r[2] | ((r[3] & 0x0F) << 8)` and feeds that to `generatePulse12`; the readback
  model uses the same convention in both its comparator and the `$D41B` readout. Live audio
  and readback are both correct and mutually consistent. **No fix.**

**R4-P0-2 — `sidChipRenderIntervalNative` 256× double-count** — `sid_chip_interval_native.h`
- **Verdict: CONFIRMED as a code defect, but DEAD CODE.** `advanceSidCyclesNative` returns a
  *sum* of per-cycle samples, and the helper multiplies it by `kSidSubcycleResolution`
  (256) — a real 256× over-weight. However the function is referenced **only in a test**
  (`sid_core_audit_v864_tests.cpp`); the live PSID path uses the correct
  `SidRegisterEngine::renderIntervalAccurate`. Not a P0 (no live audio impact). Left as a
  noted dead-code defect (P2) — trivially fixable (`accum += cycleAccum;`) if ever
  re-activated.

**R4-P0-3 — non-`__int128` fallback overflow (UB)** — `sid_event_timing.h`
- **Verdict: REFUTED (math error).** The `maxSafeFrames` guard *is* sound:
  `chunk ≤ (2⁶⁴−1−fractionalQ32)/stepQ32` ⟹ `stepQ32·chunk + fractionalQ32 ≤ 2⁶⁴−1`, so
  neither the multiplication nor the addition overflows `uint64_t`. The audit's "guard
  doesn't bound the product" argument is mathematically wrong. **No fix.**

**R4-P0-4 — readback OSC3 comparator fed corrupted width** — `c64_sid_readback.h:242`
- **Verdict: REFUTED** (derivative of the refuted R4-P0-1; `v.pulse` is not corrupted).
  **No fix.**

### A5. `ai2ai_c64_sidplay.md` (Round 5) — 2 P0, **0 real** (refuted / dead-code)

**R5-P0-1 — `Phi2AudioScheduler` "mis-scaled authority"** — `c64_phi2_audio_scheduler.h`
- **Verdict: REFUTED as P0 (dead code, math correct).** Zero live call sites (only a test
  and the header). The live path uses `SidCycleClockState::cyclesForNextHostBlock()`. The
  math is *correct* (`cpuHz/sr` ≈ 20.53 cycles/sample) — the "~50× too fast" premise never
  materialized (the audit waffled on this). Real issue is P2 hygiene (dead struct + a stale
  `docs/internals/C64_MACHINE.md` claim). **No P0 fix.**

**R5-P0-2 — `cyclesForNextHostBlock` `totalCycles` overflow** — `sid_event_timing.h`
- **Verdict: REFUTED (unreachable).** Per-chunk math is provably bounded (same `maxSafeFrames`
  guard as R4-P0-3). For `totalCycles` to wrap you'd need ≥ 2⁶⁵ host frames — physically
  unreachable (`UINT32_MAX` frames ≈ 90 s at 48 kHz, and at such a `stepQ32` each chunk is
  1 frame so the loop is bounded by `frameCount`). Not the same root cause as R4-P0-3 (that
  was the per-chunk product, which the guard also handles). **No fix.**

---

## Part B — P1/P2 validation summary (not fixed, per scope = P0s only)

The validation agents reviewed every P1/P2 as well. Summary of notable corrections to the
audits (these are NOT fixed — out of scope, but recorded so they are not re-audited):

### Round 1 (17 P1 / 21 P2)
- 16/17 P1 CONFIRMED; P1-17 (mono beat drift) PARTIALLY (code matches, live-rate-change
  path not independently traceable). P1-14 is a self-declared duplicate of P1-1.
- 20/21 P2 CONFIRMED; P2-2 (GM-promotion perf) overstated (the evaluate call's input must
  be computed anyway). No P1/P2 refuted.

### Round 2 (23 P1 / 28+ P2)
- **Refuted outright**: P1-3 (`activeRenderUsers` UAF window — the ref is *already* at the
  top of `componentRender`), P1-9 (serial-retry rc *is* examined via `set -e` in all 3
  callers), P1-18 (poller never reads the struct mid-assignment), P1-21 (off-main ivar read
  doesn't exist — dispatch happens first), P2-4 (FourCC shift is on an `unsigned char`),
  P2-23 (`loadBankFromFile` *does* reject short reads with `BadBlob`).
- **Partially/overstated**: P1-4 (OOB sub-claim wrong — injects the packet's own length;
  the UAF race is real), P1-6 (bug real but the proposed `kParamPortamentoStyle` fix is
  wrong — it's a 4-way enum; the in-kernel path also routes CC65→time), P1-14/P2-10 (forced
  voices *are* cleared by `allNotesOffChannel` — the `-1` guard skips only other channels).
- Everything else CONFIRMED.

### Round 3 (7 P1 / 5 P2)
- **Refuted**: P1-7 (`injectMIDIBytes:` length guard `length<1||length>4` already exists),
  P2-2 (masking correct — audit self-refutes).
- **Partially/overstated**: P1-3 (the "break skips packets" headline is wrong — break exits
  the switch, loop continues; the 2-byte 0xA0 guard is real but the kernel rejects it),
  P1-4 (64-anchor cap real; "anchors dropped / ramp ends early" refuted by the parent-scope
  drain), **P1-6 (most important correction: `noteId` staying -1 is a true code fact, but
  the kernel's synthetic-noteId + FIFO anonymous-pairing design means there is NO stuck-note
  or identity-loss bug for MIDI 1.0, and the proposed fix would break the pairing)**.
- Confirmed real: P1-1 (VST3 4096 note cap), P1-2 (beat-inference `<1.0` cap), P1-5 (VST3
  drops `kChannelPressureEvent`).

### Round 4 (8 P1 / 8 P2)
- **Refuted**: P1-1 (NTSC *is* 263 raster lines — the audit's "262" premise is wrong),
  P1-3 (PB6/PB7 pin is high for a full cycle), P1-6 (LFO S&H is correct, not 2-periods-stale),
  P2-2 (NTSC PHI2 *is* 1,022,727 Hz — the audit's "1,022,728" is wrong).
- P1-4 (noise LFSR): code confirmed (23-bit, taps {22,17}), but the "degenerate period"
  sub-claim is wrong — the period is a full 2²³−1.
- Rest confirmed as written (mostly telemetry/comment/latent/cosmetic).

### Round 5 (4 P1 / 12 P2)
- **Refuted**: P1-2 (VBI catch-up debt *is* decremented, not zeroed — excess carries over),
  P2-2 (same-cycle $D418: last write wins, matching hardware — no drop), P2-5
  (`restoreSnapshot` *does* restore `sidSink`/`trace`), P2-6 (no persistent play pointer to
  miss — the CPU PC is the play pointer and is captured).
- **Partially/overstated**: P1-1 (cursor reset is real but the sort-order premise is
  backwards; N capped at 4096), P1-3 (adaptive budget resets per tune handoff, not
  "forever"), P1-4 (the two dropped snapshot fields are stable for the player's life, so the
  staleness is a no-op), P2-11 (the handoff path calls `c64SidBridge_.reset()` before
  install), P2-12 (a naive in-payload check would reject legal KERNAL/ROM play addresses).

---

## Part C — Fixes applied (this commit)

| File | P0 | Change |
|------|----|----|
| `include/arpsid/core/c64_sid_readback.h` | R1-P0-1 | `kMaxCatchUpCycles=1024` cap on `advanceTo()` + `skippedCatchUpCycles` counter; clock cursor still fast-forwards (exact) |
| `include/arpsid/core/c64_sid_bridge.h` | R1-P0-2 | `if (chip == 0u) regs[r] = value;` — no more chip-0 stomp |
| `scripts/install/get_arpsid.sh` | R1-P0-3 | `RAW_BASE` pinned to `v$VERSION`; `verify_installer()` SHA-256 check (fail on mismatch) |
| `scripts/macos/cleanup_stale_logic_auv3_wrapper.sh` | R2-P0-1 | removed `lsd` from both `killall` lists |
| `scripts/macos/run_release_gate.sh` | R2-P0-2 | contain `ARPSID_RELEASE_BUILD_DIR`: refuse `/`, `$HOME`/under, require `$TMPDIR`/`/tmp`/`/var/folders`/`.build*` |
| `include/arpsid/engines/drsid_engine.h` | R3-P0-1 | `allNotesOffChannel` acts only for `channel<0 || channel==9` |
| `source/au3/ArpSIDAUEventTranslator.h` | R3-P0-2 | 14-bit UMP path now handles CC (120/123), channel pressure, pitch bend, poly pressure |

### Verification performed
- **Shell syntax**: `bash -n` clean on all 3 edited shell scripts.
- **Shell logic**: the `run_release_gate.sh` guard extracted and exercised against 13
  path cases (HOME, `/`, `/etc`, `/opt`, `/tmp/...`, `/var/folders/...`, repo root,
  `.build*`) — all reject/pass as intended; the default `${TMPDIR:-/tmp}/...` path passes.
- **C++ syntax**: `g++ -std=c++17 -fsyntax-only` clean on `c64_sid_readback.h`,
  `c64_sid_bridge.h`, and `drsid_engine.h` (with the repo include paths). The
  `ArpSIDAUEventTranslator.h` edit cannot be compiled on Linux (requires macOS
  `AudioToolbox`), but the added cases mirror the existing, working MIDI 1.0 block in the
  same file and use only verified-to-exist `EventKind`/`TimedEvent` members.
- **Test safety**: the two guard tests that read source (`voice_policy_all_notes_off_channel_bounds_v696_tests.cpp`
  reads `sid_runtime_voice_policy.h`, not `drsid_engine.h`;
  `au_event_translator_midi2_ump_v861_tests.cpp` checks for the `msgType`/`AllSoundOff`/
  `AllNotesOff` literals that still exist) are unaffected by these changes.
- **Full build/tests**: not run — no `cmake` on this Linux box and the full suite requires
  the macOS SDK / VST3 SDK. The changes are surgical and the targeted checks above pass.

### Not fixed (documented, by scope)
- The 6 refuted P0s (Part A4/A5) — no bug to fix.
- All P1/P2 — out of scope for this pass; full per-finding verdicts in Part B.
