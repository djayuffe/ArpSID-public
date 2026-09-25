#pragma once

#include "../engines/sid_register_engine.h"
#include <type_traits>

namespace ArpSID {

struct SidNoopAppliedWriteObserver {
    void operator()(const SidWrite&) const noexcept {}
};

template <class AppliedWriteObserver>
inline void renderSidRegisterQueueToStereo(SidRegisterEngine& sidRegister,
                                           SidWriteQueue& q,
                                           float* left,
                                           float* right,
                                           int frames,
                                           int baseSampleOffset,
                                           AppliedWriteObserver&& onAppliedWrite) noexcept {
    if (!left || !right || frames <= 0) return;

    q.sortStable();

    auto view = q.data();
    const SidWrite* it = view.begin();
    const SidWrite* end = view.end();
    const int sliceEnd = baseSampleOffset + frames;

    for (int i = 0; i < frames; ++i) {
        const int absSample = baseSampleOffset + i;
        while (it != end && static_cast<int>(it->sampleOffset) < absSample) {
            sidRegister.write(it->regIndex, it->value);
            if constexpr (!std::is_same_v<std::decay_t<AppliedWriteObserver>, std::nullptr_t>) {
                onAppliedWrite(*it);
            }
            ++it;
        }

        const SidWrite* wb = it;
        while (it != end && static_cast<int>(it->sampleOffset) == absSample) ++it;

        const float s = sidRegister.renderTimedSampleAccurate(wb, it);
        left[i] = s;
        right[i] = s;

        if constexpr (!std::is_same_v<std::decay_t<AppliedWriteObserver>, std::nullptr_t>) {
            for (const SidWrite* w = wb; w != it; ++w) onAppliedWrite(*w);
        }
    }

    q.eraseIf([sliceEnd](const SidWrite& w) noexcept {
        return static_cast<int>(w.sampleOffset) < sliceEnd;
    });
}

inline void renderSidRegisterQueueToStereo(SidRegisterEngine& sidRegister,
                                           SidWriteQueue& q,
                                           float* left,
                                           float* right,
                                           int frames,
                                           int baseSampleOffset = 0) noexcept {
    renderSidRegisterQueueToStereo(sidRegister, q, left, right, frames, baseSampleOffset, SidNoopAppliedWriteObserver{});
}


inline void renderSidRegisterQueueToStereo(SidRegisterEngine& sidRegister,
                                           SidWriteQueue& q,
                                           float* left,
                                           float* right,
                                           int frames,
                                           int baseSampleOffset,
                                           std::nullptr_t) noexcept {
    renderSidRegisterQueueToStereo(sidRegister, q, left, right, frames, baseSampleOffset, SidNoopAppliedWriteObserver{});
}

} // namespace ArpSID
