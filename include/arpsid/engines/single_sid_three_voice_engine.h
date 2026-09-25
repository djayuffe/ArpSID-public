// SPDX-License-Identifier: BSD-3-Clause
// single_sid_three_voice_engine.h — Authentic single-SID 3-voice C64 engine
// (Audit #29 — architectural answer).
//
// PROBLEM
// ------// `BitPerfectEngine` instantiates N independent `SIDChip` instances (one
// per polyphony voice). That gives a synth-like polyphony model but is
// NOT what a real C64 sounds like: a real C64 has ONE SID with THREE
// voices, and polyphony beyond three is achieved by voice stealing on
// that single chip.
//
// This engine is the parallel, audit-correct path: ONE `SIDChip` instance
// driven by a `SidVoiceAllocator` that maps incoming MIDI notes onto the
// chip's three voices with deterministic stealing.
//
// USAGE
// ----// The engine is independent of `BitPerfectEngine` — it can be instantiated
// by host code, kit definitions, or factory presets that explicitly want
// authentic single-SID behavior. The existing per-voice-chip path stays
// in place for users who want synth polyphony.
//
// SingleSidThreeVoiceEngine engine;
// engine.prepare(48000.0); // host SR
// engine.setClockFrequency(PAL_CLOCK_FREQ);
// engine.noteOn(60, 100); // C4 onto voice 0
// engine.noteOn(64, 100); // E4 onto voice 1
// engine.noteOn(67, 100); // G4 onto voice 2 (chord)
// engine.noteOn(72, 100); // C5 — must steal one voice
//
// for (int i = 0; i < numSamples; ++i) {
// float l, r;
// engine.processSample(l, r);
// out[i] = (l + r) * 0.5f;
// }
//
// CONTRACT
// -------// * Exactly ONE `SIDChip` instance; never more.
// * The chip's 3 hardware voices are always the allocation target. The
// allocator policy decides what happens at the 4th+ simultaneous note.
// * `processSample` is called once per output sample — single chip, no
// mixing across N chips, no per-voice oversampling beyond what the
// chip itself does.
// * `noteOn` with `Drsid::ChokeGroup::HiHat` will choke any prior HiHat
// voice on the same chip (audit §4C non-negotiable for drums).
// * RT-safe: no allocations after `prepare()`.

#ifndef ARPSID_ENGINES_SINGLE_SID_THREE_VOICE_ENGINE_H
#define ARPSID_ENGINES_SINGLE_SID_THREE_VOICE_ENGINE_H

#include "arpsid/core/sid_chip.h"
#include "arpsid/core/sid_voice_allocator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace ArpSID {

class SingleSidThreeVoiceEngine {
public:
    SingleSidThreeVoiceEngine() noexcept {
        chip_.setSampleRate(sampleRate_);
        chip_.setClockFrequency(clockFreq_);
        chip_.setMasterVolume(15);
    }

    // ── Setup (non-RT) ──────────────────────────────────────────────────────
    void prepare(double sampleRate) noexcept {
        sampleRate_ = std::isfinite(sampleRate) ? std::max(1.0, sampleRate) : 44100.0;
        chip_.setSampleRate(sampleRate_);
        allNotesOff();
    }

    void setClockFrequency(double hz) noexcept {
        if (!sidClockFrequencySupported(hz)) return;
        clockFreq_ = hz;
        chip_.setClockFrequency(clockFreq_);
    }

    void setSidModel(SIDModel model) noexcept {
        chip_.setModel(model);
    }

    void setVoiceStealingPolicy(VoiceStealingPolicy p) noexcept {
        allocator_.setPolicy(p);
    }

    void setMasterVolume(float normalized) noexcept {
        const float clean = std::clamp(std::isfinite(normalized) ? normalized : 0.8f, 0.0f, 1.0f);
        chip_.setMasterVolume(static_cast<std::uint8_t>(std::lround(clean * 15.0f)));
    }

