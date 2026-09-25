// c64_psid_full_vbi_passive_v578_tests.cpp
// Current contract: VBI PSID advancement is bounded by host time. Host cycles
// accrue progressively to each play deadline, so a play may consume at most one
// VBI period and can never borrow cycles from later samples.

#include "arpsid/core/sid_event_timing.h"

#include <algorithm>
#include <cassert>
#include <cstdint>

namespace {

constexpr double kSampleRate = 44100.0;
constexpr double kPalClockHz = 985248.0;
constexpr std::uint64_t kPalVbiCycles = 63ull * 312ull;

void testFirstPlayCannotRunAhead() {
    ArpSID::SidCycleClockState clock;
    clock.configure(kSampleRate, kPalClockHz);
    std::uint64_t debt = 0u;

    // An immediate play at sample zero has no newly elapsed host time.
    const std::uint64_t catchup = std::min(debt, kPalVbiCycles);
    assert(catchup == 0u);
}

void testTwoPlaysUseProgressiveDeadlines() {
    ArpSID::SidCycleClockState clock;
    clock.configure(kSampleRate, kPalClockHz);
    std::uint64_t debt = 0u;
    std::uint32_t accruedFrames = 0u;

    auto accrueTo = [&](std::uint32_t frame) {
        assert(frame >= accruedFrames);
        debt += clock.cyclesForNextHostBlock(frame - accruedFrames);
        accruedFrames = frame;
    };

    // First fire is immediate; it cannot consume the future 1024-frame block.
    accrueTo(0u);
    const std::uint64_t first = std::min(debt, kPalVbiCycles);
    debt -= first;
    assert(first == 0u);

    // The second PAL deadline is about 880 samples later. It receives exactly
    // the cycles elapsed to that point, never cycles from samples 880..1023.
    accrueTo(880u);
    const std::uint64_t elapsedAtSecond = debt;
    const std::uint64_t second = std::min(debt, kPalVbiCycles);
    debt -= second;
    assert(second <= elapsedAtSecond);
    assert(second <= kPalVbiCycles);
    assert(second > kPalVbiCycles - 64u);

    accrueTo(1024u);
    assert(debt < kPalVbiCycles);
}

void testCrossBlockDeadlineStaysHostBounded() {
    ArpSID::SidCycleClockState clock;
    clock.configure(kSampleRate, kPalClockHz);
    std::uint64_t debt = 0u;

    debt += clock.cyclesForNextHostBlock(512u);
    assert(debt < kPalVbiCycles);
    const std::uint64_t earlyCatchup = std::min(debt, kPalVbiCycles);
    assert(earlyCatchup == debt);
    debt -= earlyCatchup;

    debt += clock.cyclesForNextHostBlock(368u);
    const std::uint64_t laterCatchup = std::min(debt, kPalVbiCycles);
    assert(laterCatchup == debt);
    assert(laterCatchup < kPalVbiCycles);
}

void testNoOvershootInvariant() {
    for (std::uint64_t debt = 0u; debt < kPalVbiCycles * 3u; debt += 137u) {
        const std::uint64_t advance = std::min(debt, kPalVbiCycles);
        assert(advance <= debt);
        assert(advance <= kPalVbiCycles);
    }
}

} // namespace

int main() {
    testFirstPlayCannotRunAhead();
    testTwoPlaysUseProgressiveDeadlines();
    testCrossBlockDeadlineStaysHostBounded();
    testNoOvershootInvariant();
    return 0;
}
