// Copyright (C) 2024-2026 Ulf Bertilsson
// c64_psid_block_timedwrite_clear_v584_tests.cpp
// v584: Clear timed-write storage array at block start.
//
// resetTimedWrites() zeroes timedWriteCount and timedWriteOverflow but leaves
// the timedWrites[] array itself populated with data from the previous block.
// Pre-v584, entries beyond timedWriteCount carried stale register/cycle data
// from the prior block. The audio render loop was safe (it only iterates
// up to timedWriteCount), but diagnostic tools, telemetry consumers, and the
// BridgeTransaction rollback path (v583) could observe abandoned writes.
//
// Fix: after resetTimedWrites() at block start, zero the full timedWrites[]
// array (timedWrites = {}). The regs[] register mirror is preserved — it
// tracks the last audible SID state and must survive block boundaries for
// correct read-back and seeding.
//
// Tests use arithmetic/state-machine simulation — no AU/C64 linkage.

#include <cassert>
#include <cstdint>
#include <array>

static constexpr uint32_t kMaxTimedWrites = 16u;  // test-scale

struct TimedWrite { uint8_t reg; uint8_t value; uint64_t phi2Cycle; };

struct MockBridge {
    std::array<uint8_t, 32u> regs{};
    std::array<TimedWrite, kMaxTimedWrites> timedWrites{};
    uint32_t timedWriteCount = 0u;

    void write(uint8_t reg, uint8_t value, uint64_t cycle) noexcept {
        regs[reg & 0x1Fu] = value;
        if (timedWriteCount < kMaxTimedWrites)
            timedWrites[timedWriteCount++] = {reg, value, cycle};
    }
    void resetTimedWrites() noexcept { timedWriteCount = 0u; }
    void clearTimedWriteStorage() noexcept { timedWrites = {}; }  // v584
};

// ── §1 resetTimedWrites alone leaves stale data in array ────────────────────
static void testResetAloneLeavesStaleData()
{
    MockBridge b;
    b.write(0x04u, 0xAAu, 100u);
    b.write(0x05u, 0xBBu, 200u);
    assert(b.timedWriteCount == 2u);
    assert(b.timedWrites[0].value == 0xAAu);
    assert(b.timedWrites[1].value == 0xBBu);

    b.resetTimedWrites();
    assert(b.timedWriteCount == 0u);
    // Stale data remains beyond count.
    assert(b.timedWrites[0].value == 0xAAu);  // not cleared — stale
    assert(b.timedWrites[1].value == 0xBBu);  // not cleared — stale
}

// ── §2 Block-start clear zeroes all entries ──────────────────────────────────
static void testBlockStartClearZeroesAll()
{
    MockBridge b;
    // Previous block wrote kMaxTimedWrites entries.
    for (uint32_t i = 0u; i < kMaxTimedWrites; ++i)
        b.write(static_cast<uint8_t>(i % 25u), static_cast<uint8_t>(i + 1u), static_cast<uint64_t>(i * 10u));

    // Block start: reset count then clear storage.
    b.resetTimedWrites();
    b.clearTimedWriteStorage();  // v584

    assert(b.timedWriteCount == 0u);
    for (uint32_t i = 0u; i < kMaxTimedWrites; ++i) {
        assert(b.timedWrites[i].reg   == 0u);
        assert(b.timedWrites[i].value == 0u);
        assert(b.timedWrites[i].phi2Cycle == 0u);
    }
}

// ── §3 regs[] mirror is NOT cleared by block-start sequence ─────────────────
static void testRegsMirrorPreservedAcrossBlockClear()
{
    MockBridge b;
    // Init seeded regs.
    b.regs[0x05] = 0x09u;  // ADSR from init
    b.regs[0x06] = 0xF0u;

    b.write(0x00u, 0x5Bu, 100u);
    assert(b.regs[0x00] == 0x5Bu);

    // Block start.
    b.resetTimedWrites();
    b.clearTimedWriteStorage();

    // regs[] survives.
    assert(b.regs[0x00] == 0x5Bu);
    assert(b.regs[0x05] == 0x09u);
    assert(b.regs[0x06] == 0xF0u);
}

// ── §4 New block writes land cleanly at index 0 ──────────────────────────────
static void testNewBlockWritesLandAtIndexZero()
{
    MockBridge b;
    // Simulated previous block.
    for (uint32_t i = 0u; i < 5u; ++i)
        b.write(static_cast<uint8_t>(i), static_cast<uint8_t>(0xFFu), static_cast<uint64_t>(i));

    b.resetTimedWrites();
    b.clearTimedWriteStorage();

    // New block play writes.
    b.write(0x04u, 0x11u, 1000u);
    b.write(0x00u, 0x5Bu, 1005u);

    assert(b.timedWriteCount == 2u);
    assert(b.timedWrites[0].reg   == 0x04u);
    assert(b.timedWrites[0].value == 0x11u);
    assert(b.timedWrites[1].reg   == 0x00u);
    assert(b.timedWrites[1].value == 0x5Bu);
}

// ── §5 BridgeTransaction rollback on clean block is zero-cost ────────────────
static void testRollbackOnCleanBlockIsNoop()
{
    MockBridge b;
    b.regs[0x04] = 0x11u;

    // After block-start clear, bridge tx snapshot sees count = 0.
    b.resetTimedWrites();
    b.clearTimedWriteStorage();

    const uint32_t savedCount = b.timedWriteCount;  // 0
    const auto savedRegs = b.regs;

    // Play writes 3 entries then fails.
    b.write(0x04u, 0xAAu, 100u);
    b.write(0x00u, 0xBBu, 102u);
    b.write(0x01u, 0xCCu, 104u);

    // Rollback.
    for (uint32_t i = savedCount; i < b.timedWriteCount && i < kMaxTimedWrites; ++i)
        b.timedWrites[i] = {};
    b.timedWriteCount = savedCount;
    b.regs = savedRegs;

    assert(b.timedWriteCount == 0u);
    for (uint32_t i = 0u; i < kMaxTimedWrites; ++i) {
        assert(b.timedWrites[i].value == 0u);
    }
    assert(b.regs[0x04] == 0x11u);
}

// ── main ──────────────────────────────────────────────────────────────────────
int main()
{
    testResetAloneLeavesStaleData();
    testBlockStartClearZeroesAll();
    testRegsMirrorPreservedAcrossBlockClear();
    testNewBlockWritesLandAtIndexZero();
    testRollbackOnCleanBlockIsNoop();
    return 0;
}
