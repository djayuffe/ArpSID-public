// c64_psid_sidcore_timeline_v580_tests.cpp
// v580: Keep SIDCORE timeline and live shadow coherent for the C64/PSID
// early-return render path.
//
// ROOT CAUSE:
// C64/PSID rendering returned from processBlock before the normal v550
// SIDCORE end-of-block path ran. Register-write events were published with
// sidCoreBlockIndex_ * 512 + sampleOffset, but sidCoreBlockIndex_ never
// advanced for C64 blocks. Even outside C64, the hard-coded 512-sample stride
// made timestamps non-monotonic for 1024/4096 frame host blocks.
//
// FIX:
// Use a render-thread sample cursor for SIDCORE stamps, finish the SIDCORE
// block timeline before the active C64 early return, and mirror C64 handoff
// seeds/timed writes into sidQueuedShadow_ so live SIDCORE snapshots match
// the C64 audio authority.

#include <array>
#include <cassert>
#include <cstdint>
#include <algorithm>

static constexpr uint8_t kSidRegCount = 32u;
static constexpr uint8_t kSidReadOnlyLo = 0x19u;
static constexpr uint8_t kSidReadOnlyHi = 0x1Cu;
static constexpr uint8_t kV1FreqLo = 0x00u;
static constexpr uint8_t kV1FreqHi = 0x01u;
static constexpr uint8_t kV1Control = 0x04u;
static constexpr uint8_t kV1AttackDecay = 0x05u;
static constexpr uint8_t kV1SustainRel = 0x06u;
static constexpr uint8_t kVolume = 0x18u;

static bool sidRegWriteable(uint8_t reg) noexcept {
    reg = static_cast<uint8_t>(reg & 0x1Fu);
    return reg <= 0x18u;
}

struct SidCoreTimeline {
    uint64_t blockIndex = 0u;
    uint64_t sampleCursor = 0u;
    uint64_t blockSampleBase = 0u;
    uint32_t snapshots = 0u;

    void begin(int) noexcept {
        blockSampleBase = sampleCursor;
    }

    uint64_t stamp(int sampleOffset) const noexcept {
        const uint64_t local = sampleOffset > 0 ? static_cast<uint64_t>(sampleOffset) : 0u;
        return local > UINT64_MAX - blockSampleBase ? UINT64_MAX : blockSampleBase + local;
    }

    void finish(int numFrames, bool publishSnapshot = true) noexcept {
        ++blockIndex;
        const uint64_t frames = numFrames > 0 ? static_cast<uint64_t>(numFrames) : 0u;
        sampleCursor = frames > UINT64_MAX - sampleCursor ? UINT64_MAX : sampleCursor + frames;
        if (publishSnapshot) ++snapshots;
    }
};

struct Shadow {
    std::array<uint8_t, kSidRegCount> value{};
    std::array<uint8_t, kSidRegCount> valid{};
    std::array<uint16_t, kSidRegCount> sample{};
    std::array<uint16_t, kSidRegCount> cycle{};
};

static void mirrorShadowWrite(Shadow& s,
                              uint8_t reg,
                              uint8_t value,
                              uint16_t sampleOffset,
                              uint16_t cycleOffset) noexcept {
    const size_t idx = static_cast<size_t>(reg);
    if (idx >= s.value.size()) return;
    s.value[idx] = value;
    s.valid[idx] = 1u;
    s.sample[idx] = sampleOffset;
    s.cycle[idx] = cycleOffset;
}

static uint8_t gateFlagsFromPreviousShadow(const Shadow& s, uint8_t reg, uint8_t value) noexcept {
    if (reg != kV1Control) return 0u;
    const bool prevValid = s.valid[reg] != 0u;
    const uint8_t prevCtrl = prevValid ? s.value[reg] : 0u;
    const bool prevGate = (prevCtrl & 0x01u) != 0u;
    const bool newGate = (value & 0x01u) != 0u;
    uint8_t flags = 0u;
    if (newGate && !prevGate) flags |= 0x01u;
    if (!newGate && prevGate) flags |= 0x02u;
    return flags;
}

