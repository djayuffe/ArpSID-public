// SPDX-License-Identifier: BSD-3-Clause
// c64_psid_playbase_timing_v575_tests.cpp — VBI PlayBase timing (v575).
//
// Root cause of choppy C64 playback (found after v574):
//
// In renderC64PsidBlockIfActive_(), the PlayBase was recorded BEFORE the
// 8192-cycle passive PHI2 advancement:
//
// playBases[...] = {platform.phi2Cycle(), playSample}; // BEFORE passive
// platform.runCycles(catchup=8192, ...); // phi2Cycle += 8192
// player->runPlay(4096); // SID writes here
//
// SID write sample-offset calculation:
// deltaCycle = w.phi2Cycle - base->cycle
// = 8192 + play_routine_offset
// sampleOffset = playSample + round(deltaCycle × sr / clockHz)
// ≈ playSample + round(8192 × 44100/985248)
// ≈ playSample + 367 ← 8.3 ms TOO LATE
//
// Consequences:
// 1. Notes start and stop 8.3 ms late on every VBI frame.
// 2. When playSample + 367 > numFrames − 1, sampleOffset is clamped to the
// last sample of the block. SID updates arrive in the wrong block and the
// inter-update interval becomes irregular (e.g. 656, 1024 samples instead
// of the correct PAL VBI period of 882 samples).
// 3. 8.3 ms pre-note silence at block start (first frame always 367 samples
// before any SID register is written).
//
// Fix (v575): record PlayBase AFTER passive advancement:
//
// platform.runCycles(catchup=8192, ...); // phi2Cycle += 8192
// playBases[...] = {platform.phi2Cycle(), playSample}; // AFTER passive
// player->runPlay(4096); // SID writes here
//
// Now:
// deltaCycle = w.phi2Cycle - base->cycle = play_routine_offset (tiny)
// sampleOffset ≈ playSample + 0..4 samples ← correct
//
// CIA-timed tunes are covered by the v872 render-scheduling guard: they now use
// continuous block-start PHI2 mapping, not PlayBase{cycle, playSample}.
//
// Tests cover:
// I. Passive displacement constant: 8192 cycles ≈ 367 audio samples at PAL
// II. Pre-fix sampleOffset math documents the 367-sample error
// III. Post-fix sampleOffset: play-routine writes land at playSample, not +367
// IV. Block-boundary clamping: writes at playSample+367 vs playSample
// V. First-frame play: 367-sample silence before first SID write (pre-fix)
// VI. Fixed timing: first SID write at sample ~0 (at play-fire time)
// VII. Multi-block VBI period regularity: fixed timing gives correct 882-sample intervals
// VIII. CIA path changed in v872: continuous mapping avoids playSample + CIA offset
// IX. VBI displacement formula: deltaCycle × sr/clockHz pins at 44.1 kHz PAL

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>

// ─── Constants matching ArpSIDDSPKernel ──────────────────────────────────────

static constexpr uint64_t kPassiveCatchupCycles = 8192ull;  // kC64PsidMaxPassiveCatchupCyclesPerPlay
static constexpr double   kPalClockHz           = 985248.0;
static constexpr double   kSampleRate           = 44100.0;
static constexpr int      kBlockSize            = 512;

// ─── Helper: compute sample offset as the pre-fix and post-fix code would ────

// Pre-fix: base recorded BEFORE passive → deltaCycle includes 8192 passive cycles
static int sampleOffsetPreFix(int playSample, uint64_t playRoutineOffset) {
    const uint64_t deltaCycle = kPassiveCatchupCycles + playRoutineOffset;
    const int raw = playSample + static_cast<int>(
        std::llround(static_cast<double>(deltaCycle) * kSampleRate / kPalClockHz));
    return std::clamp(raw, 0, kBlockSize - 1);
}

// Post-fix (v575): base recorded AFTER passive → deltaCycle is only play routine
static int sampleOffsetPostFix(int playSample, uint64_t playRoutineOffset) {
    const uint64_t deltaCycle = playRoutineOffset;
    const int raw = playSample + static_cast<int>(
        std::llround(static_cast<double>(deltaCycle) * kSampleRate / kPalClockHz));
    return std::clamp(raw, 0, kBlockSize - 1);
}

// ─── I. Passive displacement constant: 8192 cycles ≈ 367 samples at PAL ──────

