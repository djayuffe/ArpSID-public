// Copyright (C) 2024-2026 Ulf Bertilsson
// synthmode_held_replay_identity_v869_tests.cpp
//
// v869 guards the AU/Logic SynthMode stuck-note fix:
// - raw MIDI held replay gets negative synthetic anonymous note IDs, not
//   positive host-note IDs;
// - anonymous NoteOff can release those replayed notes;
// - anonymous NoteOff still cannot steal real host-noteId voices;
// - SynthMode orphan reconciliation removes the stale held ledger entry.

#include "arpsid/core/sid_runtime_host_surface.h"
#include "arpsid/core/sid_runtime_voice_policy.h"
#include "arpsid/engines/voice_manager.h"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "synthmode_held_replay_identity_v869_tests FAIL: "
                  << message << "\n";
        std::exit(1);
    }
}

std::string readSourceFile(const char* relativePath) {
#ifndef ARPSID_SOURCE_DIR
#define ARPSID_SOURCE_DIR "."
#endif
    std::ifstream in(std::string(ARPSID_SOURCE_DIR) + "/" + relativePath);
    require(in.good(), "source file must be readable");
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool contains(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

void testSyntheticAnonymousIdsStayNegative() {
    using ArpSID::SidRuntimeHostSurface;
    const std::uint32_t serials[] = {
        0u, 1u, 2u, 0x7FFFFFFEu, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu
    };
    for (std::uint32_t serial : serials) {
        const std::int32_t id = SidRuntimeHostSurface::makeSyntheticAnonymousNoteId(serial);
        require(id < -1, "synthetic replay identity must remain in anonymous negative domain");
        require(SidRuntimeHostSurface::isSyntheticAnonymousNoteId(id),
                "synthetic replay identity helper must recognize its IDs");
    }
}

void testAnonymousReleaseCompatibility() {
    using namespace ArpSID;
    VoiceManager vm;
    const int32_t syntheticId = SidRuntimeHostSurface::makeSyntheticAnonymousNoteId(123u);
    const auto synthetic = vm.noteOnWithToken(64, 1.0f, 0, syntheticId, 111u, [](int){});
    require(synthetic.voiceIndex >= 0, "synthetic anonymous note must allocate a voice");
    const auto off = vm.noteOffDetailedResult(64, 0, -1);
    require(off.voiceIndex == synthetic.voiceIndex,
            "anonymous NoteOff must release negative synthetic anonymous replay voice");

    VoiceManager guarded;
    const auto real = guarded.noteOnWithToken(64, 1.0f, 0, 777, 222u, [](int){});
    require(real.voiceIndex >= 0, "real host-id note must allocate a voice");
    const auto anonOff = guarded.noteOffDetailedResult(64, 0, -1);
    require(anonOff.voiceIndex < 0,
            "anonymous NoteOff must not steal a positive real host-noteId voice");
    require(guarded.getVoiceState(real.voiceIndex).keyDown,
            "positive real host-noteId voice remains held after anonymous NoteOff");
}

void testVoiceAllocatorReconcileCleansHeldLedger() {
    using namespace ArpSID;
    VoiceAllocator alloc;
    alloc.setPlayMode(PlayMode::Mono);
    VoiceEventBuffer events{};
    const int32_t syntheticId = SidRuntimeHostSurface::makeSyntheticAnonymousNoteId(456u);
    alloc.noteOn(60, 1.0f, 0, syntheticId, events);
    require(alloc.priorityHeldNote() != nullptr, "note-on must create a held-ledger entry");

    int gated = 0;
    auto held = [](int, int) noexcept { return false; };
    auto pedal = [](int) noexcept { return false; };
    auto gate = [&](int) noexcept { ++gated; };
    require(alloc.reconcileUnheldVoices(held, pedal, gate) == 0,
            "first orphan reconcile pass is grace-only");
    require(alloc.priorityHeldNote() != nullptr,
            "grace pass must not clear the held-ledger entry");
    require(alloc.reconcileUnheldVoices(held, pedal, gate) == 1,
            "second orphan reconcile pass releases SynthMode voice");
    require(gated == 1, "reconcile must emit one gate-off callback");
    require(alloc.priorityHeldNote() == nullptr,
            "reconcile must remove stale SynthMode held-ledger entry");
}

void testSourceGuards() {
    const std::string hostSurface = readSourceFile("include/arpsid/core/sid_runtime_host_surface.h");
    const std::string auKernel = readSourceFile("source/au3/ArpSIDDSPKernel.hpp");
    const std::string voicePolicy = readSourceFile("include/arpsid/core/sid_runtime_voice_policy.h");
    const std::string phase2 = readSourceFile("source/arpsid_processor_phase2.cpp");

    require(contains(hostSurface, "makeSyntheticAnonymousNoteId"),
            "host surface must own synthetic anonymous note-id helper");
    require(!contains(auKernel, "store(static_cast<int32_t>(serial & 0x7FFFFFFFu)"),
            "AU held replay must not store positive serial-derived host note IDs");
    require(contains(auKernel, "makeSyntheticAnonymousNoteId(serial)"),
            "AU held replay must use negative synthetic anonymous IDs");
    require(contains(voicePolicy, "reconcileUnheldVoices"),
            "VoiceAllocator must expose orphan reconciliation with held-ledger cleanup");
    require(contains(auKernel, "reconcileSynthModeUnheldVoices_();"),
            "AU render block must run SynthMode orphan reconciliation");
    require(contains(phase2, "mirrorVstMidiHeldIngress_"),
            "VST/Phase2 MIDI path must maintain held mirror before reconciling");
    require(contains(phase2, "reconcileSynthModeUnheldVoices_();"),
            "VST/Phase2 render block must run SynthMode orphan reconciliation");
}

} // namespace

int main() {
    testSyntheticAnonymousIdsStayNegative();
    testAnonymousReleaseCompatibility();
    testVoiceAllocatorReconcileCleansHeldLedger();
    testSourceGuards();
    std::cout << "synthmode_held_replay_identity_v869_tests PASS\n";
    return 0;
}
