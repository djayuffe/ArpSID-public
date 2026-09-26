// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// kit_sequencer.h — A8: Compile KIT grid to render-owned drum sequencer events.
//
// PURPOSE
// ------
// `compileKitSequencer()` converts a `KitStepGrid` + `KitPanelModel` into a
// `CompiledKitSequencer` — a fixed-size, trivially-copyable struct that the
// render thread owns and reads without allocating or touching GUI state.
//
// DESIGN
// -----// Each compiled event carries: drum class, MIDI note hint, velocity, accent.
// The render thread advances a step cursor; on each step boundary it fires all
// events for the current step. Steps with velocity==0 produce no events.
//
// ATOMICITY (B6-equivalent for kit)
// ---------------------------------
// `compileKitSequencer()` returns false if the input is malformed; the output
// is NOT partially mutated — the caller's `CompiledKitSequencer` is only
// replaced on success.

#ifndef ARPSID_GUI_KIT_SEQUENCER_H
#define ARPSID_GUI_KIT_SEQUENCER_H

#include "arpsid/gui/kit_step_grid.h"
#include "arpsid/gui/kit_panel_model.h"
#include "arpsid/gui/kit_voice_config.h"
#include "arpsid/gui/kit_assign_config.h"

#include <array>
#include <cstdint>
#include <type_traits>

