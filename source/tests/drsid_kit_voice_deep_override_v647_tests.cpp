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
    const auto slot = static_cast<std::uint16_t>(kDrSidNewFactoryRange.first + 3u);

    const std::uint8_t wave = kKitVoiceWavePul;
    const std::uint8_t ad = 0x2Bu;
    const std::uint8_t sr = 0xC6u;
    const std::uint16_t pw = 0x0555u;
    const std::uint8_t flags = 0x07u;
    const std::uint8_t mask = static_cast<std::uint8_t>(kKitVoiceOverrideWaveform |
                                                        kKitVoiceOverrideAttackDecay |
                                                        kKitVoiceOverrideSustainRelease |
                                                        kKitVoiceOverridePulseWidth |
                                                        kKitVoiceOverrideFlags);

    dr.triggerKitMidiNote(38, 0.9f, slot, true, wave, ad, sr, pw, flags, mask);

    require(dr.kitTriggerCount() == 1u, "KIT DrSID trigger fired");
    require(dr.kitVoiceOverrideAppliedCount() == 1u, "KIT voice override was applied to synthesis");
    require(dr.lastKitAppliedVoiceIndex() == 1u, "snare uses DrSID voice 1");
    require((dr.lastKitAppliedWaveformControl() & 0x40u) != 0u, "pulse waveform reached SID control register");
    require(dr.lastKitAppliedAttackDecay() == ad, "attack/decay reached SID ADSR register");
    require(dr.lastKitAppliedSustainRelease() == sr, "sustain/release reached SID ADSR register");
    require(dr.lastKitAppliedPulseWidth() == pw, "pulse width reached SID pulse-width register");
    require((dr.lastKitAppliedFlags() & 0x07u) == 0x07u, "ring/sync/filter flags recorded as applied");

    std::cout << "DrSidKitVoiceDeepOverrideV647Tests PASS\n";
    return 0;
}
