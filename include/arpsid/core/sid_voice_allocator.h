// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// sid_voice_allocator.h — Authentic SID-voice allocator for the single-SID
// 3-voice (or dual-SID 6-voice) topology audit #29 demands.
//
// PROBLEM
// ------
// `BitPerfectEngine` instantiates `MAX_POLYPHONY` independent `SIDChip`
// instances and allocates a fresh chip per MIDI note. That produces
// "synth polyphony" but is *not* cycle-accurate single-SID C64 behavior.
// A real C64 has exactly THREE voices on ONE chip; polyphony beyond three
// requires voice stealing.
//
// This header defines the canonical voice allocator the future
// `SingleSidThreeVoiceEngine` will use. It is parallel infrastructure// it does NOT replace the existing per-voice-chip path (which the audit
// itself acknowledged as "musically useful"). The new path is for
// users / kits who want bit-accurate single-SID behavior.
//
// CONTRACT
// -------
// * `kVoiceSlotCount` is a compile-time constant (default 3 for one SID;
// instantiate with 6 for dual-SID).
// * Allocation policy is selectable: `StealOldest`, `StealQuietest`,
// `StealReleasingFirst`, `Refuse` (drop the incoming note).
// * Choke groups (HiHat, NoiseShared, etc.) are honored — a note in the
// same group as an existing voice always reuses that voice's slot.
// * `tick(samples)` advances per-voice age so `StealOldest` is deterministic.
// * All POD / no allocation / RT-safe.

#ifndef ARPSID_CORE_SID_VOICE_ALLOCATOR_H
#define ARPSID_CORE_SID_VOICE_ALLOCATOR_H

#include "arpsid/core/drsid_instrument_program.h"

#include <array>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace ArpSID {

enum class VoiceStealingPolicy : std::uint8_t {
    StealOldest          = 0,  ///< replace the voice with the highest age (default — classic SID behavior)
    StealQuietest        = 1,  ///< replace the voice with the lowest current envelope value
    StealReleasingFirst  = 2,  ///< prefer to replace voices already in release; else fall through to StealOldest
    Refuse               = 3,  ///< drop the incoming note when no slot is free
};

constexpr const char* voiceStealingPolicyName(VoiceStealingPolicy p) noexcept {
    switch (p) {
        case VoiceStealingPolicy::StealOldest:          return "steal_oldest";
        case VoiceStealingPolicy::StealQuietest:        return "steal_quietest";
        case VoiceStealingPolicy::StealReleasingFirst:  return "steal_releasing_first";
        case VoiceStealingPolicy::Refuse:               return "refuse";
    }
    return "unknown";
}

// Per-slot state. POD by design.
struct SidVoiceSlot {
    bool                inUse        = false;
    bool                isReleasing  = false;
    std::uint8_t        midiNote     = 0;
    std::uint8_t        velocity     = 0;
    Drsid::ChokeGroup   chokeGroup   = Drsid::ChokeGroup::None;
    std::uint32_t       ageSamples   = 0;     ///< samples since this slot was (re-)allocated
    float               currentEnvelope = 0.0f; ///< 0..1; consumer updates from SID chip readback for StealQuietest
};

static_assert(std::is_trivially_copyable<SidVoiceSlot>::value,
              "SidVoiceSlot must be trivially copyable for snapshot/atomic handoff");

// Sentinel for "no voice was allocated".
inline constexpr std::uint8_t kNoVoice = std::numeric_limits<std::uint8_t>::max();

template <std::uint8_t VoiceSlotCount>
class SidVoiceAllocator {
    static_assert(VoiceSlotCount >= 1 && VoiceSlotCount <= 16,
                  "VoiceSlotCount must lie in [1,16]; canonical values are 3 (single SID), 6 (dual SID), 9 (triple SID)");
public:
    static constexpr std::uint8_t kVoiceSlotCount = VoiceSlotCount;

    SidVoiceAllocator() noexcept = default;

    void setPolicy(VoiceStealingPolicy p) noexcept { policy_ = p; }
    VoiceStealingPolicy policy() const noexcept { return policy_; }

