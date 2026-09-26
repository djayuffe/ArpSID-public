// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace ArpSID {

struct SidRuntimeHostSurface {
    static constexpr std::size_t kHeldIngressIdentityLanes = 8;

    // Raw MIDI held replay has no host-provided note identity. Give each held
    // lane a stable synthetic identity for replay ordering, but keep it in the
    // anonymous (negative) note-id domain so a normal noteId-less NoteOff can
    // still release the replayed SynthMode/voice-manager note.
    static constexpr int32_t makeSyntheticAnonymousNoteId(uint32_t serial) noexcept {
        uint32_t payload = serial & 0x7FFFFFFFu;
        if (payload == 0x7FFFFFFFu) payload = 0u;
        return static_cast<int32_t>(-0x7FFFFFFF - 1 + static_cast<int32_t>(payload));
    }

    static constexpr bool isSyntheticAnonymousNoteId(int32_t noteId) noexcept {
        return noteId < -1;
    }

    float channelPressure = 0.0f;
    float pitchBendNorm = 0.5f;
    std::array<float, 16> currentChannelPressure{};
    std::array<float, 16> currentPitchBendNorm{};
    std::array<float, 16> prevModWheel{};
    std::array<float, 16> prevExpression{};
    std::array<float, 16> prevSustain{};
    std::array<float, 16> prevChannelPressure{};
    std::array<float, 16> prevPitchBend{};
    std::array<float, 16> softPedalSavedCutoff{};
    std::array<std::array<std::atomic<uint8_t>, 128>, 16> heldIngressVelocity{};
    std::array<std::array<std::atomic<uint8_t>, 128>, 16> heldIngressDepth{};
    std::array<std::array<std::atomic<int32_t>, 128>, 16> heldIngressNoteId{};
    std::array<std::array<std::atomic<uint32_t>, 128>, 16> heldIngressOrder{};
    // Per-channel generation clears held-note ingress in O(1). A held note is live only
    // when heldIngressNoteGeneration[ch][note] equals heldIngressChannelGeneration[ch].
    std::array<std::atomic<uint32_t>, 16> heldIngressChannelGeneration{};
    std::array<std::array<std::atomic<uint32_t>, 128>, 16> heldIngressNoteGeneration{};
    std::array<std::array<std::array<std::atomic<uint8_t>, kHeldIngressIdentityLanes>, 128>, 16> heldIngressLaneVelocity{};
    std::array<std::array<std::array<std::atomic<int32_t>, kHeldIngressIdentityLanes>, 128>, 16> heldIngressLaneNoteId{};
    std::array<std::array<std::array<std::atomic<uint32_t>, kHeldIngressIdentityLanes>, 128>, 16> heldIngressLaneOrder{};
    std::atomic<uint32_t> heldIngressSerial{1u};

    double hostTempo = 120.0;
    bool transportPlaying = false;
    bool lastTransportPlaying = false;
    double hostBeatPosition = 0.0;
    double lastPublishedBeatPosition = 0.0;
    bool hasPublishedBeatPosition = false;

    void resetIngress() noexcept {
        for (auto& ch : heldIngressVelocity)
            for (auto& note : ch)
                note.store(0u, std::memory_order_relaxed);
        for (auto& ch : heldIngressDepth)
            for (auto& note : ch)
                note.store(0u, std::memory_order_relaxed);
        for (auto& ch : heldIngressNoteId)
            for (auto& note : ch)
                note.store(-1, std::memory_order_relaxed);
        for (auto& ch : heldIngressOrder)
            for (auto& note : ch)
                note.store(0u, std::memory_order_relaxed);
        for (auto& gen : heldIngressChannelGeneration)
            gen.store(1u, std::memory_order_relaxed);
        for (auto& ch : heldIngressNoteGeneration)
            for (auto& note : ch)
                note.store(0u, std::memory_order_relaxed);
        for (auto& ch : heldIngressLaneVelocity)
            for (auto& note : ch)
                for (auto& lane : note)
                    lane.store(0u, std::memory_order_relaxed);
        for (auto& ch : heldIngressLaneNoteId)
            for (auto& note : ch)
                for (auto& lane : note)
                    lane.store(-1, std::memory_order_relaxed);
        for (auto& ch : heldIngressLaneOrder)
            for (auto& note : ch)
                for (auto& lane : note)
                    lane.store(0u, std::memory_order_relaxed);
        heldIngressSerial.store(1u, std::memory_order_relaxed);
    }
};

} // namespace ArpSID
