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
    using namespace ArpSID::GUI;

    DrSidEngine dr;
    dr.setSampleRate(48000.0);
    const std::uint8_t mask = static_cast<std::uint8_t>(kKitVoiceOverrideWaveform |
                                                        kKitVoiceOverrideAttackDecay |
                                                        kKitVoiceOverrideSustainRelease |
                                                        kKitVoiceOverridePulseWidth |
                                                        kKitVoiceOverrideFlags);
    dr.triggerKitMidiNote(38, 0.9f, static_cast<std::uint16_t>(kDrSidNewFactoryRange.first),
                          true, kKitVoiceWavePul, 0x24u, 0x86u, 0x0555u, 0x03u, mask);

    require(dr.lastKitAppliedVoiceIndex() == 1u, "snare maps to DrSID voice 1");
    const std::uint8_t ctrl = dr.lastKitAppliedWaveformControl();
    require((ctrl & 0x40u) != 0u, "register mirror contains pulse waveform");
    require((ctrl & 0x04u) != 0u, "register mirror contains ring bit");
    require((ctrl & 0x02u) != 0u, "register mirror contains sync bit");
    require((ctrl & 0x01u) != 0u, "register mirror contains gate bit");

    std::cout << "DrSidRegisterImageRingSyncV662Tests PASS\n";
    return 0;
}
