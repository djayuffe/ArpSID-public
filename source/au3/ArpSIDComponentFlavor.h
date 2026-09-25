#pragma once

#include <cstddef>
#include <cstdint>

#if defined(__APPLE__)
#include <AudioToolbox/AudioToolbox.h>
#else
using OSType = uint32_t;
struct AudioComponentDescription {
    OSType componentType = 0;
    OSType componentSubType = 0;
    OSType componentManufacturer = 0;
    uint32_t componentFlags = 0;
    uint32_t componentFlagsMask = 0;
};
#endif

namespace ArpSID {

enum class ComponentFlavor : uint8_t {
    Hybrid = 0,
    Instrument = 1,
    DrumMachine = 2,
    Sid808 = 3,
    C64SidPlayer = 4
};

inline constexpr int kComponentFlavorCount = 5;
inline constexpr int kComponentFlavorMinRaw = 0;
inline constexpr int kComponentFlavorMaxRaw = kComponentFlavorCount - 1;

inline constexpr ComponentFlavor componentFlavorFromRaw(int raw) noexcept {
    return (raw >= kComponentFlavorMinRaw && raw <= kComponentFlavorMaxRaw)
        ? static_cast<ComponentFlavor>(raw)
        : ComponentFlavor::Hybrid;
}

inline constexpr size_t componentFlavorIndex(ComponentFlavor flavor) noexcept {
    const int raw = static_cast<int>(flavor);
    return (raw >= kComponentFlavorMinRaw && raw <= kComponentFlavorMaxRaw)
        ? static_cast<size_t>(raw)
        : static_cast<size_t>(0);
}

constexpr OSType makeArpSIDFourCC(char a, char b, char c, char d) noexcept {
    return (static_cast<OSType>(static_cast<unsigned char>(a)) << 24) |
           (static_cast<OSType>(static_cast<unsigned char>(b)) << 16) |
           (static_cast<OSType>(static_cast<unsigned char>(c)) << 8)  |
            static_cast<OSType>(static_cast<unsigned char>(d));
}

static constexpr OSType kComponentManufacturer = makeArpSIDFourCC('A','S','I','D');
static constexpr OSType kMusicDeviceComponentType = makeArpSIDFourCC('a','u','m','u');
static constexpr OSType kHybridComponentSubType = makeArpSIDFourCC('A','r','p','S');
static constexpr OSType kInstrumentComponentSubType = makeArpSIDFourCC('A','r','I','n');
static constexpr OSType kDrumMachineComponentSubType = makeArpSIDFourCC('D','r','S','D');
static constexpr OSType kSid808ComponentSubType = makeArpSIDFourCC('S','8','0','8');
static constexpr OSType kC64SidPlayerComponentSubType = makeArpSIDFourCC('C','6','4','P');

inline constexpr bool isSupportedComponentDescription(const AudioComponentDescription& desc) noexcept {
    if (desc.componentManufacturer != kComponentManufacturer) return false;
    if (desc.componentType != kMusicDeviceComponentType) return false;
    return desc.componentSubType == kHybridComponentSubType ||
           desc.componentSubType == kInstrumentComponentSubType ||
           desc.componentSubType == kDrumMachineComponentSubType ||
           desc.componentSubType == kSid808ComponentSubType ||
           desc.componentSubType == kC64SidPlayerComponentSubType;
}

inline constexpr ComponentFlavor componentFlavorFromDescription(const AudioComponentDescription& desc) noexcept {
    switch (desc.componentSubType) {
        case kInstrumentComponentSubType:  return ComponentFlavor::Instrument;
        case kDrumMachineComponentSubType: return ComponentFlavor::DrumMachine;
        case kSid808ComponentSubType:      return ComponentFlavor::Sid808;
        case kC64SidPlayerComponentSubType: return ComponentFlavor::C64SidPlayer;
        case kHybridComponentSubType:
        default:                           return ComponentFlavor::Hybrid;
    }
}

inline constexpr const char* componentFlavorDisplayName(ComponentFlavor flavor) noexcept {
    switch (flavor) {
        case ComponentFlavor::Instrument:  return "Instrument";
        case ComponentFlavor::DrumMachine: return "Drum Machine";
        case ComponentFlavor::Sid808:      return "SID-808";
        case ComponentFlavor::C64SidPlayer: return "C64 SID Player";
        case ComponentFlavor::Hybrid:
        default:                          return "Classic";
    }
}

inline constexpr bool componentFlavorAllowsDrSid(ComponentFlavor flavor) noexcept {
    return flavor != ComponentFlavor::Instrument && flavor != ComponentFlavor::C64SidPlayer;
}

inline constexpr bool componentFlavorIsDedicatedDrum(ComponentFlavor flavor) noexcept {
    return flavor == ComponentFlavor::DrumMachine || flavor == ComponentFlavor::Sid808;
}

inline constexpr bool componentFlavorForcesDrSid(ComponentFlavor flavor) noexcept {
    return componentFlavorIsDedicatedDrum(flavor);
}

inline constexpr bool componentFlavorForcesAnalogSid808(ComponentFlavor flavor) noexcept {
    return flavor == ComponentFlavor::Sid808;
}

inline constexpr bool componentFlavorIsC64SidPlayer(ComponentFlavor flavor) noexcept {
    return flavor == ComponentFlavor::C64SidPlayer;
}

} // namespace ArpSID