    // Advance per-slot age counters by `samples`. Call once per render block
    // BEFORE allocating any new notes for the block — that keeps the age
    // monotone within a render boundary.
    void tick(std::uint32_t samples) noexcept {
        for (auto& s : slots_) {
            if (s.inUse) {
                const std::uint64_t newAge = static_cast<std::uint64_t>(s.ageSamples) + samples;
                s.ageSamples = (newAge > std::numeric_limits<std::uint32_t>::max())
                    ? std::numeric_limits<std::uint32_t>::max()
                    : static_cast<std::uint32_t>(newAge);
            }
        }
    }

    // Update current-envelope readout for a slot (consumer feeds SID ADSR
    // value here so `StealQuietest` has a real value to compare against).
    void updateEnvelopeForVoice(std::uint8_t voiceIndex, float env01) noexcept {
        if (voiceIndex >= kVoiceSlotCount) return;
        const float clamped = (env01 < 0.0f) ? 0.0f : (env01 > 1.0f ? 1.0f : env01);
        slots_[voiceIndex].currentEnvelope = clamped;
    }

    // Look up the voice index currently playing `note`. Returns `kNoVoice`
    // if no slot is playing this note. Used for note-off lookup.
    std::uint8_t voiceForNote(std::uint8_t midiNote) const noexcept {
        for (std::uint8_t i = 0; i < kVoiceSlotCount; ++i) {
            const auto& s = slots_[i];
            if (s.inUse && !s.isReleasing && s.midiNote == midiNote) return i;
        }
        return kNoVoice;
    }

    // Allocate a slot for `midiNote`. Returns the voice index (0..kVoiceSlotCount-1)
    // or `kNoVoice` if the policy refused (only happens with policy Refuse).
    //
    // Allocation order:
    // 1. If `chokeGroup != None` and any active slot has the same group,
    // reuse that slot (choke-and-replace).
    // 2. Else if a slot is free (inUse==false), use the lowest-indexed free slot.
    // 3. Else apply the stealing policy.
    std::uint8_t allocate(std::uint8_t midiNote,
                          std::uint8_t velocity,
                          Drsid::ChokeGroup chokeGroup = Drsid::ChokeGroup::None) noexcept {
        // (1) Choke-group reuse
        if (chokeGroup != Drsid::ChokeGroup::None) {
            for (std::uint8_t i = 0; i < kVoiceSlotCount; ++i) {
                if (slots_[i].inUse && slots_[i].chokeGroup == chokeGroup) {
                    fillSlot_(i, midiNote, velocity, chokeGroup);
                    return i;
                }
            }
        }
        // (2) First free slot
        for (std::uint8_t i = 0; i < kVoiceSlotCount; ++i) {
            if (!slots_[i].inUse) {
                fillSlot_(i, midiNote, velocity, chokeGroup);
                return i;
            }
        }
        // (3) Stealing policy
        const std::uint8_t victim = pickStealVictim_();
        if (victim == kNoVoice) return kNoVoice; // Refuse policy
        fillSlot_(victim, midiNote, velocity, chokeGroup);
        return victim;
    }

    // Force a specific physical SID voice. Used by drum engines with fixed
    // family-to-voice contracts so allocator bookkeeping matches the audible
    // voice that is actually programmed.
    std::uint8_t forceAllocate(std::uint8_t voiceIndex,
                               std::uint8_t midiNote,
                               std::uint8_t velocity,
                               Drsid::ChokeGroup chokeGroup = Drsid::ChokeGroup::None) noexcept {
        if (voiceIndex >= kVoiceSlotCount) return kNoVoice;
        // Choke-group law: clear any other slot in the same group first so
        // note-off/active-count state cannot retain a stale hat/noise voice.
        if (chokeGroup != Drsid::ChokeGroup::None) {
            for (std::uint8_t i = 0; i < kVoiceSlotCount; ++i) {
                if (i != voiceIndex && slots_[i].inUse && slots_[i].chokeGroup == chokeGroup) {
                    slots_[i] = SidVoiceSlot{};
                }
            }
        }
        fillSlot_(voiceIndex, midiNote, velocity, chokeGroup);
        return voiceIndex;
    }

    // Mark the slot playing `note` as releasing. Returns the voice index
    // (so the caller can clear the SID gate bit), or `kNoVoice` if not found.
    // The slot stays inUse until `freeReleased(voiceIndex)` is called once
    // the envelope completes.
    std::uint8_t release(std::uint8_t midiNote) noexcept {
        const std::uint8_t v = voiceForNote(midiNote);
        if (v == kNoVoice) return kNoVoice;
        return releaseVoice(v);
    }

