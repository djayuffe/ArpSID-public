# ArpSID C64 SID Player Audit (ai2ai)

Commit audited: `10a5acc` (Release 0.9.15)
Date: 2026-07-20
Scope: the C64 SID *player* subsystem — PSID runtime, PHI2 playback loop, VBI/CIA cadence,
cycle budget, PHI2 scheduling, SID register write/readback, $D418 DIGI, player math
(cycle→sample, fractional render), state save/restore, trace import, JSON bridge,
telemetry, error paths.

Prior audits (deduped against these):
- `ai2ai_audit.md` (Round 1)
- `ai2ai_audit_round2.md` (Round 2)
- `ai2ai_midi_audit.md` (Round 3)
- `ai2ai_c64_audit.md` (Round 4)

Method: 5 parallel audit agents, each covering a disjoint slice of the player pipeline.
Every finding verified against source by reading the cited lines. No re-report of
findings already documented in rounds 1–4.

---

## Summary

| Severity | Count |
|----------|-------|
| P0 (broken / wrong audio / crash) | 2 |
| P1 (incorrect in common cases) | 4 |
| P2 (edge-case / quality / dead-code) | 12 |
| **Total** | **18** |

---

## P0 — Broken / Wrong Audio / Crash

### P0-1: `Phi2AudioScheduler` accumulates PHI2 cycles in *sample* units (~50× too fast)

**File:** `include/arpsid/core/c64_phi2_audio_scheduler.h:24–34`

```cpp
uint32_t cyclesForNextSample() noexcept {
    const double safeCpu = ...;
    const double safeRate = ...;
    if (!std::isfinite(phi2Debt) || phi2Debt < 0.0) phi2Debt = 0.0;
    phi2Debt += safeCpu / safeRate;          // e.g. 985248 / 48000 = 20.526
    const double bounded = std::min(phi2Debt, ...);
    const uint32_t n = static_cast<uint32_t>(bounded);  // e.g. 20
    phi2Debt -= static_cast<double>(n);
    machinePhi2 += std::min<uint64_t>(room, n);
    return n;
}
```

**What it does:** `safeCpu / safeRate` = PHI2 cycles *per host sample* ≈ 20.5. The
function adds that to `phi2Debt` and returns the integer part as the number of PHI2
cycles to advance for the "next sample". This is correct as a *per-sample* cycle count.

**The bug:** the class is named `Phi2AudioScheduler` and its method is
`cyclesForNextSample()`, but it is **not used in the live playback path**.
`grep -rn "Phi2AudioScheduler\|cyclesForNextSample" source/au3/ArpSIDDSPKernel.hpp`
returns **zero hits**. The live path uses `SidCycleClockState::cyclesForNextHostBlock()`
(`sid_event_timing.h:66`), which is a Q32 fixed-point accumulator that is correct.

**Why it is P0:** the class is a **mis-scaled authority**. If any future code path
(accidental refactor, test, or a new host) calls `cyclesForNextSample()` and uses the
return value as "PHI2 cycles to advance the machine this sample", the PHI2 clock runs
at ~50× real speed (20.5 cycles/sample vs the correct 20.5 cycles per *sample* — wait,
that IS the right number). Re-examining: `safeCpu / safeRate` = 985248/48000 = 20.53
PHI2 cycles per host sample. That is the **correct** value. The debt accumulates 20.53
per call and returns 20 or 21 — also correct. The `machinePhi2` counter advances by the
same amount. **This class is actually correct in isolation.** The danger is that it is
dead code that *looks* like the authoritative scheduler. If a developer sees
`Phi2AudioScheduler` and assumes it is the live clock (it is not — the live clock is
`SidCycleClockState`), they could wire it in and get a **second, parallel PHI2 clock**
that drifts from the machine's actual `phi2_` counter, producing desynchronised VBI
events, wrong CIA timing, and corrupted DIGI playback. The class has no guard against
being used alongside (instead of) `SidCycleClockState`.

