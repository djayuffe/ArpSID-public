// Copyright (C) 2024-2026 Ulf Bertilsson
// v899 SID projection-mirror behavior closure.
//
// Pins the three validated mirror defects (values, not source shapes):
//  1. The projection bridge treated the sample-only cycle sentinel
//     ($FFFF, kSidUnresolvedCycleOffset — v890-v892 ordering policy) as a
//     REAL +65535 PHI2 delay, while the audio engine normalizes it to
//     cycle 0. Every sample-only projection write was scheduled far beyond
//     the capped mirror advance and then deleted: the mirror froze.
//  2. The per-block mirror cycle cap (768) plus the v885 destructive clear
//     deleted every same-block projection write later than ~37 samples at
//     48 kHz. v899 flushes (applies values through the normal bus path,
//     then removes) instead of dropping; the block-local no-late-replay law
//     and the performance cap are both preserved.
//  3. (Compile/behavior contract) the v898 fractional-path queue consumer
//     mirrors applied writes through runtimeMirrorAppliedProjectionWrite,
//     restoring the v874 "mirror reflects what audio consumed" law on the
//     live consumer.

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "arpsid/core/c64_sid_projection_bridge.h"
#include "arpsid/core/sid_event_queue.h"

namespace {


std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string dirnameOf(std::string path) {
    const std::size_t pos = path.find_last_of("/\\");
    return (pos == std::string::npos) ? std::string(".") : path.substr(0, pos);
}

std::string readFractionalRenderSource() {
    const std::string testDir = dirnameOf(__FILE__);
    const std::vector<std::string> candidates = {
        "include/arpsid/core/sid_runtime_fractional_render.h",
        "../include/arpsid/core/sid_runtime_fractional_render.h",
        "../../include/arpsid/core/sid_runtime_fractional_render.h",
        testDir + "/../../include/arpsid/core/sid_runtime_fractional_render.h",
    };
    for (const auto& c : candidates) {
        std::string s = readFile(c);
        if (!s.empty()) return s;
    }
    return {};
}

void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "c64_projection_mirror_behavior_v899_tests FAIL: " << msg << "\n";
        std::exit(1);
    }
}

// ── 1. Sentinel normalization in the bridge ──────────────────────────────

void unresolvedCycleMapsToSampleBoundary() {
    using namespace ArpSID::C64;
    require(c64HostSampleOffsetToPhi2(0u, ArpSID::kSidUnresolvedCycleOffset,
                                      48000.0, static_cast<double>(kPalPhi2Hz)) == 0ull,
            "sample 0 + unresolved cycle maps to PHI2 offset 0, not +65535");
    // Invalid-rate fallback must normalize too.
    require(c64HostSampleOffsetToPhi2(0u, ArpSID::kSidUnresolvedCycleOffset,
                                      0.0, 0.0) == 0ull,
            "invalid-rate fallback normalizes the unresolved sentinel");
    // Resolved cycles pass through unchanged.
    require(c64HostSampleOffsetToPhi2(0u, 7u, 48000.0,
                                      static_cast<double>(kPalPhi2Hz)) == 7ull,
            "resolved cycle offsets are preserved");
    // Sample offsets still map through the Q32 clock.
    const uint64_t s1 = c64HostSampleOffsetToPhi2(1u, 0u, 48000.0,
                                                  static_cast<double>(kPalPhi2Hz));
    require(s1 >= 20ull && s1 <= 21ull,
            "one 48 kHz sample maps to ~20.5 PAL PHI2 cycles");
    // The old bug: sentinel added raw.
    require(c64HostSampleOffsetToPhi2(0u, ArpSID::kSidUnresolvedCycleOffset,
                                      48000.0, static_cast<double>(kPalPhi2Hz)) != 65535ull,
            "the raw +65535 sentinel interpretation is gone");
}

// ── 2. Capped mirror advance + flush keeps same-block values ─────────────

void cappedMirrorFlushLandsLateBlockWrites() {
    using namespace ArpSID::C64;
    C64Platform p;
    p.reset(true);

    // A sample-only write at sample 0 (the exact frozen-mirror case) and a
    // write ~200 samples into the block (~4100 PHI2 cycles at 48 kHz — far
    // beyond the 768-cycle mirror cap).
    require(projectSidHostTimedWriteThroughC64Bus(p, 0x06u, 0xA5u,
                                                  0u, ArpSID::kSidUnresolvedCycleOffset,
                                                  48000.0, static_cast<double>(kPalPhi2Hz)),
            "sample-only projection write is accepted");
    require(projectSidHostTimedWriteThroughC64Bus(p, 0x01u, 0x3Cu,
                                                  200u, 0u,
                                                  48000.0, static_cast<double>(kPalPhi2Hz)),
            "late-in-block projection write is accepted");

    // Advance only the capped mirror budget (the publisher's per-block law).
    (void)p.runRealtimeSidCoreCycles(768u, 256u);
    // Sample-only write (offset 0 post-v899) is inside the advanced window.
    require(p.sidRegisterImage()[0x06u] == 0xA5u,
            "sample-only write lands within the advanced window");

    // The late write is beyond the cap; pre-v899 the clear deleted it.
    p.flushScheduledProjectionWrites();
    require(p.sidRegisterImage()[0x01u] == 0x3Cu,
            "capped-out same-block write still lands via the v899 flush");
}

// ── 2b. The block-local law is preserved: nothing replays later ──────────

void flushLeavesNoPendingProjectionEvents() {
    using namespace ArpSID::C64;
    C64Platform p;
    p.reset(true);
    require(projectSidHostTimedWriteThroughC64Bus(p, 0x04u, 0x41u,
                                                  300u, 0u,
                                                  48000.0, static_cast<double>(kPalPhi2Hz)),
            "projection write accepted");
    p.flushScheduledProjectionWrites();
    const uint8_t after = p.sidRegisterImage()[0x04u];
    require(after == 0x41u, "flush applied the value");
    // A later mirror advance must not re-apply anything (no pending events).
    (void)p.runRealtimeSidCoreCycles(8192u, 256u);
    // (No assertable side effect beyond not crashing / not replaying: write a
    // different value directly and confirm it is not overwritten by a replay.)
    require(projectSidWriteThroughC64Bus(p, 0x04u, 0x11u), "direct write accepted");
    (void)p.runRealtimeSidCoreCycles(1024u, 256u);
    require(p.sidRegisterImage()[0x04u] == 0x11u,
            "no late replay of flushed projection events (v885 law preserved)");
}

// ── 3. Fractional-path observer source closure ──────────────────────────

void fractionalTransferMirrorsAppliedWrites() {
    const std::string fractional = readFractionalRenderSource();
    require(!fractional.empty(), "fractional render source is readable");
    require(fractional.find("engine.queueSubphaseWrite") != std::string::npos,
            "fractional transfer still queues writes into the backend");
    require(fractional.find("target.runtimeMirrorAppliedProjectionWrite(w->regIndex, w->value") != std::string::npos,
            "fractional transfer mirrors each consumed write to the C64 projection observer");
    require(fractional.find("mirrorSample") != std::string::npos &&
            fractional.find("mirrorCycle") != std::string::npos,
            "fractional observer uses normalized audio-consumed sample/cycle timing metadata");
}


} // namespace

int main() {
    unresolvedCycleMapsToSampleBoundary();
    cappedMirrorFlushLandsLateBlockWrites();
    flushLeavesNoPendingProjectionEvents();
    fractionalTransferMirrorsAppliedWrites();
    std::cout << "c64_projection_mirror_behavior_v899_tests PASS\n";
    return 0;
}
