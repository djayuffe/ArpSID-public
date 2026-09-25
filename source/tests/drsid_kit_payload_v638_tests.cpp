#include "arpsid/engines/drsid_engine.h"
#include "arpsid/core/drum_context.h"
#include "arpsid/gui/kit_voice_config.h"
#include <cstdlib>
#include <iostream>

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

int main() {
    using namespace ArpSID;
    DrSidEngine dr;
    const auto selected = static_cast<std::uint16_t>(kDrSidNewFactoryRange.first + 4u);
    dr.triggerKitMidiNote(38, 0.8f, selected, true, 0x40u, 0xA3u, 0xB4u, 0x0777u, 0x03u, 0x1Fu);
    require(dr.kitTriggerCount() == 1u, "KIT DrSID trigger count increments");
    require(dr.lastKitHasSelectedFactorySlot(), "DrSID records selected factory slot presence");
    require(dr.lastKitSelectedFactorySlot() == selected, "DrSID records selected factory slot");
    require(dr.lastKitVoiceWaveform() == 0x40u, "DrSID records KIT waveform payload");
    require(dr.lastKitVoiceAttackDecay() == 0xA3u, "DrSID records KIT AD payload");
    require(dr.lastKitVoiceSustainRelease() == 0xB4u, "DrSID records KIT SR payload");
    require(dr.lastKitVoicePulseWidth() == 0x0777u, "DrSID records KIT PW payload");
    require(dr.lastKitVoiceFlags() == 0x03u, "DrSID records KIT flags payload");
    require(dr.lastKitVoiceOverrideMask() == 0x1Fu, "DrSID records KIT override mask payload");
    require(dr.lastGMDrumNote() == 38, "DrSID still triggers audible GM note path");
    std::cout << "DrSidKitPayloadV638Tests PASS\n";
    return 0;
}
