// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once

#include "sid_runtime_host_policy.h"
#include "sid_runtime_host_surface.h"
#include "sid_runtime_model.h"
#include "host_sample_rate.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ArpSID {

struct SidCanonicalHostBlockState final {
    bool wasPlaying = false;
    bool isPlaying = false;
    // v910 live-play authority split. A stopped transport may gate
    // transport-driven playback (internal sequencer/arrangement provenance),
    // but it must NEVER gate live/manual MIDI instrument input: hosts deliver
    // keyboard-thru NoteOns through the same event path while stopped, and a
    // Classic/Synth/BitPerfect instrument that only sounds when the DAW is
    // rolling is broken as an instrument.
    //  - suppressTransportSequencerNoteOns: the old law (playStateKnown &&
    //    !playing); consumers may apply it only to events proven to be
    //    transport-driven playback.
    //  - suppressLiveInstrumentNoteOns: constitutionally false — live input
    //    always plays.
    //  - suppressHostNoteOns: DEPRECATED alias of
    //    suppressTransportSequencerNoteOns kept for existing telemetry/tests;
    //    it must not be used to gate host-delivered note traffic.
    bool suppressHostNoteOns = false;
    bool suppressTransportSequencerNoteOns = false;
    bool suppressLiveInstrumentNoteOns = false;
    double previousBeatPosition = 0.0;
    double currentBeatPosition = 0.0;
    double sampleRate = 44100.0;
    float tempoBpm = 120.0f;
};

inline double sidSanitizeHostBlockSampleRate(double requested, double fallback) noexcept {
    const double f = canonicalizeHostSampleRate(fallback);
    if (hostSampleRateLooksBogusOrMismatched(requested, f)) return f;
    return canonicalizeHostSampleRate(requested, f);
}

inline float sidSanitizeHostBlockTempo(double bpm, double fallback) noexcept {
    const double f = (std::isfinite(fallback) && fallback > 0.0) ? fallback : 120.0;
    const double v = (std::isfinite(bpm) && bpm > 0.0) ? bpm : f;
    return std::clamp(static_cast<float>(v), 1.0f, 400.0f);
}

template <class TransportLike>
inline SidCanonicalHostBlockState sidRuntimeBeginCanonicalHostBlock(SidRuntimeModel& runtime,
                                                                    SidRuntimeHostSurface& host,
                                                                    const TransportLike& transport,
                                                                    double currentSampleRate,
                                                                    int frameCount,
                                                                    int sampleOffset = 0) noexcept {
    SidCanonicalHostBlockState s{};
    s.wasPlaying = host.lastTransportPlaying;
    s.previousBeatPosition = host.hostBeatPosition;
    s.sampleRate = sidSanitizeHostBlockSampleRate(transport.sampleRate, currentSampleRate);

    if (transport.valid()) {
        host.hostTempo = sidSanitizeHostBlockTempo(transport.bpm, host.hostTempo);
        // Only adopt the host's isPlaying when we actually know the play state.
        // A failed/default-constructed transport snapshot reports playStateKnown=false;
        // overwriting host.transportPlaying with the default `false` in that case
        // would synthesize a spurious stop edge every render block where the host
        // callback was unavailable, killing notes and rewinding the sequencer.
        if (transport.playStateKnown) {
            host.transportPlaying = transport.isPlaying;
        }
        if (std::isfinite(transport.beatPosition) && transport.beatPosition >= 0.0) {
            const double publishedBeat = transport.beatPosition;
            const bool snapshotIsStale =
                host.transportPlaying &&
                host.hasPublishedBeatPosition &&
                std::fabs(publishedBeat - host.lastPublishedBeatPosition) < 1.0e-9;
            if (!snapshotIsStale) host.hostBeatPosition = publishedBeat;
            host.lastPublishedBeatPosition = publishedBeat;
            host.hasPublishedBeatPosition = true;
        }
        runtime.setHostProjectTimePPQ(host.hostBeatPosition);
    }

    s.tempoBpm = sidSanitizeHostBlockTempo(host.hostTempo, 120.0);
    s.isPlaying = host.transportPlaying;
    s.currentBeatPosition = host.hostBeatPosition;
    s.suppressTransportSequencerNoteOns = transport.playStateKnown && !host.transportPlaying;
    s.suppressHostNoteOns = s.suppressTransportSequencerNoteOns; // deprecated alias — see struct comment
    s.suppressLiveInstrumentNoteOns = false; // live/manual MIDI always plays, transport state is irrelevant

    sidCanonicalPushTransportTempoIfChanged(runtime, s.isPlaying, s.tempoBpm, frameCount, sampleOffset);
    return s;
}

inline void sidRuntimeCommitCanonicalHostBlock(SidRuntimeHostSurface& host,
                                               const SidCanonicalHostBlockState& state) noexcept {
    host.lastTransportPlaying = state.isPlaying;
}

inline void sidRuntimeAdvanceCanonicalHostBlock(SidRuntimeHostSurface& host,
                                                const SidCanonicalHostBlockState& state,
                                                int frameCount) noexcept {
    sidRuntimeCommitCanonicalHostBlock(host, state);
    if (!state.isPlaying || frameCount <= 0 || state.sampleRate <= 0.0 || state.tempoBpm <= 0.0f) return;
    const double delta = static_cast<double>(frameCount) *
                         static_cast<double>(state.tempoBpm) /
                         (state.sampleRate * 60.0);
    const double advanced = host.hostBeatPosition + delta;
    host.hostBeatPosition = std::isfinite(advanced)
        ? advanced
        : std::numeric_limits<double>::max();
}

} // namespace ArpSID
