#pragma once
#include <cstring>
#include "sid_runtime_model.h"
#include "sid_runtime_backend.h"

namespace ArpSID {

template <class Delegate>
inline void renderCanonicalAudioKernel(SidRuntimeModel& runtime, Delegate& d, float* left, float* right, int frames) noexcept {
    if (!left || !right || frames <= 0) return;
    std::memset(left, 0, (size_t)frames * sizeof(float));
    std::memset(right, 0, (size_t)frames * sizeof(float));

    switch (runtime.resolveRenderMode()) {
        case SidRuntimeRenderMode::SidRegister:
            d.renderSidRegister(left, right, frames);
            return;
        case SidRuntimeRenderMode::DrSid:
            d.renderDrSid(left, right, frames);
            return;
        case SidRuntimeRenderMode::BitPerfect:
        default:
            // Always call the arpeggiator event collector. collectTimedEvents() owns
            // the enabled/noteCount early-exit and must run once after disable/rewind
            // so any pending gate-off is flushed to the BitPerfect engine.
            d.renderBitPerfectWithArp(left, right, frames);
            return;
    }
}

} // namespace ArpSID

namespace ArpSID {

inline void SidRuntimeModel::renderBoundAudio(float* left, float* right, int frames) noexcept {
    if (!backend_) return;
    renderCanonicalAudioKernel(*this, *backend_, left, right, frames);
}

} // namespace ArpSID