    void setMasterTuneSemis(float semis) noexcept {
        masterTuneSemis_ = std::clamp(std::isfinite(semis) ? semis : 0.0f, -24.0f, 24.0f);
        retuneActiveVoices_();
    }

    void setGlobalPitchBendSemis(float semis) noexcept {
        globalPitchBendSemis_ =
            std::clamp(std::isfinite(semis) ? semis : 0.0f, -24.0f, 24.0f);
        retuneActiveVoices_();
    }

    void setPitchBend14(int channel, int raw14) noexcept {
        raw14 = std::clamp(raw14, 0, 16383);
        const int signedBend = raw14 - 8192;
        const float denom = signedBend >= 0 ? 8191.0f : 8192.0f;
        const float norm = std::clamp(static_cast<float>(signedBend) / denom, -1.0f, 1.0f);
        if (channel >= 0 && channel < 16) {
            pitchBendNormByChannel_[static_cast<std::size_t>(channel)] = norm;
        } else {
            pitchBendNormByChannel_.fill(norm);
        }
        retuneActiveVoices_();
    }

    void setPitchBendRangeSemis(int channel, float range) noexcept {
        const float clean = std::clamp(std::isfinite(range) ? range : 2.0f, 0.0f, 24.0f);
        if (channel >= 0 && channel < 16) {
            pitchBendRangeByChannel_[static_cast<std::size_t>(channel)] = clean;
        } else {
            pitchBendRangeByChannel_.fill(clean);
        }
        retuneActiveVoices_();
    }

    void setVoiceWaveform(int voice, std::uint8_t waveform) noexcept {
        if (voice < 0 || voice >= 3) return;
        waveform_[static_cast<std::size_t>(voice)] = waveform;
        chip_.getVoice(voice).setWaveform(waveform);
    }

    void setVoicePulseWidth(int voice, std::uint16_t pulseWidth) noexcept {
        if (voice < 0 || voice >= 3) return;
        pulseWidth_[static_cast<std::size_t>(voice)] = static_cast<std::uint16_t>(pulseWidth & 0x0FFFu);
        chip_.getVoice(voice).setPulseWidth(pulseWidth_[static_cast<std::size_t>(voice)]);
    }

    void setVoiceLevel(int voice, float level) noexcept {
        if (voice < 0 || voice >= 3) return;
        voiceLevel_[static_cast<std::size_t>(voice)] =
            std::clamp(std::isfinite(level) ? level : 0.0f, 0.0f, 1.0f);
        const auto& slot = allocator_.slot(static_cast<std::uint8_t>(voice));
        const float velocity = slot.inUse ? static_cast<float>(slot.velocity) * (1.0f / 127.0f) : 1.0f;
        chip_.setVoiceLevel(voice, voiceLevel_[static_cast<std::size_t>(voice)] * velocity);
    }

    void setVoiceLowFrequencyMode(int voice, bool enabled) noexcept {
        if (voice < 0 || voice >= 3) return;
        chip_.getVoice(voice).setLowFreqMode(enabled);
    }

    void setVoiceSyncEnable(int voice, bool enabled) noexcept {
        chip_.setVoiceSyncEnable(voice, enabled);
    }

    void setVoiceRingModEnable(int voice, bool enabled) noexcept {
        chip_.setVoiceRingModEnable(voice, enabled);
    }

    void setAttack(std::uint8_t value) noexcept {
        attack_ = static_cast<std::uint8_t>(value & 0x0Fu);
        for (int voice = 0; voice < 3; ++voice) chip_.getVoice(voice).setAttack(attack_);
    }

    void setDecay(std::uint8_t value) noexcept {
        decay_ = static_cast<std::uint8_t>(value & 0x0Fu);
        for (int voice = 0; voice < 3; ++voice) chip_.getVoice(voice).setDecay(decay_);
    }

