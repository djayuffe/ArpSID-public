// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// ingress_noteoff_safety_v692_tests.cpp
//
// Concurrency audit #2: under GENUINE ingress fullness (merge lanes + freelist +
// spill all saturated) MIDI events reach captureIngressFallback_. NoteOn/NoteOff
// used to fall through to a silent drop — a lost NoteOff leaves a STUCK note.
// The fix: a dropped NoteOff forces an all-notes-off stuck-note safety and is
// counted; a dropped NoteOn is counted (observable, benign). This test floods the
// ingress without draining and asserts the per-type drop counters move.
//
// Also exercises audit #1's source contract via the runtime header (the ring's
// semantically-atomic clear is covered behaviorally in bounded_mpsc_ring_v688).

#include "arpsid/core/sid_runtime_model.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

namespace {
int g_failures = 0;
void require(bool cond, const char* msg) {
    if (!cond) { std::fprintf(stderr, "FAIL: %s\n", msg); ++g_failures; }
}
std::string readFile(const std::string& rel) {
    std::ifstream f(std::string(ARPSID_SOURCE_ROOT) + "/" + rel, std::ios::binary);
    std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}

ArpSID::SidTimedEvent makeNote(ArpSID::SidTimedEventType type, uint8_t ch, uint8_t pitch) {
    ArpSID::SidTimedEvent ev{};
    ev.type = type;
    ev.channel = ch;
    ev.pitch = pitch;
    ev.value = (type == ArpSID::SidTimedEventType::MidiNoteOn) ? 1.0f : 0.0f;
    ev.value_f32 = ev.value;
    return ev;
}
} // namespace

int main() {
    using namespace ArpSID;

    // ── behavioral: flood the ingress to genuine fullness, never draining ────
    {
        SidRuntimeModel model;
        // Far more than (merge-lane + freelist + spill) capacity, so events spill
        // into captureIngressFallback_ where the audit #2 safety/counting lives.
        for (int i = 0; i < 300000; ++i)
            (void)model.pushToLane(makeNote(SidTimedEventType::MidiNoteOff, 0u, 60u),
                                   SidIngressSourcePriority::MidiNoteControl, 64);
        require(model.ingressDroppedNoteOffCount() > 0u,
                "a NoteOff dropped under genuine fullness is counted (audit #2 stuck-note safety path)");

        for (int i = 0; i < 300000; ++i)
            (void)model.pushToLane(makeNote(SidTimedEventType::MidiNoteOn, 0u, 64u),
                                   SidIngressSourcePriority::MidiNoteControl, 64);
        require(model.ingressDroppedNoteOnCount() > 0u,
                "a NoteOn lost under genuine fullness is counted (audit #2: not silent)");
    }

    // ── source contract: the #2 safety wiring is present ─────────────────────
    {
        const std::string rt = readFile("include/arpsid/core/sid_runtime_model.h");
        require(rt.find("case SidTimedEventType::MidiNoteOff:") != std::string::npos &&
                rt.find("ingress_dropped_note_off_.fetch_add") != std::string::npos,
                "captureIngressFallback_ handles a dropped NoteOff (stuck-note safety + count)");
        require(rt.find("ingress_fallback_all_notes_off_[(size_t)(ev.channel & 0x0Fu)].store(1u, std::memory_order_release)") != std::string::npos,
                "a dropped NoteOff forces an all-notes-off stuck-note safety");
        require(rt.find("ingress_dropped_note_on_.fetch_add") != std::string::npos,
                "a dropped NoteOn is counted (observable loss)");

        const std::string ring = readFile("source/arpsid_bounded_mpsc_ring.h");
        require(ring.find("size_t clearEnqueuedBeforeNow() noexcept") != std::string::npos,
                "audit #1: ring exposes the semantically-atomic clearEnqueuedBeforeNow()");
        const std::string kernel = readFile("source/au3/ArpSIDDSPKernel.hpp");
        require(kernel.find("ring.clearEnqueuedBeforeNow()") != std::string::npos,
                "audit #1: factory/root ingress clear uses the semantically-atomic clear");
    }

    if (g_failures == 0) {
        std::printf("ingress_noteoff_safety_v692_tests: PASS (audit #1 atomic clear + #2 NoteOn/NoteOff drop safety)\n");
        return 0;
    }
    std::fprintf(stderr, "ingress_noteoff_safety_v692_tests: %d FAILURE(S)\n", g_failures);
    return 1;
}
