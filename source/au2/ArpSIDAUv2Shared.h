#pragma once

#include <AudioToolbox/AudioToolbox.h>
#include "arpsid/version.h"
#include "ArpSIDComponentFlavor.h"

#ifndef ARPSID_AUDIOCOMPONENT_VERSION
#define ARPSID_AUDIOCOMPONENT_VERSION \
    ((ARPSID_PLUGIN_VERSION_MAJOR * 10000u) + \
     (ARPSID_PLUGIN_VERSION_MINOR * 100u) + \
      ARPSID_PLUGIN_VERSION_PATCH)
#endif

namespace ArpSIDAUv2 {

static constexpr OSType kComponentType = ArpSID::kMusicDeviceComponentType;
static constexpr OSType kComponentSubType = ArpSID::kHybridComponentSubType;
static constexpr OSType kComponentManufacturer = ArpSID::kComponentManufacturer;
static constexpr UInt32 kComponentVersion = ARPSID_AUDIOCOMPONENT_VERSION;

static constexpr AudioUnitPropertyID kPropertyBridgeObject = 64000;
static constexpr AudioUnitPropertyID kArpSIDPropertyPsidData = 65001;

static constexpr const char* kFactoryFunctionName = "ArpSIDAUv2Factory";
static constexpr const char* kCocoaViewFactoryClassName = "ArpSIDAUv2ViewFactory";

inline constexpr bool isSupportedComponentDescription(const AudioComponentDescription& desc) noexcept {
    return ArpSID::isSupportedComponentDescription(desc);
}

} // namespace ArpSIDAUv2
