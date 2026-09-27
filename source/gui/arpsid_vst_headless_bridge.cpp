// Copyright (C) 2024-2026 Ulf Bertilsson
// ArpSID — Cocoa editor entry points on platforms without Cocoa.
//
// The native Cocoa editor exists only on macOS; elsewhere these report "no
// editor" so ArpSIDVSTGUIEditor falls back to the cross-platform editor.

#include "gui/arpsid_vst_cocoa_bridge.h"

namespace ArpSID {

void* arpsidOpenRichCocoaEditor(void*, void*) noexcept { return nullptr; }
void  arpsidCloseRichCocoaEditor(void*) noexcept {}
void  arpsidResizeRichCocoaEditor(void*, double, double) noexcept {}

} // namespace ArpSID