struct Snapshot {
    uint16_t voiceFrequency = 0u;
    uint8_t voiceWaveform = 0u;
    uint8_t voiceAD = 0u;
    uint8_t voiceSR = 0u;
    uint8_t filterModeVolume = 0u;
};

static Snapshot buildSnapshot(const Shadow& s) noexcept {
    Snapshot snap{};
    snap.voiceFrequency = static_cast<uint16_t>(s.value[kV1FreqLo] |
                                                (static_cast<uint16_t>(s.value[kV1FreqHi]) << 8));
    snap.voiceWaveform = s.value[kV1Control];
    snap.voiceAD = s.value[kV1AttackDecay];
    snap.voiceSR = s.value[kV1SustainRel];
    snap.filterModeVolume = s.value[kVolume];
    return snap;
}

static void seedFromInitImage(Shadow& s, const std::array<uint8_t, kSidRegCount>& initRegs) noexcept {
    for (uint8_t r = 0u; r < kSidRegCount; ++r) {
        if (sidRegWriteable(r)) {
            mirrorShadowWrite(s, r, initRegs[r], 0u, 0u);
        }
    }
}

static void testOldC64EarlyReturnRepeatsTimestamps()
{
    // Pre-v580 C64 returned before ++sidCoreBlockIndex_, so every block used
    // base 0 and repeated the same local sample window.
    uint64_t oldBlockIndex = 0u;
    const uint64_t block0Stamp = oldBlockIndex * 512u + 128u;
    // Early return: oldBlockIndex is still 0.
    const uint64_t block1Stamp = oldBlockIndex * 512u + 128u;
    assert(block0Stamp == 128u);
    assert(block1Stamp == block0Stamp);
}

static void testV580C64EarlyReturnAdvancesTimeline()
{
    SidCoreTimeline t;
    t.begin(512);
    const uint64_t block0Stamp = t.stamp(128);
    t.finish(512);

    t.begin(512);
    const uint64_t block1Stamp = t.stamp(128);
    t.finish(512);

    assert(block0Stamp == 128u);
    assert(block1Stamp == 640u);
    assert(block1Stamp > block0Stamp);
    assert(t.blockIndex == 2u);
    assert(t.sampleCursor == 1024u);
    assert(t.snapshots == 2u);
}

static void testVariableBlockSizesRemainMonotonic()
{
    // The old fixed 512 stride regressed when a 1024-frame block was followed
    // by any later block: 900 then 512 is backwards.
    const uint64_t oldBlock0 = 0u * 512u + 900u;
    const uint64_t oldBlock1 = 1u * 512u + 4u;
    assert(oldBlock1 < oldBlock0);

    SidCoreTimeline t;
    t.begin(1024);
    const uint64_t a = t.stamp(900);
    t.finish(1024);
    t.begin(64);
    const uint64_t b = t.stamp(4);
    t.finish(64);
    t.begin(4096);
    const uint64_t c = t.stamp(4095);
    t.finish(4096);

    assert(a == 900u);
    assert(b == 1028u);
    assert(c == 5183u);
    assert(a < b && b < c);
    assert(t.sampleCursor == 5184u);
}

static void testIdlePathAdvancesCursorWithoutSnapshot()
{
    SidCoreTimeline t;
    t.begin(512);
    t.finish(512, false);
    assert(t.blockIndex == 1u);
    assert(t.sampleCursor == 512u);
    assert(t.snapshots == 0u);

    t.begin(512);
    assert(t.stamp(0) == 512u);
}

static void testHandoffSeedsSidCoreShadow()
{
    Shadow s;
    s.value.fill(0xAAu);

    std::array<uint8_t, kSidRegCount> init{};
    init.fill(0u);
    init[kV1FreqLo] = 0x71u;
    init[kV1FreqHi] = 0x0Du;
    init[kV1AttackDecay] = 0x09u;
    init[kV1SustainRel] = 0xF0u;
    init[kVolume] = 0x0Fu;
    init[0x19u] = 0x55u; // read-only window must not be mirrored

    seedFromInitImage(s, init);

    assert(s.value[kV1FreqLo] == 0x71u);
    assert(s.value[kV1FreqHi] == 0x0Du);
    assert(s.value[kV1AttackDecay] == 0x09u);
    assert(s.value[kV1SustainRel] == 0xF0u);
    assert(s.value[kVolume] == 0x0Fu);
    assert(s.valid[kV1SustainRel] == 1u);
    assert(s.value[0x19u] == 0xAAu);
    assert(s.valid[0x19u] == 0u);
}

