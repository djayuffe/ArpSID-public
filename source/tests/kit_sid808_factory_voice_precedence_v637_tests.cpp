#include "arpsid/gui/kit_voice_config.h"
#include "arpsid/engines/drum_engine_router.h"
#include "arpsid/core/drum_context.h"
#include <cstdlib>
#include <iostream>

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

int main() {
    using namespace ArpSID;
    using namespace ArpSID::GUI;

    KitVoiceConfig def = makeDefaultKitVoiceConfig();
    require(kitVoiceRuntimeOverrideMask(def) == 0u,
            "untouched default KIT voice must not mask selected SID808 factory slot");

    KitVoiceConfig edited = def;
    kitVoiceSetWaveform(edited, kKitVoiceWavePul);
    require((kitVoiceRuntimeOverrideMask(edited) & kKitVoiceOverrideWaveform) != 0u,
            "edited waveform sets runtime override mask");

    Sid808Engine s8;
    DrSidEngine dr;
    DrumEngineRouter router(dr, s8);
    router.setActiveIdentityFromFactorySlot(static_cast<int>(kSid808NewFactoryRange.first));

    Sid808HitOverride ov{};
    ov.selectedFactorySlot = static_cast<std::uint16_t>(kSid808NewFactoryRange.first + 1u);
    ov.hasSelectedFactorySlot = true;
    // No waveform/ADSR/PW/flags present: selected factory slot must supply full base config.
    router.noteOnWithOverride(SidGMDrumClass::Kick, 80u, 36u, &ov);
    const Sid808VoiceConfig fromFactory = factorySid808ResolvedKitForSlot(
        static_cast<int>(kSid808NewFactoryRange.first + 1u))[static_cast<std::size_t>(Sid808Drum::Kick)];
    const Sid808VoiceConfig applied = s8.lastAppliedConfig();
    require(applied.waveform == fromFactory.waveform, "factory slot supplies waveform when KIT voice is default");
    require(applied.attackDecay == fromFactory.attackDecay, "factory slot supplies AD");
    require(applied.sustainRelease == fromFactory.sustainRelease, "factory slot supplies SR");
    require(applied.pulseWidth == fromFactory.pulseWidth, "factory slot supplies PW");
    require(applied.flags == fromFactory.flags, "factory slot supplies flags");

    Sid808HitOverride editedOv = ov;
    editedOv.waveform = 0x40u;
    editedOv.hasWaveform = true;
    router.noteOnWithOverride(SidGMDrumClass::Kick, 80u, 36u, &editedOv);
    require(s8.lastAppliedConfig().waveform == 0x40u,
            "explicit KIT voice override wins over selected factory slot");

    std::cout << "KitSid808FactoryVoicePrecedenceV637Tests PASS\n";
    return 0;
}
