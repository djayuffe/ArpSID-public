// factory_bank_c64_auth_v845_tests.cpp
//
// Guards the v845 factory-bank "true C64-auth" pass:
//   A. Internal ring/sync source oscillators must RUN (waveform bits present)
//      even when muted out of the mixer (V1<-V3, V2<-V1, V3<-V2).
//   B. Filter MODE is not LP-everywhere: BP / HP / multi-mode are actually used.
//   C. Both ring-mod and hard-sync are exercised by the factory bank, and the
//      cyclic source oscillators are authored.
//   D. Synth factory slots are register-authored (regSnap.valid=true); DrSID/
//      SID808/Digi slots keep their own engine payload authority.
//   E. SID-808 canonical slots 120..149 stay Drum-role + drSidMode + SID808
//      AnalogProjection context (no stale 120..124-only authority).
//
// These assert the factory bank uses the real SID 25-register vocabulary rather
// than the narrow safe subset it shipped before. Audit items 1-8, 10.

#include "arpsid/core/drum_context.h"
#include "arpsid/patchbank/forensic_patch_bank.h"
#include "../parameter_ids.h"
#include "../factory_patch_params.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

using namespace ArpSID;

namespace {
int g_failures = 0;
void check(bool ok, const char* msg) {
    if (!ok) { std::printf("FAIL: %s\n", msg); ++g_failures; }
}
int regByte(const std::array<float, static_cast<size_t>(kNumParams)>& p, ParamID id) {
    return std::clamp(static_cast<int>(std::lround(p[static_cast<size_t>(id)] * 255.0f)), 0, 255);
}
bool hasWaveform(int ctrl) { return (ctrl & 0xF0) != 0; } // any of NOISE/PULSE/SAW/TRI
} // namespace

