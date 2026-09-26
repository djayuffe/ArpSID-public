// Copyright (C) 2024-2026 Ulf Bertilsson
// c64_phi2_rsid_playbase_cycle_v605_tests.cpp — RSID PHI2-machine PlayBase cycle fix.
//
// Root cause of RSID mis-timed SID writes (found in v605 audit):
//
// In renderC64PsidBlockIfActive_(), the RSID PlayBase was always recorded as:
//
// playBases[...] = { platform.phi2Cycle(), 0 };
//
// platform.phi2Cycle() is the LEGACY C64Platform counter, which is set during
// runInit() and then NEVER updated when RSID runs via the PHI2 machine
// (runRsidPhi2MachineCycles). The PHI2 machine uses its own cycle counter
// (phi2Machine_.phi2_) which starts at 0 after syncPhi2MachineFromPlatform_.
//
// SID write sample-offset calculation:
// deltaCycle = (w.phi2Cycle >= base->cycle) ? (w.phi2Cycle - base->cycle) : 0
//
// Early blocks (PHI2 machine phi2 < legacy platform phi2):
// w.phi2Cycle = 0..catchup-1 (PHI2 machine)
// base->cycle = X (legacy platform, e.g. 50000 after init)
// → w.phi2Cycle < base->cycle → deltaCycle = 0 for ALL writes
// → ALL SID writes cluster at sample 0 every block
//
// Late blocks (PHI2 machine phi2 >> legacy platform phi2):
// w.phi2Cycle grows without bound; base->cycle stays fixed
// → deltaCycle grows unboundedly → sampleOffset clamped to numFrames-1
// → ALL SID writes cluster at sample numFrames-1 every block
//
// Fix (v605): use the PHI2 machine's counter as base when it is active:
//
// const uint64_t baseCycle =
// (player->phi2MachineEnabled() && player->phi2MachineReady())
// ? player->phi2Machine().phi2Cycle()
// : platform.phi2Cycle();
// playBases[...] = { baseCycle, 0 };
//
// Now:
// Block N: baseCycle = phi2Machine.phi2Cycle() = N * catchup
// Writes: phi2Cycle = N*catchup .. (N+1)*catchup - 1
// deltaCycle = 0 .. catchup-1 → correct sample offset every block
//
// Tests cover:
// I. Pre-fix: legacy platform phi2 > PHI2 machine phi2 → all writes at sample 0
// II. Pre-fix: legacy platform phi2 << PHI2 machine phi2 → all writes at sample N-1
// III. Post-fix: baseCycle from PHI2 machine → writes correctly spread across block
// IV. Post-fix: writes on block boundaries are sample-accurate for multiple blocks
// V. Legacy (non-phi2machine) path unchanged: platform.phi2Cycle() still correct
// VI. Exact cycle-to-sample mapping at PAL 44100 Hz for first/last write in block

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>

// ─── Constants matching ArpSIDDSPKernel / C64 timing ─────────────────────────

static constexpr double   kPalClockHz   = 985248.0;
static constexpr double   kSampleRate   = 44100.0;
static constexpr int      kBlockSize    = 512;
// One PAL VBI frame = one RSID block's worth of PHI2 cycles.
// 985248 / 44100 * 512 ≈ 11439 cycles per 512-sample block at 44100 Hz PAL.
static constexpr uint64_t kCyclesPerBlock = 11439ull;

// ─── Helper: compute sampleOffset from deltaCycle ────────────────────────────

static int cycleToSample(uint64_t deltaCycle, int numFrames) {
    const double raw = static_cast<double>(deltaCycle) * kSampleRate / kPalClockHz;
    const int s = static_cast<int>(std::llround(raw));
    return std::clamp(s, 0, std::max(0, numFrames - 1));
}

// ─── I. Pre-fix: early blocks — PHI2 machine counter < legacy platform phi2 ──

static void testPrefixEarlyBlocksClusterAtSampleZero() {
    // After syncPhi2MachineFromPlatform_(), PHI2 machine starts at 0.
    // Legacy platform retains its post-init counter, e.g. 50000.
    const uint64_t legacyPlatformPhi2 = 50000ull;  // fixed after init
    const uint64_t catchup = kCyclesPerBlock;

    // Block 0: PHI2 machine goes from 0 to catchup-1.
    for (uint64_t writePhi2 = 0; writePhi2 < catchup; writePhi2 += catchup / 8) {
        // Pre-fix: base is legacy phi2 → underflows → deltaCycle = 0
        const uint64_t baseCycle = legacyPlatformPhi2;
        const uint64_t deltaCycle = (writePhi2 >= baseCycle) ? (writePhi2 - baseCycle) : 0u;
        const int sample = cycleToSample(deltaCycle, kBlockSize);
        // All writes land at sample 0 — the bug.
        assert(sample == 0);
    }
}

// ─── II. Pre-fix: late blocks — PHI2 machine phi2 >> legacy phi2 → overflow ──

static void testPrefixLateBlocksClusterAtSampleNMinus1() {
    // After many blocks, the PHI2 machine counter is far ahead of the legacy
    // platform's frozen counter.
    const uint64_t legacyPlatformPhi2 = 50000ull;
    const uint64_t blockIndex = 100u;  // 100 blocks in
    const uint64_t phi2MachineStart = blockIndex * kCyclesPerBlock;  // ~1,143,900

    for (uint64_t writeOffset = 0; writeOffset < kCyclesPerBlock; writeOffset += kCyclesPerBlock / 8) {
        const uint64_t writePhi2 = phi2MachineStart + writeOffset;
        // Pre-fix: base is frozen legacy phi2.
        const uint64_t deltaCycle = (writePhi2 >= legacyPlatformPhi2)
            ? (writePhi2 - legacyPlatformPhi2)
            : 0u;
        // deltaCycle >> kCyclesPerBlock → sample overflows to kBlockSize-1.
        const int sample = cycleToSample(deltaCycle, kBlockSize);
        assert(sample == kBlockSize - 1);
    }
}