static void testTimedWritesPublishFlagsBeforeShadowUpdate()
{
    Shadow s;
    mirrorShadowWrite(s, kV1Control, 0x10u, 0u, 0u); // waveform, gate off

    const uint8_t gateOnFlags = gateFlagsFromPreviousShadow(s, kV1Control, 0x11u);
    mirrorShadowWrite(s, kV1Control, 0x11u, 5u, 123u);
    assert((gateOnFlags & 0x01u) != 0u);
    assert((gateOnFlags & 0x02u) == 0u);
    assert(s.value[kV1Control] == 0x11u);
    assert(s.sample[kV1Control] == 5u);
    assert(s.cycle[kV1Control] == 123u);

    const uint8_t gateOffFlags = gateFlagsFromPreviousShadow(s, kV1Control, 0x10u);
    mirrorShadowWrite(s, kV1Control, 0x10u, 9u, 456u);
    assert((gateOffFlags & 0x02u) != 0u);
    assert((gateOffFlags & 0x01u) == 0u);
}

static void testCycleOffsetClampsForShadowProvenance()
{
    Shadow s;
    const uint64_t largeCycle = 70000u;
    const uint16_t clamped = static_cast<uint16_t>(std::min<uint64_t>(largeCycle, 0xFFFFull));
    mirrorShadowWrite(s, kV1FreqLo, 0x44u, 17u, clamped);
    assert(s.sample[kV1FreqLo] == 17u);
    assert(s.cycle[kV1FreqLo] == 0xFFFFu);
}

static void testSnapshotReflectsC64Shadow()
{
    Shadow s;
    std::array<uint8_t, kSidRegCount> init{};
    init[kV1FreqLo] = 0x34u;
    init[kV1FreqHi] = 0x12u;
    init[kV1AttackDecay] = 0x09u;
    init[kV1SustainRel] = 0xF0u;
    init[kVolume] = 0x0Fu;
    seedFromInitImage(s, init);

    // First play call overwrites per-frame registers; ADSR and volume remain
    // the init-seeded authority in the live shadow.
    mirrorShadowWrite(s, kV1FreqLo, 0x78u, 0u, 0u);
    mirrorShadowWrite(s, kV1FreqHi, 0x56u, 0u, 0u);
    mirrorShadowWrite(s, kV1Control, 0x11u, 0u, 0u);

    const Snapshot snap = buildSnapshot(s);
    assert(snap.voiceFrequency == 0x5678u);
    assert(snap.voiceWaveform == 0x11u);
    assert(snap.voiceAD == 0x09u);
    assert(snap.voiceSR == 0xF0u);
    assert(snap.filterModeVolume == 0x0Fu);
}

static void testTimelineSaturatesInsteadOfWrapping()
{
    SidCoreTimeline t;
    t.sampleCursor = UINT64_MAX - 8u;
    t.begin(32);
    assert(t.stamp(4) == UINT64_MAX - 4u);
    assert(t.stamp(16) == UINT64_MAX);
    t.finish(32);
    assert(t.sampleCursor == UINT64_MAX);
}

int main()
{
    testOldC64EarlyReturnRepeatsTimestamps();
    testV580C64EarlyReturnAdvancesTimeline();
    testVariableBlockSizesRemainMonotonic();
    testIdlePathAdvancesCursorWithoutSnapshot();
    testHandoffSeedsSidCoreShadow();
    testTimedWritesPublishFlagsBeforeShadowUpdate();
    testCycleOffsetClampsForShadowProvenance();
    testSnapshotReflectsC64Shadow();
    testTimelineSaturatesInsteadOfWrapping();
    return 0;
}
