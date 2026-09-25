#pragma once

namespace ArpSID {

// Returns an opaque handle when a native Cocoa editor was attached successfully.
// Returns nullptr when the platform is unsupported or attach failed.
void* arpsidOpenRichCocoaEditor(void* parentNSView, void* editController) noexcept;
void  arpsidCloseRichCocoaEditor(void* handle) noexcept;
void  arpsidResizeRichCocoaEditor(void* handle, double width, double height) noexcept;

// Telemetry registry shared between the VST3 processor and the rich Cocoa editor.
struct IArpSIDTelemetryProvider;
void  arpsidSetActiveTelemetryProvider(IArpSIDTelemetryProvider* provider) noexcept;
IArpSIDTelemetryProvider* arpsidGetActiveTelemetryProvider() noexcept;

} // namespace ArpSID
