// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once
#include "sid_runtime_primitive_surface.h"
#include "sid_event_queue.h"

namespace ArpSID {

class SidRuntimeBackend : public SidRuntimePrimitiveSurface {
public:
    ~SidRuntimeBackend() override = default;
    virtual void dispatchCanonicalEvent(const SidTimedEvent& ev) noexcept override = 0;
    // Render-owned generators (currently the arpeggiator) append their exact
    // sample-stamped gates before the one final queue sort/dispatch.
    virtual void appendGeneratedTimedEvents(int frameCount, SidTimedEventQueue& queue) noexcept {
        (void)frameCount; (void)queue;
    }
    virtual void finishGeneratedTimedEvents() noexcept {}
    virtual void renderCanonicalSlice(int offset, int frames) noexcept override = 0;
    virtual uint16_t estimatedCyclesPerHostSample() const noexcept = 0;
    virtual double physicalSampleRateHz() const noexcept { return 0.0; }
    virtual double physicalSidClockHz() const noexcept { return 0.0; }
};

} // namespace ArpSID