namespace ArpSID {
namespace GUI {

// ─── CompiledKitEvent ────────────────────────────────────────────────────────
struct CompiledKitEvent {
    std::uint8_t drumClass;    ///< KitDrumClass index (0..kKitDrumClassCount-1)
    std::uint8_t midiNote;     ///< canonical GM MIDI note hint
    std::uint8_t velocity;     ///< 1–127
    std::uint8_t flags;        ///< bit0=accent (matches kKitStepFlagAccent)
    std::uint8_t engineTarget; ///< KitEngineTarget
    std::uint8_t selectedFactorySlotLo;
    std::uint8_t selectedFactorySlotHi;
    std::uint8_t digiSlotIndex;
    std::uint8_t voiceWaveform;
    std::uint8_t voiceAttackDecay;
    std::uint8_t voiceSustainRelease;
    std::uint8_t voicePulseWidthLo;
    std::uint8_t voicePulseWidthHi;
    std::uint8_t voiceFlags;
    std::uint8_t voiceOverrideMask;
    std::uint8_t reserved_[1];
};
static_assert(sizeof(CompiledKitEvent) == 16, "CompiledKitEvent pinned at 16 bytes");
static_assert(std::is_trivially_copyable<CompiledKitEvent>::value,
              "CompiledKitEvent must be trivially copyable");

// ─── CompiledKitStep ─────────────────────────────────────────────────────────
// All events that fire at one step boundary (up to kKitDrumClassCount voices).
struct CompiledKitStep {
    std::uint8_t   eventCount;                             ///< 0 = silent step
    std::uint8_t   pad_[3];
    CompiledKitEvent events[kKitDrumClassCount];           ///< up to 9 per step
};
static_assert(sizeof(CompiledKitStep) == 4 + kKitDrumClassCount * sizeof(CompiledKitEvent),
              "CompiledKitStep size check");

// ─── CompiledKitSequencer ────────────────────────────────────────────────────
// Full compiled pattern: kKitStepCount steps × CompiledKitStep.
// Trivially copyable — safe for atomic-swap between GUI and render threads.
inline constexpr std::uint32_t kCompiledKitSchemaVersion = 1u;

struct CompiledKitSequencer {
    std::uint32_t   schemaVersion = kCompiledKitSchemaVersion;
    std::uint8_t    stepCount     = kKitStepCount;
    std::uint8_t    pad_[3]       = {};
    std::uint32_t   generation    = 0u;  ///< incremented on each successful compile
    CompiledKitStep steps[kKitStepCount];
};
static_assert(std::is_trivially_copyable<CompiledKitSequencer>::value,
              "CompiledKitSequencer must be trivially copyable");

// ─── Default construction ─────────────────────────────────────────────────────
inline CompiledKitSequencer makeDefaultCompiledKitSequencer() noexcept {
    CompiledKitSequencer cs{};
    cs.schemaVersion = kCompiledKitSchemaVersion;
    cs.stepCount     = kKitStepCount;
    cs.generation    = 0u;
    for (auto& step : cs.steps) {
        step.eventCount = 0u;
        step.pad_[0] = step.pad_[1] = step.pad_[2] = 0u;
    }
    return cs;
}

// ─── compileKitSequencer ─────────────────────────────────────────────────────
// A8: Convert KitStepGrid + KitPanelModel into a CompiledKitSequencer.
// Returns true on success; on failure `out` is NOT mutated (B6-style atomicity).
// Increments out.generation on each successful call.
[[nodiscard]] inline bool compileKitSequencer(CompiledKitSequencer& out,
                                               const KitStepGrid&    grid,
                                               const KitPanelModel&  model,
                                               const KitVoiceConfigGrid& voiceGrid,
                                               const KitAssignConfigGrid& assignGrid) noexcept {
    if (!kitStepGridIsWellFormed(grid)) return false;
    if (!kitPanelModelIsWellFormed(model)) return false;
    if (!kitVoiceConfigGridIsWellFormed(voiceGrid)) return false;
    KitAssignConfigGrid sanitizedAssign = assignGrid;
    kitAssignSanitizeGrid(sanitizedAssign);

    // Build into a staging buffer first (B6 atomicity: never partial-mutate out).
    CompiledKitSequencer staging = makeDefaultCompiledKitSequencer();
    staging.generation = out.generation + 1u;

    for (std::uint8_t step = 0u; step < kKitStepCount; ++step) {
        CompiledKitStep& cs = staging.steps[step];
        cs.eventCount = 0u;
        for (std::uint8_t dc = 0u; dc < kKitDrumClassCount; ++dc) {
            const std::uint8_t vel   = grid.steps[dc][step];
            const std::uint8_t flags = (grid.schemaVersion >= 2u)
                                       ? grid.stepFlags[dc][step] : 0u;
            if (vel == 0u) continue;  // inactive step
            const std::uint8_t midiNote = kKitDrumClassMidiNote[dc];
            const std::uint8_t resolvedTargetByte =
                kitAssignResolveEngineTarget(sanitizedAssign.assignConfigs[dc], model.activeEngineTarget);
            const auto target = static_cast<KitEngineTarget>(
                std::min<std::uint8_t>(resolvedTargetByte, kKitEngineTargetCount - 1u));
            const KitDrumClassAssignment& asn =
                model.drumAssignments[dc][static_cast<std::uint8_t>(target)];
            const std::uint16_t slot = kitAbsoluteSlot(target, asn.factorySlotIndex);
            auto& ev     = cs.events[cs.eventCount++];
            ev.drumClass = dc;
            ev.midiNote  = midiNote;
            ev.velocity  = vel;
            ev.flags     = flags;
            ev.engineTarget = static_cast<std::uint8_t>(target);
            ev.selectedFactorySlotLo = static_cast<std::uint8_t>(slot & 0xFFu);
            ev.selectedFactorySlotHi = static_cast<std::uint8_t>((slot >> 8) & 0xFFu);
            ev.digiSlotIndex = asn.factorySlotIndex;
            const KitVoiceConfig& vc = voiceGrid.voiceConfigs[dc];
            ev.voiceWaveform = vc.waveform;
            ev.voiceAttackDecay = vc.attackDecay;
            ev.voiceSustainRelease = vc.sustainRelease;
            ev.voicePulseWidthLo = vc.pulseWidthLo;
            ev.voicePulseWidthHi = vc.pulseWidthHi;
            ev.voiceFlags = vc.flags;
            ev.voiceOverrideMask = kitVoiceRuntimeOverrideMask(vc);
        }
    }

    // Only write to out on full success.
    out = staging;
    return true;
}


[[nodiscard]] inline bool compileKitSequencer(CompiledKitSequencer& out,
                                               const KitStepGrid&    grid,
                                               const KitPanelModel&  model,
                                               const KitVoiceConfigGrid& voiceGrid) noexcept {
    return compileKitSequencer(out, grid, model, voiceGrid, makeDefaultKitAssignConfigGrid());
}

[[nodiscard]] inline bool compileKitSequencer(CompiledKitSequencer& out,
                                               const KitStepGrid&    grid,
                                               const KitPanelModel&  model) noexcept {
    return compileKitSequencer(out, grid, model, makeDefaultKitVoiceConfigGrid(), makeDefaultKitAssignConfigGrid());
}

} // namespace GUI
} // namespace ArpSID

#endif // ARPSID_GUI_KIT_SEQUENCER_H