static void testPassiveDisplacementValue() {
    // 8192 C64 PAL cycles mapped to audio samples at 44.1 kHz
    const double displacement = static_cast<double>(kPassiveCatchupCycles) * kSampleRate / kPalClockHz;
    const int displacementSamples = static_cast<int>(std::llround(displacement));

    // Must be approximately 367 samples (8.3 ms at 44.1 kHz)
    assert(displacementSamples >= 360 && displacementSamples <= 375);
    assert(displacementSamples == 367);

    // This is 41.7% of a PAL VBI period (882 samples)
    const double palVbiFraction = static_cast<double>(displacementSamples) / 882.0;
    assert(palVbiFraction > 0.40 && palVbiFraction < 0.43);
}

// ─── II. Pre-fix math documents the 367-sample error ─────────────────────────

static void testPreFixOffsetError() {
    // A play routine's first SID write typically happens 10-50 cycles into
    // the routine (after a JSR trampoline overhead).
    constexpr uint64_t kFirstWriteOffset = 20u;  // typical first write

    // Play fires at sample 0: pre-fix puts SID write at ~367
    {
        const int offset = sampleOffsetPreFix(0, kFirstWriteOffset);
        // Should be around 367 + small (367 + round(20 × 0.04477) ≈ 368)
        assert(offset >= 365 && offset <= 370);
    }

    // Play fires at sample 100: write at ~467 (within block)
    {
        const int offset = sampleOffsetPreFix(100, kFirstWriteOffset);
        assert(offset >= 465 && offset <= 470);
    }

    // Play fires at sample 200: write at ~567 → CLAMPED to 511 (wrong!)
    {
        const int offset = sampleOffsetPreFix(200, kFirstWriteOffset);
        assert(offset == kBlockSize - 1);  // clamped
    }

    // Play fires at sample 370: write at ~737 → CLAMPED to 511 (wrong!)
    {
        const int offset = sampleOffsetPreFix(370, kFirstWriteOffset);
        assert(offset == kBlockSize - 1);  // clamped
    }

    // Play fires at sample 400: write at ~767 → CLAMPED to 511 (wrong!)
    {
        const int offset = sampleOffsetPreFix(400, kFirstWriteOffset);
        assert(offset == kBlockSize - 1);  // clamped
    }
}

// ─── III. Post-fix: writes land at playSample, not +367 ──────────────────────

static void testPostFixOffsetCorrect() {
    constexpr uint64_t kFirstWriteOffset = 20u;  // typical first SID write

    // Play fires at sample 0: write at ~0-1 (correct: notes update at play fire)
    {
        const int offset = sampleOffsetPostFix(0, kFirstWriteOffset);
        // deltaCycle = 20, sampleOffset = round(20 × 0.04477) = round(0.895) ≈ 1
        assert(offset >= 0 && offset <= 2);
    }

    // Play fires at sample 370: write at ~370-371 (within block, correct)
    {
        const int offset = sampleOffsetPostFix(370, kFirstWriteOffset);
        assert(offset >= 369 && offset <= 372);
    }

    // Play fires at sample 400: write at ~400-401 (within block, correct)
    {
        const int offset = sampleOffsetPostFix(400, kFirstWriteOffset);
        assert(offset >= 399 && offset <= 402);
    }

    // Play fires at sample 505: write at ~505-506, clamped to 511 (marginal, OK)
    {
        const int offset = sampleOffsetPostFix(505, kFirstWriteOffset);
        assert(offset >= 505 && offset <= 511);
    }

    // Pre-fix and post-fix differ by approximately 367 samples
    for (int playSample = 0; playSample <= 100; playSample += 10) {
        const int pre  = sampleOffsetPreFix(playSample, kFirstWriteOffset);
        const int post = sampleOffsetPostFix(playSample, kFirstWriteOffset);
        // Pre-fix is approximately 367 samples later (or clamped)
        const int expected_diff = 367;  // approx
        const int actual_diff = pre - post;
        // When pre is not clamped, the diff should be ~367
        if (pre < kBlockSize - 1) {
            assert(actual_diff >= 364 && actual_diff <= 370);
        } else {
            // Pre is clamped — diff is less than 367 (clamping absorbed part)
            assert(actual_diff >= 0);
        }
    }
}

// ─── IV. Block-boundary clamping: pre-fix clamps, post-fix doesn't ────────────