int main() {
    const auto& defs = getFactoryPatchDefinitions();
    check(defs.size() == static_cast<size_t>(kFactoryPatchSlotCount), "factory bank has 180 slots");

    int lp = 0, bp = 0, hp = 0, multi = 0;
    int ringTotal = 0, syncTotal = 0;
    int v1ring = 0, v2ring = 0, v3ring = 0, v1sync = 0, v2sync = 0, v3sync = 0;
    int synthSlots = 0, synthRegSnap = 0, rawSnaps = 0;
    int synthNonLP = 0; // synth patches whose live filter mode is NOT plain LP

    for (int slot = 0; slot < kFactoryPatchSlotCount; ++slot) {
        std::array<float, static_cast<size_t>(kNumParams)> p{};
        check(loadFactoryPatchNormalizedParamsForSlot(slot, p), "slot params load");

        const int d404 = regByte(p, kParamSidRegD404);
        const int d40b = regByte(p, kParamSidRegD40B);
        const int d412 = regByte(p, kParamSidRegD412);
        const int d418 = regByte(p, kParamSidRegD418);

        // $D418 filter mode bits[6:4]: bit4=LP bit5=BP bit6=HP.
        const int modeBits = (d418 >> 4) & 0x07;
        const bool isLP = (modeBits & 0x01) != 0;
        const bool isBP = (modeBits & 0x02) != 0;
        const bool isHP = (modeBits & 0x04) != 0;
        const int nbits = (isLP ? 1 : 0) + (isBP ? 1 : 0) + (isHP ? 1 : 0);
        if (nbits >= 2) ++multi;
        else if (isHP)  ++hp;
        else if (isBP)  ++bp;
        else            ++lp;

        const PatchStaticState& s = defs[static_cast<size_t>(slot)].staticState;
        if (s.vco1Ring) ++v1ring;
        if (s.vco2Ring) ++v2ring;
        if (s.vco3Ring) ++v3ring;
        if (s.vco1Sync) ++v1sync;
        if (s.vco2Sync) ++v2sync;
        if (s.vco3Sync) ++v3sync;
        if (s.vco1Ring || s.vco2Ring || s.vco3Ring) ++ringTotal;
        if (s.vco1Sync || s.vco2Sync || s.vco3Sync) ++syncTotal;

        // A. Internal ring/sync sources must run. SID source topology is cyclic:
        //    V2 reads V1 ($D404), V3 reads V2 ($D40B), V1 reads V3 ($D412).
        if (s.vco2Ring || s.vco2Sync)
            check(hasWaveform(d404), "V1 control byte runs when V2 ring/syncs from it");
        if (s.vco3Ring || s.vco3Sync)
            check(hasWaveform(d40b), "V2 control byte runs when V3 ring/syncs from it");
        if (s.vco1Ring || s.vco1Sync)
            check(hasWaveform(d412), "V3 control byte runs when V1 ring/syncs from it");

        // D. Register authority by engine.
        if (s.regSnap.valid) {
            ++rawSnaps;
            check(s.regSnap.r.size() == 30, "valid regSnap holds 30 register bytes");
        }
        if (s.synthMode && !s.drSidMode) {
            ++synthSlots;
            if (s.regSnap.valid) ++synthRegSnap;
            // Synth patches render in SidRegister mode where $D418 mode is live
            // audio authority. They must keep the safe authored LP mode; a
            // non-LP-only mode would thin/hollow the tuned sound.
            const bool plainLP = isLP && !isBP && !isHP;
            if (!plainLP) {
                ++synthNonLP;
                std::printf("  synth slot %d has non-LP filter mode bits=%d (%s)\n",
                            slot, modeBits, defs[static_cast<size_t>(slot)].displayName.c_str());
            }
        }

        // E. SID-808 canonical range.
        if (slot >= 120 && slot <= 149) {
            check(factorySlotContext(slot) == DrumContext::SID808_AnalogProjection,
                  "slot 120..149 is SID808 AnalogProjection context");
            check(s.drSidMode, "slot 120..149 is drSidMode");
            check(defs[static_cast<size_t>(slot)].usage.role == PatchRole::Drum,
                  "slot 120..149 is Drum role");
        }
    }

    std::printf("[c64auth] filter modes  LP=%d BP=%d HP=%d multi=%d\n", lp, bp, hp, multi);
    std::printf("[c64auth] ring/sync     ringSlots=%d syncSlots=%d  v1r=%d v2r=%d v3r=%d v1s=%d v2s=%d v3s=%d\n",
                ringTotal, syncTotal, v1ring, v2ring, v3ring, v1sync, v2sync, v3sync);
    std::printf("[c64auth] reg authority synthSlots=%d synthRegSnap=%d rawSnaps=%d\n",
                synthSlots, synthRegSnap, rawSnaps);

    // B. Filter vocabulary is real, not LP-everywhere. Variety is expressed on the
    //    DrSID/SID808/Digi families (where $D418 is a cosmetic telemetry mirror),
    //    never by rewriting synth patches' live filter mode.
    check(lp > 70,    "LP mode is the common default");
    check(bp >= 8,    "band-pass mode is used by a meaningful set of patches");
    check(hp >= 8,    "high-pass mode is used by a meaningful set of patches");
    check(multi > 10, "multi (LP+BP+HP) mode is used by a meaningful set of patches");

    // B'. SidRegister-mode synth patches keep their authored LP audio authority:
    //     filter mode must not be silently rewritten to BP/HP and thin the sound.
    check(synthNonLP == 0, "synth (SidRegister) patches preserve authored LP filter mode");

    // C. Both ring-mod and hard-sync exist, with authored cyclic sources.
    check(ringTotal > 0, "factory bank exercises ring modulation");
    check(syncTotal > 0, "factory bank exercises hard sync");
    check(v1ring  > 0,   "V1 ring-mod patches exist (source V3 runs)");
    check(v2sync  > 0,   "V2 hard-sync patches exist (source V1 runs)");

    // D. Every synth factory slot is register-authored.
    check(synthSlots > 0, "factory bank has synth slots");
    check(synthRegSnap == synthSlots, "all synth factory slots are register-authored (regSnap.valid)");

    if (g_failures == 0) {
        std::printf("FactoryBankC64AuthV845Tests: PASS\n");
        return 0;
    }
    std::printf("FactoryBankC64AuthV845Tests: %d FAILURE(S)\n", g_failures);
    return 1;
}
