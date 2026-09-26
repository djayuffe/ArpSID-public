// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once
#include "sid_event_queue.h"

namespace ArpSID {

class SidRuntimeSurface {
public:
    virtual ~SidRuntimeSurface() = default;
    virtual void dispatchCanonicalEvent(const SidTimedEvent& ev) noexcept = 0;
    virtual void renderCanonicalSlice(int offset, int frames) noexcept = 0;
    virtual bool supportsFractionalSubSampleSpans() const noexcept { return false; }
    virtual void renderCanonicalSubSampleSpan(int sampleOffset, uint16_t cycleStart, uint16_t cycleEnd) noexcept { (void)sampleOffset; (void)cycleStart; (void)cycleEnd; }
    virtual void renderCanonicalSubPhaseSpan(int sampleOffset, uint16_t cycleIndex, uint16_t subphaseStart, uint16_t subphaseEnd) noexcept { (void)sampleOffset; (void)cycleIndex; (void)subphaseStart; (void)subphaseEnd; }
};

} // namespace ArpSID