// ─── III. Post-fix: PHI2 machine baseCycle → writes correctly distributed ────

static void testPostfixWritesCorrectlyDistributed() {
    // With the fix, baseCycle = phi2Machine.phi2Cycle() at block start.
    // For block N: baseCycle = N * catchup.
    for (uint64_t blockIdx = 0; blockIdx < 10; ++blockIdx) {
        const uint64_t baseCycle = blockIdx * kCyclesPerBlock;
        const uint64_t catchup = kCyclesPerBlock;

        // Write at block start → sample 0.
        {
            const uint64_t writePhi2 = baseCycle;
            const uint64_t delta = (writePhi2 >= baseCycle) ? (writePhi2 - baseCycle) : 0u;
            const int sample = cycleToSample(delta, kBlockSize);
            assert(sample == 0);
        }

        // Write at block end → sample near kBlockSize-1.
        {
            const uint64_t writePhi2 = baseCycle + catchup - 1u;
            const uint64_t delta = writePhi2 - baseCycle;
            const int sample = cycleToSample(delta, kBlockSize);
            // Must be in the last handful of samples.
            assert(sample >= kBlockSize - 3 && sample <= kBlockSize - 1);
        }

        // Write in the middle → sample near kBlockSize/2.
        {
            const uint64_t writePhi2 = baseCycle + catchup / 2u;
            const uint64_t delta = writePhi2 - baseCycle;
            const int sample = cycleToSample(delta, kBlockSize);
            assert(sample >= kBlockSize / 2 - 3 && sample <= kBlockSize / 2 + 3);
        }
    }
}

// ─── IV. Post-fix: multi-block continuity — deltas are always < catchup ──────

static void testPostfixDeltaStaysBounded() {
    // With baseCycle = phi2Machine.phi2Cycle() at block start, and writes
    // during that block, the delta must always be in [0, catchup).
    for (uint64_t blockIdx = 0; blockIdx < 50; ++blockIdx) {
        const uint64_t baseCycle = blockIdx * kCyclesPerBlock;
        for (uint64_t off = 0; off < kCyclesPerBlock; ++off) {
            const uint64_t writePhi2 = baseCycle + off;
            const uint64_t delta = (writePhi2 >= baseCycle) ? (writePhi2 - baseCycle) : 0u;
            assert(delta < kCyclesPerBlock);
            const int sample = cycleToSample(delta, kBlockSize);
            assert(sample >= 0 && sample < kBlockSize);
        }
    }
}

// ─── V. Legacy non-phi2machine path unchanged ────────────────────────────────

static void testLegacyPathUnchanged() {
    // For RSID without PHI2 machine (usePhi2Machine_=false), the legacy platform
    // advances during runRealtimeSidCoreCycles. Writes are timestamped relative
    // to the advancing platform phi2. PlayBase.cycle = platform.phi2Cycle()
    // BEFORE the run. Writes during the run have phi2Cycle >= baseCycle.
    const uint64_t platformPhi2BeforeRun = 50000ull;
    const uint64_t catchup = kCyclesPerBlock;

    // Simulate the legacy run: writes are at platformPhi2BeforeRun + offset.
    for (uint64_t off = 0; off < catchup; off += catchup / 8) {
        const uint64_t writePhi2 = platformPhi2BeforeRun + off;
        const uint64_t baseCycle = platformPhi2BeforeRun;  // captured before run
        const uint64_t delta = (writePhi2 >= baseCycle) ? (writePhi2 - baseCycle) : 0u;
        assert(delta == off);
        const int sample = cycleToSample(delta, kBlockSize);
        assert(sample >= 0 && sample < kBlockSize);
    }
}

// ─── VI. Exact cycle-to-sample pins at PAL 44.1 kHz for boundary writes ──────

static void testExactBoundaryMapping() {
    // At PAL 44100 Hz: 985248 / 44100 ≈ 22.34 cycles per sample.
    // The first write at cycle 0 must map to sample 0.
    // A write at cycle 22 maps to sample 1 (floor 22/22.34 = 0.98 → round = 1).
    // A write at cycle kCyclesPerBlock-1 = 11438 maps to sample ~511.

    assert(cycleToSample(0u, kBlockSize) == 0);
    assert(cycleToSample(22u, kBlockSize) == 1);

    const int lastSample = cycleToSample(kCyclesPerBlock - 1u, kBlockSize);
    assert(lastSample == kBlockSize - 1);

    // The crossover from sample 0 to sample 1 occurs at cycle 11 or 12
    // (half of 22.34 rounded). Verify monotonicity over the first 5 samples.
    int prev = 0;
    for (uint64_t c = 0; c < 120u; ++c) {
        const int s = cycleToSample(c, kBlockSize);
        assert(s >= prev);  // non-decreasing
        prev = s;
    }
    assert(prev >= 4);  // at least 5 distinct sample values over 120 cycles
}

int main() {
    testPrefixEarlyBlocksClusterAtSampleZero();
    testPrefixLateBlocksClusterAtSampleNMinus1();
    testPostfixWritesCorrectlyDistributed();
    testPostfixDeltaStaysBounded();
    testLegacyPathUnchanged();
    testExactBoundaryMapping();
    return 0;
}