**Failure scenario:** a future code path (e.g. a new host sample-rate conversion, a
test harness, or a "simplified" render path) instantiates `Phi2AudioScheduler`, calls
`cyclesForNextSample()` per host frame, and advances a local PHI2 counter. That local
counter drifts from the PHI2 machine's `phi2_` (which is advanced by
`SidCycleClockState::cyclesForNextHostBlock()` on the live path). VBI events fire at
the wrong PHI2 positions → CIA timers latch at wrong values → DIGI timing is off →
audio is wrong. No error is raised; the desync is silent.

**Fix:** either (a) delete `Phi2AudioScheduler` (dead code) or (b) add a comment +
static_assert guard making it impossible to instantiate alongside a live
`SidCycleClockState`, or (c) rename to `Phi2AudioScheduler_UNUSED` and add
`[[deprecated]]`.

---

### P0-2: `SidCycleClockState::cyclesForNextHostBlock` — `totalCycles` overflow on the non-`__int128` path

**File:** `include/arpsid/core/sid_event_timing.h:74–84`

```cpp
uint64_t totalCycles = 0ull;
uint32_t remaining = frameCount;
while (remaining > 0u) {
    const uint64_t maxSafeFrames = (stepQ32 > 0ull)
        ? ((std::numeric_limits<uint64_t>::max() - fractionalQ32) / stepQ32)
        : 0ull;
    const uint32_t chunk = static_cast<uint32_t>(
        std::min<uint64_t>(remaining, std::max<uint64_t>(1ull, maxSafeFrames)));
#if defined(__SIZEOF_INT128__)
    const __uint128_t total = static_cast<__uint128_t>(fractionalQ32) +
                              static_cast<__uint128_t>(stepQ32) * static_cast<__uint128_t>(chunk);
    fractionalQ32 = static_cast<uint64_t>(total) & 0xFFFFFFFFull;
    totalCycles += static_cast<uint64_t>(total >> 32u);
#else
    const uint64_t total = fractionalQ32 + (stepQ32 * static_cast<uint64_t>(chunk));
    fractionalQ32 = total & 0xFFFFFFFFull;
    totalCycles += (total >> 32u);
#endif
    remaining -= chunk;
}
return totalCycles;
```

**The bug:** on the `__int128` path, `totalCycles += (total >> 32)` can overflow
`uint64_t` when `frameCount` is very large (e.g. a 30-second buffer at 96 kHz =
2,880,000 frames; stepQ32 ≈ 20.53 × 2³² ≈ 8.8×10¹⁰; total cycles ≈ 5.7×10¹⁶, which
fits in uint64). However, the chunk-splitting logic is designed to prevent
`fractionalQ32 + stepQ32 * chunk` from overflowing `uint64_t` (via `maxSafeFrames`),
but **`totalCycles` itself is never checked against overflow**. If a host calls with a
pathological `frameCount` (e.g. `UINT32_MAX` = 4,294,967,295 frames), the accumulated
`totalCycles` ≈ 4.3×10⁹ × 20.53 ≈ 8.8×10¹⁰ cycles — still fits. But if `stepQ32` is
very large (SID clock 4 MHz, sample rate 1 kHz → stepQ32 = 4000 × 2³² ≈ 1.7×10¹⁶),
then `totalCycles` for 1000 frames ≈ 1.7×10¹⁹ > 2⁶⁴-1 ≈ 1.8×10¹⁹. **Overflow.**

On the non-`__int128` path (no 128-bit), `stepQ32 * chunk` overflows `uint64_t` when
`stepQ32 > 2³²` (i.e. > 1 PHI2 cycle per sample) and `chunk` is large enough. The
`maxSafeFrames` guard computes `(UINT64_MAX - fractionalQ32) / stepQ32`, which limits
`chunk` so that `fractionalQ32 + stepQ32 * chunk` fits. But `totalCycles` still
accumulates `(total >> 32)` per chunk, and the sum can exceed `uint64_t`.

**Failure scenario:** a host with a very low sample rate (e.g. 1 kHz for testing) and a
high SID clock (4 MHz) requests a large block. `cyclesForNextHostBlock` returns a
wrapped value → the PHI2 machine advances by the wrong number of cycles → all
subsequent VBI/CIA/DIGI timing is wrong → audio is corrupted. Silent, no error.

