// Copyright (C) 2024-2026 Ulf Bertilsson
// =============================================================================
// ArpSID — IPluginFactory3-compliant factory
// Version synchronized through include/arpsid/version.h
//
// Steinberg spec notes:
// Processor: kNoDistributable (=0) — instrument with live audio state cannot
// be distributed across network nodes. Sub-category: ARPSID_PLUGIN_CATEGORY.
// Controller: 0 flags, no subcategory — UI component is not distributable.
// Factory info: kUnicode flag set.
// Class FUIDs must match moduleinfo.json exactly (same hex strings).
// =============================================================================

#include "pluginterfaces/base/funknown.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/vsttypes.h"

#include "arpsid_processor_phase2.h"
#include "arpsid/version.h"
#include "arpsid_controller.cpp"   // single-TU build; #include is intentional

using namespace Steinberg;
using namespace Steinberg::Vst;

// =============================================================================
// IPluginFactory3 implementation via SDK macro
// BEGIN_FACTORY_DEF opens the class list and uses the metadata macros from
// include/arpsid/version.h directly. Keeping only one source of truth avoids
// stale parallel PFactoryInfo/PClassInfo objects.
// =============================================================================
BEGIN_FACTORY_DEF(
    ARPSID_PLUGIN_VENDOR,
    ARPSID_PLUGIN_URL,
    ARPSID_PLUGIN_EMAIL
)

    DEF_CLASS2(
        INLINE_UID(0xA1B2C3D4u, 0xE5F60718u, 0x9A0B1C2Du, 0x3E4F5A6Bu),
        PClassInfo::kManyInstances,
        kVstAudioEffectClass,
        "ArpSID",
        0,                              // kNoDistributable
        ARPSID_PLUGIN_CATEGORY,
        ARPSID_PLUGIN_VERSION,
        kVstVersionString,
        ArpSID::ArpSIDProcessorPhase2::createInstance
    )

    DEF_CLASS2(
        INLINE_UID(0xB2C3D4E5u, 0xF6071829u, 0xA0B1C2D3u, 0xE4F5A6B7u),
        PClassInfo::kManyInstances,
        kVstComponentControllerClass,
        "ArpSID Controller",
        0,
        "",
        ARPSID_PLUGIN_VERSION,
        kVstVersionString,
        ArpSID::ArpSIDControllerPhase3::createInstance
    )

END_FACTORY
