// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// v745 - DIGI MIDI sample-pad must audition while the host transport is stopped.
//
// The on-screen DIGI pads (consumeGuiDigiPadTriggers_) are never gated by the
// host transport, so clicking a pad always plays. A MIDI keyboard must behave
// the same way: a NoteOn that lands on a DIGI slot has to fire the $D418 pad
// even when the host reports a known-stopped transport (suppressHostNoteOns).
//
// Before the fix, both kernel MIDI dispatch sites did `continue` on
// `suppressHostNoteOns` *before* calling maybeTriggerDigiFromMidi_, so a player
// could not audition recorded/kit DIGI samples from a MIDI keyboard unless the
// DAW was rolling. This is a source-level contract guard because the full AU
// kernel translation unit requires the Apple SDK to instantiate at runtime.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef ARPSID_SOURCE_DIR
#define ARPSID_SOURCE_DIR "."
#endif

static std::string readFile(const char* rel) {
    const std::string path = std::string(ARPSID_SOURCE_DIR) + "/" + rel;
    std::ifstream in(path, std::ios::binary);
    if (!in) std::abort();
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static void requireContains(const std::string& s, const char* needle) {
    if (s.find(needle) == std::string::npos) {
        std::cerr << "FAIL: missing source contract: " << needle << '\n';
        std::abort();
    }
}

// Counts occurrences so we can assert BOTH suppressed-note-on branches keep the
// DIGI pad route live, not just one of the two MIDI dispatch sites.
static std::size_t countOccurrences(const std::string& s, const std::string& needle) {
    std::size_t n = 0, pos = 0;
    while ((pos = s.find(needle, pos)) != std::string::npos) { ++n; pos += needle.size(); }
    return n;
}

int main() {
    const std::string kernel = readFile("source/au3/ArpSIDDSPKernel.hpp");

    // The suppression policy itself must still exist (synth/arp notes are still
    // gated when the transport is known-stopped).
    requireContains(kernel, "suppressHostNoteOns");

    // v910 single ingress authority: both MIDI dispatch sites (midiQueue_ ring
    // drain and host events[] loop) now route through ONE shared helper,
    // dispatchCanonicalIngressEvent_, so the suppression carve-out and the
    // DIGI pad route can never diverge between paths. The helper keeps the
    // guarded suppressed-NoteOn branch with the SID808 live-pad exemption.
    requireContains(kernel, "bool sid808LivePadNote_(const TimedEvent& ev) const noexcept");
    requireContains(kernel, "void dispatchCanonicalIngressEvent_(const TimedEvent& te");
    if (countOccurrences(kernel, "!sid808LivePadNote_(te)") < 1u) {
        std::cerr << "FAIL: shared ingress helper must keep the SID808 live-pad exemption\n";
        std::abort();
    }
    // Both ingress paths must actually call the shared helper (definition + 2 call sites).
    if (countOccurrences(kernel, "dispatchCanonicalIngressEvent_(") < 3u) {
        std::cerr << "FAIL: both the midiQueue_ drain and the events[] loop must "
                     "dispatch through dispatchCanonicalIngressEvent_\n";
        std::abort();
    }

    // The DIGI pad route must fire even while transport playback is gated.
    requireContains(kernel,
        "audition from a MIDI keyboard even while the host transport");
    requireContains(kernel,
        "mirrors the ungated on-screen DIGI pads");

    // Sanity: the shared helper triggers DIGI on both the suppressed and the
    // normal branch (suppression-only would break live pads; live-only would
    // break stopped-transport audition).
    if (countOccurrences(kernel, "maybeTriggerDigiFromMidi_(canonical, numFrames);") < 2u) {
        std::cerr << "FAIL: DIGI MIDI trigger must fire on both suppressed and "
                     "non-suppressed branches of the shared ingress helper\n";
        std::abort();
    }

    std::cout << "DigiMidiPadTransportStopV745Tests PASS\n";
    return 0;
}
