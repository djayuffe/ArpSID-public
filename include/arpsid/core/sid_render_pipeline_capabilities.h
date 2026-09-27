// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once

#include <cstdint>

namespace ArpSID {

// Explicit host-pipeline contract. Every wrapper (AUv2, AUv3, Standalone and
// VST3) runs the shared ArpSIDDSPKernel, so all of them instantiate and persist
// the full pipeline including the KIT/DIGI/MIX layer. Keeping this
// compile-time contract prevents a wrapper from claiming layers it lacks.
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

// VST3 hosts the same kernel (source/vst3/arpsid_vst3_kernel_host.h).
inline constexpr SidRenderPipelineCapabilities kVst3RenderPipelineCapabilities =
    kAuRenderPipelineCapabilities;

static_assert(sidPipelineHas(kAuRenderPipelineCapabilities,
                             SidRenderPipelineCapability::KitSequencerOverlay));
static_assert(kVst3RenderPipelineCapabilities == kAuRenderPipelineCapabilities,
              "VST3 and AU share one kernel and one pipeline");

} // namespace ArpSID