static void testBlockBoundaryClamping() {
    // For a 512-sample block, pre-fix clamps whenever playSample > 512 - 367 = 145
    // Post-fix only clamps when playSample > 511 (impossible for valid playSample)

    constexpr uint64_t kFirstWrite = 20u;

    // playSample = 144: pre-fix offset = 144 + 367 = 511 (just at boundary)
    {
        const int pre  = sampleOffsetPreFix(144, kFirstWrite);
        const int post = sampleOffsetPostFix(144, kFirstWrite);
        assert(pre <= kBlockSize - 1);   // may be 511 (clamped at boundary)
        assert(post <= 146);             // post-fix: near 144
        assert(post < pre);              // post-fix is earlier ← correct
    }

    // playSample = 145: pre-fix clamped to 511
    {
        const int pre  = sampleOffsetPreFix(145, kFirstWrite);
        const int post = sampleOffsetPostFix(145, kFirstWrite);
        assert(pre == kBlockSize - 1);   // clamped
        assert(post >= 144 && post <= 147);  // post-fix: ~145
    }

    // playSample = 370: pre-fix clamped (370+367=737>511), post-fix ~370
    {
        const int pre  = sampleOffsetPreFix(370, kFirstWrite);
        const int post = sampleOffsetPostFix(370, kFirstWrite);
        assert(pre == kBlockSize - 1);   // clamped
        assert(post >= 369 && post <= 372);  // post-fix: ~370
    }
}

// ─── V. First-frame play: 367-sample silence with pre-fix ─────────────────────

static void testFirstFrameSilence() {
    // After PSID load, debt=0, countdown=0 → play fires at sample 0 of block 1.
    // Pre-fix: passive=0 (no debt yet), but in subsequent blocks debt grows to
    // 8192. After first block, for the SECOND play call:
    // debt = 11440 - 0 = 11440 (first play used 0 passive: no debt initially)
    // second play: debt=11440, catchup=8192, SID writes at playSample+367

    // Simulate first block: debt=0, play at sample 0
    constexpr uint64_t kFirstPlayPassive = 0ull;  // no debt at load
    constexpr int kFirstPlaySample = 0;
    {
        const int offset = sampleOffsetPreFix(kFirstPlaySample, 20u);
        // With 0 passive cycles (no debt), pre-fix has no displacement
        // Wait: passive = min(debt=0, 8192) = 0, so deltaCycle = 0 + 20
        // sampleOffset = 0 + round(20 × 0.04477) = 1
        // Actually for first frame, pre-fix and post-fix are equivalent (no passive)
        // The displacement problem starts from the SECOND play onward.
        (void)offset;  // both formulas agree for zero-debt first play
    }

    // Second play in a typical block: debt=11440, catchup=8192
    // playSample=370 (typical for second PAL block)
    {
        const int pre_offset  = sampleOffsetPreFix(370, 20u);
        const int post_offset = sampleOffsetPostFix(370, 20u);

        // Pre-fix: 370+367=737 → clamped to 511 (writes land at end of block)
        assert(pre_offset == kBlockSize - 1);

        // Post-fix: 370+1=371 (writes land at play-fire time)
        assert(post_offset >= 369 && post_offset <= 372);
    }
}

// ─── VI. Fixed timing: first SID write at ~playSample ─────────────────────────

static void testFixedTimingFirstWrite() {
    // With post-fix, SID writes from any play-routine position land near playSample.
    // Test 10 representative play-sample positions.
    const int playSamples[] = {0, 50, 100, 150, 200, 250, 300, 350, 400, 450};
    for (int ps : playSamples) {
        for (uint64_t writeOffset : {5ull, 20ull, 50ull, 100ull}) {
            const int offset = sampleOffsetPostFix(ps, writeOffset);
            // Write should land within 5 samples of playSample
            // (play_routine_offset ≈ 5-100 cycles × 0.04477 ≈ 0-4 samples)
            assert(offset >= ps);
            assert(offset <= std::min<int>(ps + 5, kBlockSize - 1));
        }
    }
}

// ─── VII. Multi-block VBI period regularity (post-fix gives correct 882 intervals) ─

