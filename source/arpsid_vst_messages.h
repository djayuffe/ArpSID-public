// Copyright (C) 2024-2026 Ulf Bertilsson
// ArpSID — VST3 controller <-> processor message contract.
//
// Commands travel through the VST3 IConnectionPoint (IMessage), which hosts
// deliver on their main/UI thread, never inside process().
#pragma once

namespace ArpSID {

// Controller -> processor: load factory patch <slot> (0 .. slot max).
inline constexpr const char* kVstMsgLoadFactoryPatch = "ArpSID.LoadFactoryPatch";
inline constexpr const char* kVstMsgAttrSlot = "slot";

// Editor (via controller) -> processor: one short MIDI channel message from
// the on-screen keyboard (status, data1, data2).
inline constexpr const char* kVstMsgUiMidi = "ArpSID.UiMidi";
inline constexpr const char* kVstMsgAttrStatus = "status";
inline constexpr const char* kVstMsgAttrData1 = "data1";
inline constexpr const char* kVstMsgAttrData2 = "data2";

// Processor -> controller: address of the processor's Vst3KernelHost so the
// editor can read telemetry and edit the GUI models (SETTINGS/MIX/KIT/DIGI,
// SID file, C64 hub) directly, like the AU editor does through its adapter.
// Only valid when processor and controller share a process: the controller
// accepts it only if "pid" matches its own process id.
inline constexpr const char* kVstMsgKernelHost = "ArpSID.KernelHost";
inline constexpr const char* kVstMsgAttrHostPtr = "ptr";
inline constexpr const char* kVstMsgAttrPid = "pid";

// Controller -> processor: ask for kVstMsgKernelHost again (sent on connect,
// in case the processor connected first).
inline constexpr const char* kVstMsgRequestKernelHost = "ArpSID.RequestKernelHost";

} // namespace ArpSID
