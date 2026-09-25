// plugin_ids.cpp
// ArpSID — FUID / GUID definitions
//
// These must match the INLINE_UID values in factory.cpp and the hex strings
// in resources/moduleinfo.json.in exactly. They are the stable identity of
// the plug-in across hosts, sessions, and platform versions.
//
// ProcessorUID: A1B2C3D4-E5F6-0718-9A0B-1C2D3E4F5A6B
// ControllerUID: B2C3D4E5-F607-1829-A0B1-C2D3E4F5A6B7
//
// Copyright (c) 2024 ArpSID Project. All rights reserved.
// SPDX-License-Identifier: MIT

#include "pluginterfaces/base/funknown.h"
#include "arpsid_processor_phase2.h"

namespace ArpSID {

// ProcessorUID — matches DEF_CLASS2 first INLINE_UID in factory.cpp
const Steinberg::FUID ProcessorUID(
    0xA1B2C3D4u, 0xE5F60718u, 0x9A0B1C2Du, 0x3E4F5A6Bu);

// ControllerUID — matches DEF_CLASS2 second INLINE_UID in factory.cpp
const Steinberg::FUID ControllerUID(
    0xB2C3D4E5u, 0xF6071829u, 0xA0B1C2D3u, 0xE4F5A6B7u);

} // namespace ArpSID