    void setSustain(std::uint8_t value) noexcept {
        sustain_ = static_cast<std::uint8_t>(value & 0x0Fu);
        for (int voice = 0; voice < 3; ++voice) chip_.getVoice(voice).setSustain(sustain_);
    }

    void setRelease(std::uint8_t value) noexcept {
        release_ = static_cast<std::uint8_t>(value & 0x0Fu);
        for (int voice = 0; voice < 3; ++voice) chip_.getVoice(voice).setRelease(release_);
    }

    void setFilterCutoff(std::uint16_t value) noexcept { chip_.setFilterCutoff(value); }
    void setFilterResonance(std::uint8_t value) noexcept { chip_.setFilterResonance(value); }
    void setFilterMode(FilterMode mode) noexcept { chip_.setFilterMode(mode); }
    void setFilterVoiceRouting(bool v1, bool v2, bool v3) noexcept {
        chip_.setFilterVoiceRouting(v1, v2, v3);
    }

    // ── Note dispatch (host / sequencer thread) ─────────────────────────────
    // Returns the allocated SID voice index (0..2) or `kNoVoice` if the
    // allocator refused (only possible under VoiceStealingPolicy::Refuse).
    std::uint8_t noteOn(std::uint8_t midiNote,
                        std::uint8_t velocity = 100,
                        Drsid::ChokeGroup choke = Drsid::ChokeGroup::None) noexcept {
        return noteOn(midiNote, velocity, -1, -1, choke);
    }

    std::uint8_t noteOn(std::uint8_t midiNote,
                        std::uint8_t velocity,
                        int channel,
                        int noteId,
                        Drsid::ChokeGroup choke = Drsid::ChokeGroup::None) noexcept {
        const std::uint8_t voiceIdx = allocator_.allocate(midiNote, velocity, choke);
        if (voiceIdx == kNoVoice) return kNoVoice;
        channelByVoice_[voiceIdx] = channel;
        noteIdByVoice_[voiceIdx] = noteId;
        keyDownByVoice_[voiceIdx] = true;
        sustainHeldByVoice_[voiceIdx] = false;
        sostenutoHeldByVoice_[voiceIdx] = false;
        applyNoteOnToChipVoice_(voiceIdx, midiNote, velocity);
        return voiceIdx;
    }

    std::uint8_t forceNoteOnVoice(std::uint8_t voiceIdx,
                                  std::uint8_t midiNote,
                                  std::uint8_t velocity = 100,
                                  Drsid::ChokeGroup choke = Drsid::ChokeGroup::None) noexcept {
        const std::uint8_t v = allocator_.forceAllocate(voiceIdx, midiNote, velocity, choke);
        if (v == kNoVoice) return kNoVoice;
        channelByVoice_[v] = -1;
        noteIdByVoice_[v] = -1;
        keyDownByVoice_[v] = true;
        sustainHeldByVoice_[v] = false;
        sostenutoHeldByVoice_[v] = false;
        applyNoteOnToChipVoice_(v, midiNote, velocity);
        return v;
    }

    std::uint8_t forceNoteOnVoiceBookkeepingOnly(std::uint8_t voiceIdx,
                                                 std::uint8_t midiNote,
                                                 std::uint8_t velocity = 100,
                                                 Drsid::ChokeGroup choke = Drsid::ChokeGroup::None) noexcept {
        const std::uint8_t v = allocator_.forceAllocate(voiceIdx, midiNote, velocity, choke);
        if (v == kNoVoice) return kNoVoice;
        channelByVoice_[v] = -1;
        noteIdByVoice_[v] = -1;
        keyDownByVoice_[v] = true;
        sustainHeldByVoice_[v] = false;
        sostenutoHeldByVoice_[v] = false;
        return v;
    }

    // Returns the SID voice that was released, or `kNoVoice` if the note
    // wasn't currently allocated.
    std::uint8_t noteOff(std::uint8_t midiNote) noexcept {
        return noteOff(midiNote, -1, -1);
    }

