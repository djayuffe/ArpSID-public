// Copyright (C) 2024-2026 Ulf Bertilsson
// ArpSID — VST3 editor bridge for platforms without the native Cocoa GUI.
//
// The rich editor is macOS-only (source/gui/arpsid_vst_cocoa_bridge.mm).
// Elsewhere ArpSIDVSTGUIEditor reports no supported platform view, so hosts
// fall back to their generic parameter UI and these editor hooks are never
// reached with a live handle. The telemetry provider registry is still real:
// the Phase2 processor registers itself here on every platform.

#include "gui/arpsid_vst_cocoa_bridge.h"

#include <atomic>

namespace ArpSID {

static std::atomic<IArpSIDTelemetryProvider*> gActiveTelemetryProvider { nullptr };

void arpsidSetActiveTelemetryProvider(IArpSIDTelemetryProvider* provider) noexcept {
    gActiveTelemetryProvider.store(provider, std::memory_order_release);
}

IArpSIDTelemetryProvider* arpsidGetActiveTelemetryProvider() noexcept {
    return gActiveTelemetryProvider.load(std::memory_order_acquire);
}

void* arpsidOpenRichCocoaEditor(void*, void*) noexcept { return nullptr; }
void  arpsidCloseRichCocoaEditor(void*) noexcept {}
void  arpsidResizeRichCocoaEditor(void*, double, double) noexcept {}

} // namespace ArpSID
