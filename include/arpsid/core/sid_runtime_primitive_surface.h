// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once
#include "sid_runtime_surface.h"
#include "sid_variant_profile.h"

namespace ArpSID {

class SidRuntimePrimitiveSurface : public SidRuntimeSurface {
public:
    ~SidRuntimePrimitiveSurface() override = default;
    virtual void setTransportPlaying(bool playing) noexcept = 0;
    virtual void setHostTempo(float bpm) noexcept = 0;
    virtual void rewindTransport() noexcept = 0;
    virtual void applyNormalizedParameter(uint32_t target, float value) noexcept = 0;
    virtual void panic() noexcept = 0;
    virtual void allNotesOff() noexcept = 0;
    // Channel-scoped variant: release only voices on `channel`.
    // Default delegates to the global allNotesOff() for backward compatibility
    // with implementations that do not override it.
    virtual void allNotesOffChannel(int channel) noexcept { (void)channel; allNotesOff(); }
    virtual void observeNoteActivity(uint8_t note, float velocity) noexcept = 0;
    virtual void triggerDrumMidi(uint8_t note, float velocity) noexcept = 0;
    virtual void releaseDrumMidi(uint8_t note) noexcept { (void)note; }
    virtual void arpNoteOn(uint8_t note, float velocity) noexcept = 0;
    virtual void arpNoteOff(uint8_t note) noexcept = 0;
    virtual void synthNoteOn(const SidTimedEvent& ev) noexcept = 0;
    virtual void synthNoteOff(const SidTimedEvent& ev) noexcept = 0;
    virtual void bitPerfectNoteOn(const SidTimedEvent& ev) noexcept = 0;
    // Optional token-stamped BitPerfect note-on. Canonical queue dispatch has
    // already applied noteOnCanonical() before engine dispatch, so the runtime
    // can resolve the canonical token and pass it here. Older/test surfaces can
    // ignore the token via the default compatibility fallback.
    virtual void bitPerfectNoteOnWithToken(const SidTimedEvent& ev, uint64_t voiceToken) noexcept { (void)voiceToken; bitPerfectNoteOn(ev); }
    virtual void bitPerfectNoteOff(const SidTimedEvent& ev) noexcept = 0;
    virtual void applyPitchBend(const SidTimedEvent& ev) noexcept = 0;
    virtual void applyPolyPressure(const SidTimedEvent& ev) noexcept = 0;
    virtual void applyChannelPressure(const SidTimedEvent& ev) noexcept = 0;
    virtual void applyMidiCC(const SidTimedEvent& ev) noexcept = 0;
    virtual void applyVariantProfile(const SidVariantProfile& profile) noexcept = 0;
    virtual void applyProgramChange(uint8_t program) noexcept = 0;
    virtual void renderSidRegister(float* left, float* right, int n) noexcept = 0;
    virtual void renderDrSid(float* left, float* right, int n) noexcept = 0;
    virtual void emitArpTimedEvents(int n) noexcept = 0;
    virtual void renderBitPerfect(float* left, float* right, int n) noexcept = 0;
    virtual void renderBitPerfectWithArp(float* left, float* right, int n) noexcept { emitArpTimedEvents(n); renderBitPerfect(left, right, n); }
};

} // namespace ArpSID