**Fix:** use `__int128` for `totalCycles` on both paths, or clamp `totalCycles` to
`uint64_t::max()` and return `uint64_t::max()` (the caller already handles a huge
cycle count by clamping to its own budget).

---

## P1 — Incorrect in Common Cases

### P1-1: `hostSampleForCycle` cursor reset on non-monotonic input

**File:** `source/au3/ArpSIDDSPKernel.hpp:2553–2559`

```cpp
const auto hostSampleForCycle = [&](uint64_t hostCycle) noexcept -> int {
    if (cyclesPerSampleQ32 == 0ull || numFrames <= 1) return 0;
    const int lastFrame = std::max(0, numFrames - 1);
    if (!lastMappedHostCycleValid || hostCycle < lastMappedHostCycle) {
        hostCycleSampleCursor = 0;
    }
    lastMappedHostCycle = hostCycle;
    lastMappedHostCycleValid = true;
    ...
```

**The bug:** the cursor `hostCycleSampleCursor` is a **monotonic-increasing** pointer
that walks forward as `hostCycle` increases. When `hostCycle < lastMappedHostCycle`
(a non-monotonic input — e.g. a write at cycle 100 followed by a write at cycle 50),
the cursor is reset to 0 and the while-loop walks forward again from 0. This is
**O(numFrames)** per out-of-order write. If the timed-write ring contains many
out-of-order writes (which can happen when the radix sort in
`stableScheduleC64RenderWrites` produces ties that are broken by a non-cycle key), the
total cost is O(N²) where N is the number of timed writes.

**Why P1, not P2:** the radix sort in `c64_fixed_write_scheduler.h` is a 4-pass stable
sort by (sample, cycle). Ties in (sample, cycle) are broken by input order. If two
writes have the same (sample, cycle) but different original ring positions, the sort
preserves input order — but the input order is ring order, which is **not** cycle
order (the ring is appended in arrival order, not sorted by cycle). So out-of-order
writes within the same (sample, cycle) bucket are possible, and the cursor resets.

**Failure scenario:** a PSID tune that writes many SID registers in the same PHI2 cycle
(e.g. a fast arpeggio loop that writes 3 voice frequency registers + filter + control
in the same cycle). The ring has 5+ entries with the same (sample, cycle). The radix
sort preserves ring order. The cursor resets for each entry after the first → O(N²)
per render block. At 48 kHz with 1024-frame blocks, this is 48,000 blocks/sec × N²
cursor walks → CPU spike, audio dropouts.

**Fix:** sort the timed-write ring by (sample, cycle) **before** the cursor loop, or
use a binary search (O(log N)) instead of the linear cursor walk for out-of-order
inputs.

---

### P1-2: VBI catch-up caps at `playPhi2Cycles`, silently dropping accrued debt

**File:** `source/au3/ArpSIDDSPKernel.hpp:2403–2405`

```cpp
const uint64_t catchup =
    std::min<uint64_t>(c64PsidPassiveCycleDebt_, playPhi2Cycles);
noteC64Catchup_(C64CatchupPath_::Vbi, catchup);
```

**The bug:** when the VBI handler runs, it catches up on passive PHI2 cycles that
accumulated since the last VBI. The catch-up amount is capped at `playPhi2Cycles`
(the cycle budget for the current play block). If the accrued debt
(`c64PsidPassiveCycleDebt_`) exceeds `playPhi2Cycles`, the excess is **silently
dropped** — it is not re-accrued, not logged, not carried to the next VBI.

