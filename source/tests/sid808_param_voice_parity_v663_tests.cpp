#include "factory_patch_params.h"
#include "arpsid/patchbank/factory_sid808_param_bridge.h"
#include "parameter_ids.h"
#include <cstdlib>
#include <iostream>

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}
static bool near(float a, float b) {
    const float d = a - b;
    return d > -0.0001f && d < 0.0001f;
}

int main() {
    using namespace ArpSID;
    for (int slot = 120; slot <= 149; ++slot) {
        std::array<float, static_cast<std::size_t>(kNumParams)> params{};
        require(loadFactoryPatchNormalizedParamsForSlot(slot, params), "SID808 params load");
        const Sid808FactoryParamSignature sig = factorySid808ParamSignatureForSlot(slot);
        require(near(params[static_cast<std::size_t>(kParamDrSidVolume)], sig.volume), "volume from Sid808VoiceConfig signature");
        require(near(params[static_cast<std::size_t>(kParamDrSidKickTune)], sig.kickTune), "kick tune from Sid808VoiceConfig signature");
        require(near(params[static_cast<std::size_t>(kParamDrSidKickDecay)], sig.kickDecay), "kick decay from Sid808VoiceConfig signature");
        require(near(params[static_cast<std::size_t>(kParamDrSidSnareTone)], sig.snareTone), "snare tone from Sid808VoiceConfig signature");
        require(near(params[static_cast<std::size_t>(kParamDrSidHatMetal)], sig.hatMetal), "hat metal from Sid808VoiceConfig signature");
        require(near(params[static_cast<std::size_t>(kParamDrSidClapSpread)], sig.clapSpread), "clap spread from Sid808VoiceConfig signature");
    }
    std::cout << "Sid808ParamVoiceParityV663Tests PASS\n";
    return 0;
}
