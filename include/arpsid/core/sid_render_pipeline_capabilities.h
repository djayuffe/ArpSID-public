// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once

#include <cstdint>

namespace ArpSID {

// Explicit host-pipeline contract. AU owns the user-facing KIT/DIGI/MIX layer;
// Phase2/VST3 currently exposes the canonical SID engines plus canonical post-FX.
// Keeping this compile-time contract prevents wrappers from claiming behavioral
// parity for layers they do not instantiate or persist.
enum class SidRenderPipelineCapability : std::uint32_t {
    CanonicalTimedEvents = 1u << 0,
    FractionalSidRender  = 1u << 1,
    ExactPostFxTimeline  = 1u << 2,
    NoOutputContinuation = 1u << 3,
    KitSequencerOverlay  = 1u << 4,
    DigiOverlay          = 1u << 5,
    MixFx                = 1u << 6,
};

using SidRenderPipelineCapabilities = std::uint32_t;

constexpr SidRenderPipelineCapabilities sidPipelineBit(SidRenderPipelineCapability c) noexcept {
    return static_cast<SidRenderPipelineCapabilities>(c);
}

constexpr bool sidPipelineHas(SidRenderPipelineCapabilities set,
                              SidRenderPipelineCapability c) noexcept {
    return (set & sidPipelineBit(c)) != 0u;
}

inline constexpr SidRenderPipelineCapabilities kAuRenderPipelineCapabilities =
    sidPipelineBit(SidRenderPipelineCapability::CanonicalTimedEvents) |
    sidPipelineBit(SidRenderPipelineCapability::FractionalSidRender) |
    sidPipelineBit(SidRenderPipelineCapability::ExactPostFxTimeline) |
    sidPipelineBit(SidRenderPipelineCapability::NoOutputContinuation) |
    sidPipelineBit(SidRenderPipelineCapability::KitSequencerOverlay) |
    sidPipelineBit(SidRenderPipelineCapability::DigiOverlay) |
    sidPipelineBit(SidRenderPipelineCapability::MixFx);

inline constexpr SidRenderPipelineCapabilities kPhase2RenderPipelineCapabilities =
    sidPipelineBit(SidRenderPipelineCapability::CanonicalTimedEvents) |
    sidPipelineBit(SidRenderPipelineCapability::FractionalSidRender) |
    sidPipelineBit(SidRenderPipelineCapability::ExactPostFxTimeline) |
    sidPipelineBit(SidRenderPipelineCapability::NoOutputContinuation);

static_assert(sidPipelineHas(kAuRenderPipelineCapabilities,
                             SidRenderPipelineCapability::KitSequencerOverlay));
static_assert(!sidPipelineHas(kPhase2RenderPipelineCapabilities,
                              SidRenderPipelineCapability::KitSequencerOverlay));

} // namespace ArpSID