    // Mark a specific physical voice as releasing. This is the canonical
    // release primitive for hosts that track MIDI channel/note-id separately
    // from the allocator's hardware-slot bookkeeping.
    std::uint8_t releaseVoice(std::uint8_t voiceIndex) noexcept {
        if (voiceIndex >= kVoiceSlotCount || !slots_[voiceIndex].inUse)
            return kNoVoice;
        slots_[voiceIndex].isReleasing = true;
        return voiceIndex;
    }

    // Free a slot whose envelope has finished decaying. Called by the
    // SID-chip-watching path after the release tail completes.
    void freeReleased(std::uint8_t voiceIndex) noexcept {
        if (voiceIndex >= kVoiceSlotCount) return;
        slots_[voiceIndex] = SidVoiceSlot{};
    }

    // Hard-clear (panic / all-notes-off). All slots become free.
    void reset() noexcept {
        for (auto& s : slots_) s = SidVoiceSlot{};
    }

    // Diagnostic / consumer accessors (const).
    const SidVoiceSlot& slot(std::uint8_t i) const noexcept {
        return slots_[i < kVoiceSlotCount ? i : 0];
    }
    std::uint8_t activeCount() const noexcept {
        std::uint8_t c = 0;
        for (const auto& s : slots_) if (s.inUse) ++c;
        return c;
    }
    std::uint8_t releasingCount() const noexcept {
        std::uint8_t c = 0;
        for (const auto& s : slots_) if (s.inUse && s.isReleasing) ++c;
        return c;
    }

private:
    void fillSlot_(std::uint8_t i, std::uint8_t note, std::uint8_t vel, Drsid::ChokeGroup g) noexcept {
        slots_[i].inUse        = true;
        slots_[i].isReleasing  = false;
        slots_[i].midiNote     = note;
        slots_[i].velocity     = vel;
        slots_[i].chokeGroup   = g;
        slots_[i].ageSamples   = 0u;
        slots_[i].currentEnvelope = static_cast<float>(vel) * (1.0f / 127.0f);
    }

    std::uint8_t pickStealVictim_() const noexcept {
        switch (policy_) {
            case VoiceStealingPolicy::Refuse:
                return kNoVoice;
            case VoiceStealingPolicy::StealReleasingFirst: {
                std::uint8_t best = kNoVoice;
                std::uint32_t bestAge = 0;
                for (std::uint8_t i = 0; i < kVoiceSlotCount; ++i) {
                    if (slots_[i].inUse && slots_[i].isReleasing) {
                        if (best == kNoVoice || slots_[i].ageSamples > bestAge) {
                            best = i; bestAge = slots_[i].ageSamples;
                        }
                    }
                }
                if (best != kNoVoice) return best;
            }
            [[fallthrough]]; // no releasing voice found → steal oldest
            case VoiceStealingPolicy::StealOldest: {
                std::uint8_t best = 0;
                std::uint32_t bestAge = slots_[0].ageSamples;
                for (std::uint8_t i = 1; i < kVoiceSlotCount; ++i) {
                    if (slots_[i].ageSamples > bestAge) {
                        bestAge = slots_[i].ageSamples;
                        best = i;
                    }
                }
                return best;
            }
            case VoiceStealingPolicy::StealQuietest: {
                std::uint8_t best = 0;
                float bestEnv = slots_[0].currentEnvelope;
                for (std::uint8_t i = 1; i < kVoiceSlotCount; ++i) {
                    if (slots_[i].currentEnvelope < bestEnv) {
                        bestEnv = slots_[i].currentEnvelope;
                        best = i;
                    }
                }
                return best;
            }
        }
        return 0;
    }

    std::array<SidVoiceSlot, kVoiceSlotCount> slots_{};
    VoiceStealingPolicy policy_ = VoiceStealingPolicy::StealOldest;
};

// Canonical SID voice allocators per topology.
using SidVoiceAllocatorSingleChip = SidVoiceAllocator<3>; // one SID — three voices
using SidVoiceAllocatorDualChip   = SidVoiceAllocator<6>; // two SIDs — six voices
using SidVoiceAllocatorTripleChip = SidVoiceAllocator<9>; // three SIDs — nine voices

} // namespace ArpSID

#endif // ARPSID_CORE_SID_VOICE_ALLOCATOR_H
