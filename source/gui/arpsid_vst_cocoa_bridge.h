// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once

namespace Steinberg { class IPlugView; }

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
void arpsidControllerSelectFactoryPatch(void* editController, int slot) noexcept;
int  arpsidControllerLoadedFactorySlot(void* editController) noexcept;
// Editor size (user zoom, 1.0 = 1200 x 800 at host scale 1) kept by the
// controller so a reopened editor keeps the size the user chose.
double arpsidControllerEditorZoom(void* editController) noexcept;
void arpsidControllerSetEditorZoom(void* editController, double zoom) noexcept;
void arpsidControllerSendUiMidi(void* editController, unsigned char status, unsigned char data1,
                                unsigned char data2) noexcept;
// The cross-platform (VSTGUI) editor view; nullptr where it is not built.
Steinberg::IPlugView* arpsidCreateCrossPlatformEditor(void* editController);

} // namespace ArpSID