    std::uint8_t noteOff(std::uint8_t midiNote, int channel, int noteId) noexcept {
        const std::uint8_t voiceIdx = matchingVoice_(midiNote, channel, noteId);
        if (voiceIdx == kNoVoice) return kNoVoice;
        keyDownByVoice_[voiceIdx] = false;
        const int ownerChannel = channelByVoice_[voiceIdx];
        const bool sustained =
            ownerChannel >= 0 && ownerChannel < 16 &&
            sustainDownByChannel_[static_cast<std::size_t>(ownerChannel)];
        sustainHeldByVoice_[voiceIdx] = sustained;
        if (!sustained && !sostenutoHeldByVoice_[voiceIdx]) releaseVoice_(voiceIdx);
        return voiceIdx;
    }

    void setSustainPedal(int channel, bool down) noexcept {
        if (channel < 0 || channel >= 16) {
            sustainDownByChannel_.fill(down);
        } else {
            sustainDownByChannel_[static_cast<std::size_t>(channel)] = down;
        }
        if (down) return;
        for (std::uint8_t voice = 0; voice < 3; ++voice) {
            const int ownerChannel = channelByVoice_[voice];
            if (channel >= 0 && ownerChannel >= 0 && ownerChannel != channel) continue;
            sustainHeldByVoice_[voice] = false;
            if (!keyDownByVoice_[voice] && !sostenutoHeldByVoice_[voice] &&
                allocator_.slot(voice).inUse && !allocator_.slot(voice).isReleasing) {
                releaseVoice_(voice);
            }
        }
    }

    void setSostenutoPedal(int channel, bool down) noexcept {
        if (channel < 0 || channel >= 16) return;
        sostenutoDownByChannel_[static_cast<std::size_t>(channel)] = down;
        for (std::uint8_t voice = 0; voice < 3; ++voice) {
            if (channelByVoice_[voice] >= 0 && channelByVoice_[voice] != channel) continue;
            if (down) {
                if (keyDownByVoice_[voice] && allocator_.slot(voice).inUse)
                    sostenutoHeldByVoice_[voice] = true;
            } else if (sostenutoHeldByVoice_[voice]) {
                sostenutoHeldByVoice_[voice] = false;
                if (!keyDownByVoice_[voice] && !sustainHeldByVoice_[voice] &&
                    allocator_.slot(voice).inUse && !allocator_.slot(voice).isReleasing) {
                    releaseVoice_(voice);
                }
            }
        }
    }

    // Panic / all-notes-off. Clears every gate on the chip and resets the
    // allocator. Renders silence after the chip's release tail completes.
    void allNotesOff() noexcept {
        for (int i = 0; i < 3; ++i) {
            chip_.getVoice(i).setGate(false);
        }
        allocator_.reset();
        clearIdentityState_();
    }

    void allNotesOffChannel(int channel) noexcept {
        if (channel < 0 || channel >= 16) {
            allNotesOff();
            return;
        }
        for (std::uint8_t voice = 0; voice < 3; ++voice) {
            if (!allocator_.slot(voice).inUse) continue;
            if (channelByVoice_[voice] >= 0 && channelByVoice_[voice] != channel) continue;
            chip_.getVoice(voice).setGate(false);
            allocator_.freeReleased(voice);
            clearVoiceIdentity_(voice);
        }
        sustainDownByChannel_[static_cast<std::size_t>(channel)] = false;
        sostenutoDownByChannel_[static_cast<std::size_t>(channel)] = false;
    }

    // ── Per-block / per-sample render ──────────────────────────────────────
    // Audit #29's *measurable* invariant: every output sample comes from
    // exactly ONE chip, never an N-chip sum. The mix across the 3 voices is
    // entirely the chip's own internal mixer.
    void processSample(float& outL, float& outR) noexcept {
        chip_.processSample(outL, outR);
    }

