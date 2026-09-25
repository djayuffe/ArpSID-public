// c64_psid_bridge_transaction_v583_tests.cpp
// v583: Transactional PSID play — BridgeTransaction for VBI and CIA paths.
//
// A PSID play routine may fail mid-execution (instruction budget exhausted,
// malformed tune, runaway loop). Pre-v583, a partial play committed half// updated SID register state to the bridge and audio engine: missing gate// offs, wrong frequency or ADSR transitions, incomplete arpeggio steps.
// Result: audible pops, wrong notes, or phantom gate-ons that sustain forever.
//
// Fix: wrap every runPlay() and runPsidCiaPlaybackServiceTicks() call in a
// BridgeTransactionSnapshot. On failure, roll back timedWriteCount and regs[]
// to the pre-play snapshot, zeroing stale timed-write entries for telemetry
// cleanliness. Set jammed=true on rollback (complementing the v577 jam rule)
// so passive runCycles() on the next block does not continue executing the
// mid-play CPU state.
//
// CIA-specific: roll back only when the play address was entered but the CIA
// IRQ was not acknowledged (playAddressEntered && !ciaAckObserved). When
// the play address was never entered, no play writes occurred — no rollback.
// When both playAddressEntered and ciaAckObserved are true, the play completed
// — commit.
//
// Tests use arithmetic/state-machine simulation only — no AU/C64 linkage.

#include <cassert>
#include <cstdint>
#include <array>
#include <cstring>
#include <algorithm>

// ── shared constants ──────────────────────────────────────────────────────────
static constexpr uint32_t kMaxTimedWrites = 64u;  // test-scale (not 4096)
static constexpr uint8_t  kSidRegCount    = 32u;

struct TimedWrite { uint8_t reg; uint8_t value; uint64_t phi2Cycle; };

// Minimal bridge mock.
struct MockBridge {
    std::array<uint8_t, kSidRegCount> regs{};
    std::array<TimedWrite, kMaxTimedWrites> timedWrites{};
    uint32_t timedWriteCount = 0u;

    void write(uint8_t reg, uint8_t value, uint64_t cycle) noexcept {
        regs[reg & 0x1Fu] = value;
        if (timedWriteCount < kMaxTimedWrites)
            timedWrites[timedWriteCount++] = {reg, value, cycle};
    }
    void reset() noexcept { regs = {}; timedWrites = {}; timedWriteCount = 0u; }
};

// Minimal transaction implementation matching the kernel.
struct BridgeTx {
    uint32_t timedWriteCount = 0u;
    std::array<uint8_t, kSidRegCount> regs{};
};

static BridgeTx beginTx(const MockBridge& b) noexcept {
    BridgeTx tx;
    tx.timedWriteCount = b.timedWriteCount;
    tx.regs = b.regs;
    return tx;
}

static void rollbackTx(MockBridge& b, const BridgeTx& tx) noexcept {
    const uint32_t oldCount = tx.timedWriteCount;
    const uint32_t newCount = b.timedWriteCount;
    for (uint32_t i = oldCount; i < newCount && i < kMaxTimedWrites; ++i)
        b.timedWrites[i] = {};
    b.timedWriteCount = oldCount;
    b.regs = tx.regs;
}

// ── §1 Successful play commits bridge state ──────────────────────────────────
static void testSuccessfulPlayCommits()
{
    MockBridge bridge;
    bridge.regs[0x05] = 0x09u;  // ADSR from init

    const BridgeTx tx = beginTx(bridge);
    // Simulate play writing freq + gate.
    bridge.write(0x00u, 0x5Bu, 1000u);
    bridge.write(0x01u, 0x0Eu, 1002u);
    bridge.write(0x04u, 0x11u, 1010u);

    // Play completed: commit (no rollback).
    assert(bridge.timedWriteCount == 3u);
    assert(bridge.regs[0x00] == 0x5Bu);
    assert(bridge.regs[0x01] == 0x0Eu);
    assert(bridge.regs[0x04] == 0x11u);
    assert(bridge.regs[0x05] == 0x09u);  // ADSR preserved

    (void)tx;  // transaction not used: commit path
}