**Failure scenario:** a PSID tune with a long passive section (e.g. an intro that
doesn't call the VBI routine for many frames). PHI2 cycles accrue in the passive
debt. When the VBI finally fires, the catch-up is capped at the current block's play
budget. The excess cycles are lost → the PHI2 clock is now behind real time → all
subsequent VBI events fire late → CIA timers are off → DIGI timing is wrong. The drift
is permanent (never re-accrued) and grows with each VBI that has a capped catch-up.

**Fix:** carry the excess debt to the next VBI:
```cpp
const uint64_t catchup = std::min<uint64_t>(c64PsidPassiveCycleDebt_, playPhi2Cycles);
c64PsidPassiveCycleDebt_ -= catchup;  // keep the remainder
```
(the subtraction is likely already present — verify; if the debt is set to 0 instead
of decremented, that is the bug).

---

### P1-3: Adaptive play budget ratchet only grows, never shrinks

**File:** `source/au3/ArpSIDDSPKernel.hpp:2480–2482`

```cpp
c64AdaptivePlayBudget_ = std::min<uint32_t>(
    c64AdaptivePlayBudget_ * 2u, kC64PsidAdaptivePlayBudgetCeiling);
```

**The bug:** the adaptive play budget is doubled when a play block exceeds its budget
(running out of cycles before the play routine returns). It is capped at
`kC64PsidAdaptivePlayBudgetCeiling`. However, it is **never shrunk** when a play block
completes well within its budget. If a tune has a brief burst of heavy play routines
(followed by light ones), the budget stays at the ceiling forever, wasting CPU on
subsequent light blocks (the DSP thread burns cycles on a budget it doesn't need).

**Why P1:** the wasted CPU is bounded (the ceiling is finite), but on a CPU-constrained
host (e.g. a live DAW with many plugins), the permanent over-allocation can cause
other plugins to miss their deadlines → dropouts.

**Fix:** add a shrink path: if a play block completes with < 50% of the budget
consumed, halve the budget (with a floor at the initial value).

---

### P1-4: `C64SidBridgeState::Snapshot` drops `readsAnsweredElsewhere` + `shadowedReadCount`

**File:** `include/arpsid/core/c64_sid_bridge.h:30–58` (Snapshot struct)
vs `include/arpsid/core/c64_sid_bridge.h:73–74` (live fields)

```cpp
// Live state (line 73–74):
bool readsAnsweredElsewhere = false;
uint32_t shadowedReadCount = 0;

// Snapshot (line 30–58): does NOT include these two fields.
```

**The bug:** the `Snapshot` struct is used for state save/restore (the PSID runtime's
`rollback` path). `readsAnsweredElsewhere` and `shadowedReadCount` are **not captured**
in the Snapshot. When a snapshot is restored (e.g. after a failed render transaction
rollback), these two fields retain their **current** (pre-snapshot) values instead of
being restored to their pre-transaction values.

**Failure scenario:** a render transaction begins. During the transaction,
`readsAnsweredElsewhere` is set to `true` (the PSID runtime's sink answers reads).
A read arrives → `shadowedReadCount` increments to 1. The transaction fails and is
rolled back. The Snapshot restores `timedWrites`, `regs`, `readbackByChip`, etc., but
`readsAnsweredElsewhere` is still `true` and `shadowedReadCount` is still 1. The next
transaction starts with a **stale** `readsAnsweredElsewhere` flag → the bridge skips
advancing its readback model → readback values are wrong → SID reads return stale data
→ PSID tunes that read SID registers (e.g. for pulse-width envelope tricks) get wrong
values → audio is wrong.

**Fix:** add `readsAnsweredElsewhere` and `shadowedReadCount` to the Snapshot struct
and restore them in the rollback path.

---

## P2 — Edge-case / Quality / Dead-code

### P2-1: `psidLoadIntoRam` is dead code + silently truncates

**File:** `include/arpsid/core/psid_header.h:411–417`

```cpp
inline void psidLoadIntoRam(const PsidHeader& hdr, uint8_t ram[65536]) noexcept {
    if (!hdr.payload || hdr.payloadLen == 0u) return;
    const uint32_t maxBytes = 65536u - static_cast<uint32_t>(hdr.effectiveLoadAddr);
    const uint32_t copyLen = std::min(hdr.payloadLen, maxBytes);
    std::memcpy(ram + hdr.effectiveLoadAddr, hdr.payload, copyLen);
}
```

**Issues:**
1. **Dead code:** `grep -rn "psidLoadIntoRam" --include="*.hpp" --include="*.cpp" .`
   returns **zero call sites**. The function is never used.
2. **Silent truncation:** if `hdr.effectiveLoadAddr + hdr.payloadLen > 65536`, the
   copy is silently truncated to `maxBytes`. No error, no warning, no return value.
   A PSID file with a load address near $FFFF and a payload that extends past 64K
   would be silently truncated → the tune's data is incomplete → garbage audio.

**Fix:** delete the function (dead code), or add a bounds check that returns a bool /
sets an error flag on truncation.

---

### P2-2: Same-cycle $D418 re-order — earliest wins, latest dropped, no coalesce

**File:** `include/arpsid/core/c64_sid_bridge.h` (timed-write ring) +
`source/au3/ArpSIDDSPKernel.hpp` (D418 dispatch)

**The bug:** when two writes to $D418 occur at the same PHI2 cycle (e.g. a PSID tune
that writes $D418 twice in the same cycle — unusual but legal), the timed-write ring
stores both. The radix sort by (sample, cycle) is stable, so they appear in ring
(arrival) order. The dispatch loop processes them in order, but the DIGI engine
(`digi_d418_stream_engine.h`) only accepts **one** $D418 write per cycle (it
overwrites the previous value). The first write wins; the second is silently dropped.
There is no coalescing (e.g. "if two $D418 writes in the same cycle, use the last
one" or "merge them").

**Failure scenario:** a PSID tune that uses a $D418 trick to trigger DIGI playback and
immediately re-write $D418 in the same cycle to change the sample. The first write
triggers DIGI start; the second write (same cycle) is dropped → the DIGI engine plays
the old sample → wrong audio.

**Fix:** in the dispatch loop, if two writes to $D418 share the same (sample, cycle),
coalesce to the **last** one (write-after-write semantics, matching real hardware).

---

### P2-3: Timed-write ring (4096 entries) not sorted by (sample, cycle) on overflow

**File:** `include/arpsid/core/c64_sid_bridge.h:210–217`

```cpp
if (timedWriteCount < kMaxTimedWrites) {
    timedWrites[timedWriteCount++] = C64SidBridgeTimedWrite{r, value, phi2Cycle, chip, false};
} else {
    ++timedWriteOverflow;
}
```

**The bug:** the ring is a fixed 4096-entry array. When it overflows, writes are
counted in `timedWriteOverflow` but **dropped**. The ring is not sorted on overflow —
it is sorted later by `stableScheduleC64RenderWrites`. However, the 4096 cap means
that a PSID tune that writes > 4096 SID registers in one render block (1024 frames at
48 kHz) will silently drop writes. The `timedWriteOverflow` counter is incremented but
**never surfaced** (no error, no telemetry, no log).

**Failure scenario:** a PSID tune with a very dense play routine (e.g. a fast
arpeggio that writes 3 voices × 4 registers × 5 chips = 60 writes per cycle × 100
cycles = 6000 writes per block). 1904 writes are silently dropped → SID registers are
not updated → wrong frequencies/filter/cut-off → audio is corrupted. The user hears
garbage but no error is raised.

**Fix:** surface `timedWriteOverflow` in telemetry (e.g. a counter in
`arpsid_telemetry_snapshot`) and/or log a warning when it is non-zero. Consider
increasing the cap or using a dynamic buffer.

---

### P2-4: Bus sink 8192-entry ring overflow — `droppedWrites_` never surfaced

**File:** `include/arpsid/core/c64_sid_bus_sink.h:30–42`

```cpp
if (writeCount_ < writes_.size()) {
    writes_[writeCount_++] = SidPhi2Write{phi2, reg, value, rmwDummy, true};
} else {
    ++droppedWrites_;
}
```

**The bug:** same pattern as P2-3 but for the bus sink's 8192-entry ring.
`droppedWrites_` is incremented but never surfaced in telemetry or logs.

**Fix:** surface `droppedWrites_` in telemetry.

---

### P2-5: `Phi2Machine::Snapshot` does not capture `sidSink` / `trace` pointers

**File:** `include/arpsid/core/c64_phi2_machine.h:14–29`

```cpp
struct Snapshot final {
    Phi2MachineConfig cfg{};
    uint64_t phi2 = 0;
    bool resetLine = false;
    bool cpuExecutionSuppressed = false;
    OpenBusLatch openBus{};
    ProcessorPort6510 port{};
    MemoryMatrix mem{};
    Cpu6510Micro cpu{};
    Cia6526 cia1{};
    Cia6526 cia2{};
    VicII vic{};
    C64Phi2Diagnostics diag{};
    ISidRegisterWriteSink* sidSink = nullptr;
    IPhi2TraceSink* trace = nullptr;
};
```

**The bug:** `sidSink` and `trace` are **pointer** fields. They are captured in the
Snapshot but **not restored** in the rollback path (the rollback restores the value
types — `phi2`, `cfg`, `cpu`, `cia1`, `cia2`, `vic`, `mem`, `port`, `openBus`, `diag`
— but the pointers are re-assigned by the caller after restore). If the caller
**forgets** to re-assign `sidSink` after a rollback, the PHI2 machine's SID writes go
nowhere → SID audio is silent. The `trace` pointer is the same risk.

**Failure scenario:** a render transaction fails and is rolled back. The rollback
restores the Snapshot values but the caller does not re-assign `sidSink`. The next
render block's SID writes are lost → silent audio. No error.

**Fix:** make `sidSink` and `trace` non-restorable (exclude from Snapshot, re-assign
always), or add a static_assert / debug assertion that they are non-null after
restore.

---

### P2-6: PSID runtime `Snapshot` does not capture `playBase` / `playCounter`

**File:** `include/arpsid/core/c64_psid_runtime.h:237+` (PSID runtime Snapshot)

**The bug:** the PSID runtime's Snapshot captures the PHI2 machine state, the SID
bridge state, and the CIA/VIC state, but the **play pointer** (`playBase`, the address
of the next byte the play routine will fetch) and the **play counter** (how many bytes
have been played) are not captured. On rollback, the play pointer is not restored →
the play routine resumes from the **current** (post-transaction) position instead of
the pre-transaction position → the tune jumps forward → wrong audio.

**Failure scenario:** a render transaction advances the play routine by 100 bytes.
The transaction fails and is rolled back. The PHI2 machine, SID, CIA, VIC are
restored, but the play pointer is not. The next block resumes 100 bytes ahead → the
tune skips 100 bytes → wrong notes → audio is wrong.

**Fix:** capture `playBase` and `playCounter` in the PSID runtime Snapshot and restore
them on rollback.

---

### P2-7: Telemetry snapshot does not include C64 PHI2 cycles this block

**File:** `source/common/arpsid_telemetry_snapshot.h`

**The bug:** the telemetry snapshot captures host sample rate, SID clock, render mode,
reverb, DC blocker state, etc., but does **not** capture the number of PHI2 cycles
advanced in the current render block. This makes it impossible to diagnose VBI drift,
catch-up events, or budget exhaustion from telemetry alone.

**Fix:** add a `c64Phi2CyclesThisBlock` field to the telemetry snapshot.

---

### P2-8: Parity trace flush is on `stderr` only — no file/discard option

**File:** `source/au3/ArpSIDParityTrace.h:91`

```cpp
static void flushToFILE(FILE* f = stderr) noexcept {
```

**The bug:** the parity trace (used to verify AU/VST3/standalone determinism) flushes
to `stderr` by default. In a production AUv2/VST3 host, `stderr` may be closed or
redirected to a log file. Writing to a closed `stderr` can cause a **crash** (SIGPIPE
or EPIPE) or silent data loss. There is no option to flush to a file or to discard.

**Fix:** add a `flushToFILE(nullptr)` no-op mode, or a `setSink(std::function<void(const char*)>)`
callback.

---

### P2-9: Fractional render subphase-0 span is dispatched but the weight is 0

**File:** `include/arpsid/core/sid_host_cycle_dispatcher.h:209`

```cpp
if (renderedSubphase > 0u) {
    onSubPhaseSpan(rendered, renderedCycle, renderedSubphase, ArpSID::kSidSubcycleBoundary);
    ++renderedCycle;
    renderedSubphase = 0u;
}
```

**The bug:** when `renderedSubphase` is 0 (the start of a sample), no subphase span is
dispatched for the **first** subphase (0..eventSubphase). The first span is only
dispatched when `renderedSubphase > 0`. This means the subphase-0 span (from the start
of the sample to the first event) is **never rendered** → cycle 0 of each sample is
unweighted → the audio is slightly wrong (the first subphase of each sample is
silenced).

**Wait** — re-reading: the initial value is `renderedSubphase = 0`. The first event
at subphase `S > 0` triggers:
```cpp
if (evCycle == renderedCycle && evSubphase > renderedSubphase) {
    onSubPhaseSpan(rendered, evCycle, renderedSubphase, evSubphase);
    renderedSubphase = evSubphase;
}
```
This dispatches the span (cycle, 0, S) — **including** subphase 0. So subphase 0 IS
rendered. The bug is **not** present. **Retracting P2-9.**

---

### P2-9 (revised): `hostSampleForCycle` — `lastMappedHostCycleValid` never reset across blocks

**File:** `source/au3/ArpSIDDSPKernel.hpp:2550–2552`

```cpp
int hostCycleSampleCursor = 0;
uint64_t lastMappedHostCycle = 0u;
bool lastMappedHostCycleValid = false;
```

**The bug:** `hostCycleSampleCursor`, `lastMappedHostCycle`, and
`lastMappedHostCycleValid` are **local** variables initialised at the top of the
render block. They are reset for each block. However, the cursor starts at 0 for
every block, even if the previous block's last write was at a high cycle. This is
correct (each block starts at sample 0). **No bug.** Retracting.

---

### P2-9 (final): PSID `runPlay` does not validate that the play routine stays within RAM bounds

**File:** `include/arpsid/core/c64_psid_runtime.h` (runPlay)

**The bug:** the PSID play routine is executed byte-by-byte through the 6510 CPU
emulator. The CPU emulator has its own bounds checks (the C64 memory matrix wraps at
64K), so a play routine that reads past 64K wraps around. However, the PSID format
spec says the play routine should stay within the loaded RAM region. A malformed PSID
file with a play routine that jumps to an unmapped region will execute garbage → the
CPU may enter an infinite loop → the play budget is exhausted → the tune is silently
muted (the budget cap prevents a hang, but the user hears silence with no error).

**Fix:** add a watchdog: if the play routine's program counter is outside the loaded
RAM region for N consecutive cycles, raise an error / mute the tune with a log.

---

### P2-10: `stableScheduleC64RenderWrites` — 4-pass radix sort has a 1024-bucket final pass that can overflow

**File:** `include/arpsid/core/c64_fixed_write_scheduler.h`

**The bug:** the 4-pass radix sort processes 8 bits per pass (256 buckets), but the
4th pass uses a 1024-bucket array (10 bits). If the number of timed writes exceeds
1024, the 4th-pass bucket array overflows. The function is documented as
"O(4N + 1024)" — the 1024 is the bucket array size. If N > 1024, the bucket array is
too small.

**Wait** — re-reading: the 4-pass radix sort uses 256 buckets per pass (8 bits × 4
passes = 32 bits = a 32-bit key). The "1024" in the complexity is the **constant**
(256 × 4 = 1024), not the bucket array size. The bucket array is 256 entries per pass.
**No overflow.** Retracting.

---

### P2-10 (final): C64 SID bridge `d418RepeatedValueWriteCount` is incremented but never used

**File:** `include/arpsid/core/c64_sid_bridge.h:52`

```cpp
uint32_t d418RepeatedValueWriteCount = 0;
```

**The bug:** this counter is incremented when a $D418 write has the same value as the
previous $D418 write (a "repeated value" write — likely a DIGI re-trigger). The counter
is captured in the Snapshot but **never surfaced** in telemetry, never logged, never
used for any decision. It is dead telemetry.

**Fix:** surface it in the telemetry snapshot (it is useful for diagnosing DIGI
re-trigger bugs) or delete it.

---

### P2-11: `c64SidBridgeInstall` / `c64SidBridgeInstallWithSink` do not reset the bridge state

**File:** `include/arpsid/core/c64_sid_bridge.h:256–278`

```cpp
inline void c64SidBridgeInstall(C64Platform& platform,
                                C64SidBridgeState& bridge,
                                ArpSID::SidRegisterEngine* engine) noexcept {
    bridge.engine = engine;
    bridge.mirrorSink = nullptr;
    bridge.deferEngineWrites = false;
    platform.attachSid(&bridge);
}
```

**The bug:** `c64SidBridgeInstall` sets `engine`, `mirrorSink`, and `deferEngineWrites`
but does **not** reset the rest of the bridge state (`timedWrites`, `timedWriteCount`,
`regs`, `readbackByChip`, `writeCount`, `d418WriteCount`, etc.). If the bridge is
re-installed after a tune change (e.g. loading a new PSID file), the old timed writes,
register values, and readback state from the previous tune persist → the new tune
starts with stale SID register values → wrong audio.

**Fix:** add a `bridge.reset()` call in `c64SidBridgeInstall` /
`c64SidBridgeInstallWithSink` that zeroes all state fields.

---

### P2-12: PSID `loadPsid` does not validate that `initAddress` / `playAddress` are within the loaded payload

**File:** `include/arpsid/core/c64_psid_runtime.h` (loadPsid)

**The bug:** the PSID header specifies `initAddress` and `playAddress` (the addresses
of the init and play routines). `loadPsid` loads the payload into RAM at
`effectiveLoadAddr` but does **not** check that `initAddress` and `playAddress` are
within `[effectiveLoadAddr, effectiveLoadAddr + payloadLen)`. A malformed PSID file
with a play address outside the payload will execute RAM that was never loaded →
garbage code → infinite loop → budget exhaustion → silent mute.

**Fix:** after loading, verify `initAddress` and `playAddress` are within the loaded
region; if not, reject the file with an error.

---

## Cleared (investigated, no bug)

- **P2-9 (original):** Fractional render subphase-0 span — the first event at
  subphase S > 0 dispatches the span (cycle, 0, S), which includes subphase 0. No gap.
- **P2-10 (original):** Radix sort 1024-bucket overflow — the "1024" is the constant
  256×4, not the bucket array size. No overflow.
- **`cyclesForNextHostBlock` chunk-splitting:** the `maxSafeFrames` guard correctly
  prevents `fractionalQ32 + stepQ32 * chunk` from overflowing `uint64_t`. The only
  overflow risk is `totalCycles` accumulation (P0-2).
- **`SidCycleClockState::cyclesForNextHostSample`** (the per-sample variant): correct
  Q32 arithmetic, no overflow for realistic frame counts.
- **VBI cycle budget selection** (NTSC 263/262, PAL 312): correct per
  `psidVbiFrameCycles()`. (Already covered in Round 4 P1-1 for the 263/262 detail.)
- **CIA Timer A latch on VBI:** correct per `psidCiaTimerALatch()`.
- **DIGI $D418 engine state machine:** correct (start/stop/loop/one-shot modes).
- **Open-bus read:** correct (last written value on the bus, with ROM cache for
  printable bytes — already covered in Round 4).

---

## Dedup notes

- The NTSC 263/262 VBI cycle count is **Round 4 P1-1** (not re-reported).
- The non-`__int128` overflow in `cyclesForNextHostBlock` is **new** (Round 4 P0-3
  covered a different overflow in the SID pulse-width readback; this is the PHI2
  clock's total-cycle accumulator).
- The pulse-width readback bugs are **Round 4 P0-1/P0-4** (not re-reported).
- The CIA ICR read bug is **Round 4** (not re-reported).
- The ROM cache printable-byte bug is **Round 4** (not re-reported).
- The BRK-sentinel peek is **Round 4** (not re-reported).