    void processBlock(float* outL, float* outR, int numSamples) noexcept {
        if (!outL || !outR || numSamples <= 0) return;
        // Tick the allocator's age counter once per block, then read back
        // envelopes after rendering so `StealQuietest` has fresh data for
        // the next block's allocation.
        allocator_.tick(static_cast<std::uint32_t>(numSamples));
        for (int i = 0; i < numSamples; ++i) {
            float l = 0.0f, r = 0.0f;
            chip_.processSample(l, r);
            outL[i] = l;
            outR[i] = r;
        }
        // Update envelope readback from the chip's voices. We use the
        // last-sample voice tap as a coarse envelope proxy. The allocator
        // uses this on the NEXT block's StealQuietest decisions.
        for (std::uint8_t i = 0; i < 3; ++i) {
            const float tap = std::abs(chip_.getVoiceLastSample(i));
            const float env = std::min(1.0f, tap);
            allocator_.updateEnvelopeForVoice(i, env);
        }
        // Garbage-collect released voices whose envelope has decayed below
        // the chip's "voice active" threshold. This frees the slot for the
        // next note-on.
        for (std::uint8_t i = 0; i < 3; ++i) {
            const auto& s = allocator_.slot(i);
            if (s.inUse && s.isReleasing && !chip_.isVoiceActive(i)) {
                allocator_.freeReleased(i);
                clearVoiceIdentity_(i);
            }
        }
    }

    // ── Diagnostic / test surface ──────────────────────────────────────────
    SIDChip&       chip()       noexcept { return chip_; }
    const SIDChip& chip() const noexcept { return chip_; }
    const SidVoiceAllocatorSingleChip& allocator() const noexcept { return allocator_; }
    double         sampleRate()      const noexcept { return sampleRate_; }
    double         clockFrequency()  const noexcept { return clockFreq_; }
    std::uint8_t   activeVoiceCount() const noexcept { return allocator_.activeCount(); }

private:
    // Convert a MIDI note to a 16-bit SID frequency register value using
    // the configured clock frequency:
    // sid_freq = round(hz * 2^24 / clock_freq), clamped to uint16
    std::uint16_t midiNoteToSidFrequency_(std::uint8_t midiNote, int channel) const noexcept {
        // A4 = 69 → 440 Hz. f = 440 * 2^((n-69)/12)
        float bend = 0.0f;
        if (channel >= 0 && channel < 16) {
            const std::size_t c = static_cast<std::size_t>(channel);
            bend = pitchBendNormByChannel_[c] * pitchBendRangeByChannel_[c];
        }
        const double note = static_cast<double>(midiNote) +
                            static_cast<double>(masterTuneSemis_ + globalPitchBendSemis_ + bend);
        const double hz = 440.0 * std::pow(2.0, (note - 69.0) / 12.0);
        const double sidF = std::round(hz * 16777216.0 / std::max(1.0, clockFreq_));
        const double clamped = (sidF < 0.0) ? 0.0 : (sidF > 65535.0 ? 65535.0 : sidF);
        return static_cast<std::uint16_t>(clamped);
    }

    // Apply a note-on event to the given SID voice. Default voice config
    // (triangle waveform, snappy envelope) suitable for monophonic per-voice
    // melodic playback. Callers that need different waveforms or filter
    // routes can poke the chip directly via `chip()`.
    void applyNoteOnToChipVoice_(std::uint8_t voiceIdx,
                                 std::uint8_t midiNote,
                                 std::uint8_t velocity) noexcept {
        SIDVoice& v = chip_.getVoice(voiceIdx);
        v.setGate(false);
        v.setFrequency(midiNoteToSidFrequency_(midiNote, channelByVoice_[voiceIdx]));
        v.setPulseWidth(pulseWidth_[voiceIdx]);
        v.setWaveform(waveform_[voiceIdx]);
        v.setAttack(attack_);
        v.setDecay(decay_);
        v.setSustain(sustain_);
        v.setRelease(release_);
        chip_.setVoiceLevel(voiceIdx,
                            voiceLevel_[voiceIdx] *
                            static_cast<float>(velocity) * (1.0f / 127.0f));
        // Gate the voice last so hard-restart semantics are consistent with
        // the audit's §B "C64 punch" invariant.
        v.setGate(true);
    }

