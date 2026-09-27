// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once

namespace ArpSID {

class Vst3KernelHost;

// Returns an opaque handle when a native Cocoa editor was attached successfully.
// Returns nullptr when the platform is unsupported or attach failed.
void* arpsidOpenRichCocoaEditor(void* parentNSView, void* editController) noexcept;
void  arpsidCloseRichCocoaEditor(void* handle) noexcept;
void  arpsidResizeRichCocoaEditor(void* handle, double width, double height) noexcept;

// Implemented by the edit controller (arpsid_controller.cpp): the processor's
// kernel host when it runs in this process (else nullptr), and the "state
// changed without a parameter change" notification for model edits.
Vst3KernelHost* arpsidControllerKernelHost(void* editController) noexcept;
void arpsidControllerMarkStateDirty(void* editController) noexcept;

} // namespace ArpSID