// ── §2 Failed play rolls back timedWriteCount and regs ──────────────────────
static void testFailedPlayRollsBack()
{
    MockBridge bridge;
    bridge.regs[0x04] = 0x11u;  // gate+waveform from last committed play
    bridge.regs[0x05] = 0x09u;  // ADSR from init

    const BridgeTx tx = beginTx(bridge);
    // Simulate budget-hit: 2 partial writes before exhaustion.
    bridge.write(0x00u, 0xAAu, 2000u);
    bridge.write(0x04u, 0x01u, 2005u);  // partial: gate-off, waveform cleared

    // Play failed: rollback.
    rollbackTx(bridge, tx);

    assert(bridge.timedWriteCount == 0u);  // writes retracted
    assert(bridge.regs[0x04] == 0x11u);   // gate+waveform from committed play restored
    assert(bridge.regs[0x05] == 0x09u);   // ADSR preserved
    assert(bridge.regs[0x00] == 0x00u);   // partial freq write rolled back
}

// ── §3 Rollback zeroes stale timed-write entries ────────────────────────────
static void testRollbackZeroesStaleEntries()
{
    MockBridge bridge;

    // Pre-populate with one committed play's writes.
    bridge.write(0x00u, 0x11u, 100u);
    bridge.write(0x01u, 0x22u, 102u);
    const uint32_t committed = bridge.timedWriteCount;  // = 2

    // Second play in same block starts.
    const BridgeTx tx = beginTx(bridge);
    bridge.write(0x04u, 0xAAu, 200u);  // partial
    bridge.write(0x00u, 0xBBu, 205u);  // partial

    // Second play fails: rollback.
    rollbackTx(bridge, tx);

    // Count restored to committed writes.
    assert(bridge.timedWriteCount == committed);
    // Entries at [committed..committed+1] are zeroed.
    assert(bridge.timedWrites[committed].reg   == 0u);
    assert(bridge.timedWrites[committed].value == 0u);
    assert(bridge.timedWrites[committed+1u].reg   == 0u);
    assert(bridge.timedWrites[committed+1u].value == 0u);
    // Earlier committed entries are untouched.
    assert(bridge.timedWrites[0].value == 0x11u);
    assert(bridge.timedWrites[1].value == 0x22u);
}

// ── §4 CIA path: commit when playAddressEntered && ciaAckObserved ────────────
static void testCiaCommitWhenComplete()
{
    MockBridge bridge;
    bridge.regs[0x05] = 0xF0u;

    const BridgeTx tx = beginTx(bridge);
    bridge.write(0x00u, 0x71u, 1000u);

    // CIA service: play complete (both entered and ack'd).
    const bool playAddressEntered = true;
    const bool ciaAckObserved     = true;
    const bool playComplete = playAddressEntered && ciaAckObserved;

    if (playComplete) {
        // commit — no rollback
    } else if (playAddressEntered) {
        rollbackTx(bridge, tx);
    }

    assert(bridge.timedWriteCount == 1u);   // committed
    assert(bridge.regs[0x00] == 0x71u);
    (void)tx;
}

// ── §5 CIA path: rollback when play entered but CIA not ack'd ────────────────
static void testCiaRollbackWhenIncomplete()
{
    MockBridge bridge;
    bridge.regs[0x04] = 0x11u;

    const BridgeTx tx = beginTx(bridge);
    bridge.write(0x04u, 0x01u, 3000u);  // partial: gate-off

    // CIA service: play entered but CIA IRQ not ack'd.
    const bool playAddressEntered = true;
    const bool ciaAckObserved     = false;
    const bool playComplete = playAddressEntered && ciaAckObserved;

    bool jammed = false;
    if (playComplete) {
        // commit
    } else if (playAddressEntered) {
        rollbackTx(bridge, tx);
        jammed = true;
    }

    assert(bridge.timedWriteCount == 0u);  // rolled back
    assert(bridge.regs[0x04] == 0x11u);   // gate+wave restored
    assert(jammed);                         // CPU jammed after rollback
}

// ── §6 CIA path: no rollback when play address never entered ─────────────────
static void testCiaNoRollbackWhenNotEntered()
{
    MockBridge bridge;

    // CIA timer didn't fire in this service window.
    const BridgeTx tx = beginTx(bridge);

    // No play writes occurred.
    const bool playAddressEntered = false;
    const bool ciaAckObserved     = false;

    bool rolledBack = false;
    if (playAddressEntered && ciaAckObserved) {
        // commit
    } else if (playAddressEntered) {
        rollbackTx(bridge, tx);
        rolledBack = true;
    }
    // else: !playAddressEntered — do nothing

    assert(!rolledBack);
    assert(bridge.timedWriteCount == 0u);  // unchanged
    (void)tx;
}