    std::uint8_t matchingVoice_(std::uint8_t midiNote, int channel, int noteId) const noexcept {
        for (std::uint8_t voice = 0; voice < 3; ++voice) {
            const auto& slot = allocator_.slot(voice);
            if (!slot.inUse || slot.isReleasing || slot.midiNote != midiNote) continue;
            const bool channelMatches =
                channel < 0 || channelByVoice_[voice] < 0 || channelByVoice_[voice] == channel;
            const bool noteIdMatches =
                noteId < 0 ? noteIdByVoice_[voice] < 0 : noteIdByVoice_[voice] == noteId;
            if (channelMatches && noteIdMatches) return voice;
        }
        return kNoVoice;
    }

    void releaseVoice_(std::uint8_t voice) noexcept {
        if (allocator_.releaseVoice(voice) == kNoVoice) return;
        chip_.getVoice(voice).setGate(false);
    }

    void retuneActiveVoices_() noexcept {
        for (std::uint8_t voice = 0; voice < 3; ++voice) {
            const auto& slot = allocator_.slot(voice);
            if (!slot.inUse) continue;
            chip_.getVoice(voice).setFrequency(
                midiNoteToSidFrequency_(slot.midiNote, channelByVoice_[voice]));
        }
    }

    void clearVoiceIdentity_(std::uint8_t voice) noexcept {
        channelByVoice_[voice] = -1;
        noteIdByVoice_[voice] = -1;
        keyDownByVoice_[voice] = false;
        sustainHeldByVoice_[voice] = false;
        sostenutoHeldByVoice_[voice] = false;
    }

    void clearIdentityState_() noexcept {
        channelByVoice_.fill(-1);
        noteIdByVoice_.fill(-1);
        keyDownByVoice_.fill(false);
        sustainHeldByVoice_.fill(false);
        sostenutoHeldByVoice_.fill(false);
        sustainDownByChannel_.fill(false);
        sostenutoDownByChannel_.fill(false);
    }

    SIDChip chip_{};
    SidVoiceAllocatorSingleChip allocator_{};
    double sampleRate_ = 44100.0;
    double clockFreq_  = PAL_CLOCK_FREQ;
    float masterTuneSemis_ = 0.0f;
    float globalPitchBendSemis_ = 0.0f;
    std::array<float, 16> pitchBendNormByChannel_{};
    std::array<float, 16> pitchBendRangeByChannel_{
        2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f,
        2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f
    };
    std::array<std::uint8_t, 3> waveform_{0x10u, 0x10u, 0x10u};
    std::array<std::uint16_t, 3> pulseWidth_{2048u, 2048u, 2048u};
    std::array<float, 3> voiceLevel_{1.0f, 1.0f, 1.0f};
    std::uint8_t attack_ = 2u;
    std::uint8_t decay_ = 7u;
    std::uint8_t sustain_ = 12u;
    std::uint8_t release_ = 5u;
    std::array<int, 3> channelByVoice_{-1, -1, -1};
    std::array<int, 3> noteIdByVoice_{-1, -1, -1};
    std::array<bool, 3> keyDownByVoice_{};
    std::array<bool, 3> sustainHeldByVoice_{};
    std::array<bool, 3> sostenutoHeldByVoice_{};
    std::array<bool, 16> sustainDownByChannel_{};
    std::array<bool, 16> sostenutoDownByChannel_{};
};

} // namespace ArpSID

#endif // ARPSID_ENGINES_SINGLE_SID_THREE_VOICE_ENGINE_H