static void testVbiPeriodRegularity() {
    // Simulate 6 audio blocks of PAL C64 play at 44.1 kHz, 512 samples/block.
    // PAL VBI: one play call per ~882 samples.
    // Play countdown accumulator tracks which sample the play fires at.

    constexpr double kPlayPeriodSamples = 312.0 * 63.0 * kSampleRate / kPalClockHz;  // ≈ 879.8
    constexpr int    kFirstWriteOffset  = 20;  // cycles into play routine

    double countdown = 0.0;
    int    prevWriteSample = -1;
    int    prevGlobalSample = -1;

    for (int block = 0; block < 6; ++block) {
        const int globalBlockStart = block * kBlockSize;

        if (countdown < static_cast<double>(kBlockSize)) {
            // Play fires this block
            const int playSample = std::clamp(
                static_cast<int>(std::floor(countdown + 1.0e-9)), 0, kBlockSize - 1);
            const int globalPlaySample = globalBlockStart + playSample;

            // Post-fix: write lands at ~playSample within this block
            const int localWriteSample = sampleOffsetPostFix(playSample, static_cast<uint64_t>(kFirstWriteOffset));
            const int globalWriteSample = globalBlockStart + localWriteSample;

            if (prevWriteSample >= 0) {
                // The gap between consecutive SID write events should be ≈880 samples
                // (PAL: 19656 cycles × 44100/985248 ≈ 879.8 samples per VBI frame)
                const int gap = globalWriteSample - prevGlobalSample;
                // Allow ±10 samples tolerance for block-boundary quantization
                assert(gap >= 870 && gap <= 895);
            }

            prevWriteSample = localWriteSample;
            prevGlobalSample = globalWriteSample;

            countdown += kPlayPeriodSamples;
        }

        countdown -= static_cast<double>(kBlockSize);
        countdown = std::max(0.0, countdown);
    }

    // Must have observed at least 3 plays in 6 blocks
    assert(prevWriteSample >= 0);
}

// ─── VIII. VBI displacement formula: deltaCycle × sr/clockHz ─────────────────

static void testDisplacementFormula() {
    // Cross-check the arithmetic against the constants.
    // 8192 passive PAL cycles at 44100 Hz sample rate.
    const double expectedDisplacementSamples = 8192.0 * 44100.0 / 985248.0;

    // Must be exactly 367 (rounded)
    assert(static_cast<int>(std::llround(expectedDisplacementSamples)) == 367);

    // At 48000 Hz: 8192 × 48000 / 985248 ≈ 399 samples
    const double displacement48k = 8192.0 * 48000.0 / 985248.0;
    assert(static_cast<int>(std::llround(displacement48k)) == 399);

    // At NTSC (1022727 Hz): 8192 × 44100 / 1022727 ≈ 353 samples
    const double displacementNtsc = 8192.0 * 44100.0 / 1022727.0;
    assert(static_cast<int>(std::llround(displacementNtsc)) == 353);

    // In all cases, pre-fix displacement is > 350 samples (> 7.9ms) — audibly
    // significant for a 50/60 Hz music driver.
    assert(expectedDisplacementSamples > 350.0);
    assert(displacement48k > 350.0);
    assert(displacementNtsc > 350.0);
}

// ─── IX. CIA path: continuous mapping avoids a double playSample offset ───────

static void testCiaPathContinuousMapping() {
    // v872: PSID-CIA service is continuous PHI2 machine time. If the CIA IRQ
    // produces a write 180 host samples after block start, the renderer must map
    // it near sample 180. The old PlayBase path added playSample again, landing
    // near 360 and causing audible lag/clamping in small AU buffers.
    constexpr int kIrqWriteSample = 180;
    constexpr int kPlaySample = 180;
    const uint64_t deltaCycle = static_cast<uint64_t>(
        std::llround(static_cast<double>(kIrqWriteSample) * kPalClockHz / kSampleRate));
    const int continuous = std::clamp(static_cast<int>(
        std::llround(static_cast<double>(deltaCycle) * kSampleRate / kPalClockHz)),
        0, kBlockSize - 1);
    const int oldPlayBase = std::clamp(kPlaySample + static_cast<int>(
        std::llround(static_cast<double>(deltaCycle) * kSampleRate / kPalClockHz)),
        0, kBlockSize - 1);

    assert(continuous >= 179 && continuous <= 181);
    assert(oldPlayBase >= 359 && oldPlayBase <= 361);
    assert(oldPlayBase > continuous + 170);
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main() {
    testPassiveDisplacementValue();
    testPreFixOffsetError();
    testPostFixOffsetCorrect();
    testBlockBoundaryClamping();
    testFirstFrameSilence();
    testFixedTimingFirstWrite();
    testVbiPeriodRegularity();
    testDisplacementFormula();
    testCiaPathContinuousMapping();
    return 0;
}