// ── §7 Multi-play block: rollback restores only second play ──────────────────
static void testMultiPlayBlockRollback()
{
    MockBridge bridge;

    // First play: complete.
    bridge.write(0x00u, 0x5Bu, 100u);
    bridge.write(0x04u, 0x11u, 110u);
    const uint32_t afterFirstPlay = bridge.timedWriteCount;  // = 2

    // Second play starts.
    const BridgeTx tx = beginTx(bridge);
    bridge.write(0x00u, 0xCCu, 200u);
    bridge.write(0x04u, 0x01u, 210u);  // partial gate-off

    // Second play fails.
    rollbackTx(bridge, tx);

    // Only the second play's writes are retracted.
    assert(bridge.timedWriteCount == afterFirstPlay);
    assert(bridge.timedWrites[0].value == 0x5Bu);  // first play preserved
    assert(bridge.timedWrites[1].value == 0x11u);  // first play preserved
    // Second play writes zeroed.
    assert(bridge.timedWrites[2].value == 0u);
    assert(bridge.timedWrites[3].value == 0u);
}

// ── §8 Rollback with overflow (newCount capped at kMaxTimedWrites) ───────────
static void testRollbackWithCount()
{
    MockBridge bridge;

    const BridgeTx tx = beginTx(bridge);
    // Write exactly kMaxTimedWrites entries.
    for (uint32_t i = 0u; i < kMaxTimedWrites; ++i) {
        bridge.write(static_cast<uint8_t>(i % 25u), static_cast<uint8_t>(i), static_cast<uint64_t>(i));
    }
    assert(bridge.timedWriteCount == kMaxTimedWrites);

    // Rollback.
    rollbackTx(bridge, tx);

    assert(bridge.timedWriteCount == 0u);
    // All entries zeroed.
    for (uint32_t i = 0u; i < kMaxTimedWrites; ++i) {
        assert(bridge.timedWrites[i].value == 0u);
    }
}

// ── §9 beginTx is a pure snapshot (no side effects) ─────────────────────────
static void testBeginTxIsNonMutating()
{
    MockBridge bridge;
    bridge.write(0x05u, 0xF0u, 1u);

    const uint32_t countBefore = bridge.timedWriteCount;
    const uint8_t  regBefore   = bridge.regs[0x05];

    const BridgeTx tx = beginTx(bridge);

    assert(bridge.timedWriteCount == countBefore);
    assert(bridge.regs[0x05] == regBefore);

    (void)tx;
}

// ── §10 ADSR integrity preserved across failed play ──────────────────────────
static void testAdsrIntegrityPreservedAcrossFailedPlay()
{
    // A failed play that wrote gate-off and then crashed must not persist
    // the gate-off. ADSR from init must also survive the rollback.

    MockBridge bridge;
    bridge.regs[0x04] = 0x11u;  // gate=1, triangle
    bridge.regs[0x05] = 0x09u;  // attack=0, decay=9
    bridge.regs[0x06] = 0xF0u;  // sustain=15, release=0

    const BridgeTx tx = beginTx(bridge);
    bridge.write(0x04u, 0x10u, 500u);  // gate=0 (note off mid-play)
    bridge.write(0x00u, 0x00u, 510u);  // freq zeroed mid-play

    // Play crashed — rollback.
    rollbackTx(bridge, tx);

    assert(bridge.regs[0x04] == 0x11u);  // gate still on
    assert(bridge.regs[0x05] == 0x09u);  // ADSR intact
    assert(bridge.regs[0x06] == 0xF0u);  // sustain intact
    assert(bridge.timedWriteCount == 0u);
}

// ── main ──────────────────────────────────────────────────────────────────────
int main()
{
    testSuccessfulPlayCommits();
    testFailedPlayRollsBack();
    testRollbackZeroesStaleEntries();
    testCiaCommitWhenComplete();
    testCiaRollbackWhenIncomplete();
    testCiaNoRollbackWhenNotEntered();
    testMultiPlayBlockRollback();
    testRollbackWithCount();
    testBeginTxIsNonMutating();
    testAdsrIntegrityPreservedAcrossFailedPlay();
    return 0;
}
