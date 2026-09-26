// Copyright (C) 2024-2026 Ulf Bertilsson
// Factory-bank audio closure: by default this is a bounded CI/closure audit that
// verifies all factory slots structurally and audio-renders a deterministic
// representative set covering synth, DrSID/drum, SID-808 and edge slots.  Set
// ARPSID_FACTORY_BANK_FULL_AUDIO_AUDIT=1 to render every slot for the slower
// exhaustive release soak.
#include "au3/ArpSIDDSPKernel.hpp"
#include "factory_patch_params.h"
#include "arpsid/patchbank/forensic_patch_bank.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <set>
#include <vector>

using namespace ArpSID;

namespace {

static bool envFullAudit() {
    const char* v = std::getenv("ARPSID_FACTORY_BANK_FULL_AUDIO_AUDIT");
    return v && (v[0] == '1' || v[0] == 'y' || v[0] == 'Y' || v[0] == 't' || v[0] == 'T');
}

static double blockRms(ArpSIDDSPKernel& k, TransportState& t) {
    constexpr int N = 256;
    std::array<float, N> l{}, r{};
    float* o[2] = {l.data(), r.data()};
    k.processBlock(o, 2, N, nullptr, 0, t);
    double s = 0.0;
    for (int i = 0; i < N; ++i) {
        const double v = 0.5 * (static_cast<double>(l[(size_t)i]) + static_cast<double>(r[(size_t)i]));
        s += v * v;
    }
    return std::sqrt(s / N);
}

static int noteForSlot(const PatchDefinition* def, const bool drum) {
    if (drum) return 36;
    int note = 60;
    if (def) {
        note = std::clamp(60, def->usage.validMidiMin, def->usage.validMidiMax);
        if (note < def->usage.preferredMidiMin || note > def->usage.preferredMidiMax) {
            note = std::clamp((def->usage.preferredMidiMin + def->usage.preferredMidiMax) / 2,
                              def->usage.validMidiMin,
                              def->usage.validMidiMax);
        }
    }
    return note;
}

static void addSlot(std::set<int>& slots, int slot) {
    if (slot >= 0 && slot < kFactoryPatchSlotCount) slots.insert(slot);
}

static std::vector<int> representativeSlots() {
    std::set<int> out;
    addSlot(out, 0);
    addSlot(out, kFactoryPatchSlotCount - 1);

    bool sawDrum = false;
    bool sawSynth = false;
    bool sawSid808 = false;
    bool sawDigi = false;
    bool sawBass = false;
    bool sawLead = false;
    bool sawPad = false;

    for (int slot = 0; slot < kFactoryPatchSlotCount; ++slot) {
        const PatchDefinition* def = getFactoryPatchDefinition(slot);
        if (!def) continue;
        const auto role = def->usage.role;
        if (!sawSynth && !def->staticState.drSidMode) { addSlot(out, slot); sawSynth = true; }
        if (!sawDrum && (def->staticState.drSidMode || role == PatchRole::Drum)) { addSlot(out, slot); sawDrum = true; }
        if (!sawBass && role == PatchRole::Bass) { addSlot(out, slot); sawBass = true; }
        if (!sawLead && role == PatchRole::Lead) { addSlot(out, slot); sawLead = true; }
        if (!sawPad && role == PatchRole::PadIllusion) { addSlot(out, slot); sawPad = true; }
        // Current factory metadata has evolved over many passes.  Keep these as
        // broad integration sentinels without requiring every role to exist.
        const std::string name = def->displayName;
        if (!sawSid808 && (name.find("808") != std::string::npos || name.find("SID-808") != std::string::npos)) {
            addSlot(out, slot); sawSid808 = true;
        }
        if (!sawDigi && (name.find("Digi") != std::string::npos || name.find("DIGI") != std::string::npos)) {
            addSlot(out, slot); sawDigi = true;
        }
        if (sawSynth && sawDrum && sawBass && sawLead && sawPad && sawSid808 && sawDigi) break;
    }

    // Spread probes across the whole canonical 180-slot bank so root creation,
    // slot identity and bank assignment regressions are not hidden in slot 0.
    for (int slot = 0; slot < kFactoryPatchSlotCount; slot += 16) addSlot(out, slot);
    for (int slot = 15; slot < kFactoryPatchSlotCount; slot += 32) addSlot(out, slot);

    return std::vector<int>(out.begin(), out.end());
}

static bool validateSlotStateRoot(int slot) {
    const PatchDefinition* def = getFactoryPatchDefinition(slot);
    if (!def) {
        std::printf("[MISSING] slot %3d has no factory definition\n", slot);
        return false;
    }
    SidStateRootV1 root = makeFactoryPatchStateRootForSlot(slot);
    const float bankSlot = sidStateRootParamValue(root, kParamBankSlot);
    const int decodedSlot = std::clamp(static_cast<int>(std::lround(bankSlot * static_cast<float>(kFactoryPatchSlotCount - 1))),
                                       0,
                                       kFactoryPatchSlotCount - 1);
    if (decodedSlot != slot) {
        std::printf("[ROOT  ] slot %3d %-26s bankSlot=%.3f decoded=%d\n", slot, def->displayName.c_str(), bankSlot, decodedSlot);
        return false;
    }
    return true;
}

static bool auditAudioSlot(int slot, const bool fullMode) {
    const PatchDefinition* def = getFactoryPatchDefinition(slot);
    if (!def) return false;
    const bool drum = def->staticState.drSidMode || def->usage.role == PatchRole::Drum;
    const uint8_t ch = drum ? 9 : 0;
    const int note = noteForSlot(def, drum);

    auto k = std::make_unique<ArpSIDDSPKernel>();
    k->setup(48000.0, 256);
    std::array<float, kNumParams> p{};
    SidStateRootV1 root = makeFactoryPatchStateRootForSlot(slot);
    for (int i = 0; i < kNumParams; ++i) p[(size_t)i] = sidStateRootParamValue(root, i);
    k->resetPreservingHostParameterSnapshot(p.data(), kNumParams, slot, true);

    TransportState t{};
    t.isPlaying = false;
    t.playStateKnown = false;
    t.sampleRate = 48000.0;
    t.frameCount = 256;

    const uint8_t on[3] = {static_cast<uint8_t>(0x90 | ch), static_cast<uint8_t>(note), 110};
    k->enqueueMidiIntent(on, 3, 0, 0);

    const int attackBlocks = fullMode ? 160 : 48;
    double held = 0.0;
    for (int b = 0; b < attackBlocks; ++b) {
        held = std::max(held, blockRms(*k, t));
        if (b >= 4 && held > 3.0e-4) break;
    }

    const bool oneShot = drum || def->usage.role == PatchRole::Drum;
    double tail = 0.0;
    if (!oneShot) {
        const uint8_t off[3] = {static_cast<uint8_t>(0x80 | ch), static_cast<uint8_t>(note), 0};
        k->enqueueMidiIntent(off, 3, 0, 0);
        const int releaseBlocks = fullMode ? 260 : 96;
        for (int b = 0; b < releaseBlocks; ++b) {
            tail = blockRms(*k, t);
            if (b >= 12 && (tail <= held * 0.25 || tail <= 2.0e-3)) break;
        }
    }

    const bool isSilent = (held <= 1.0e-4);
    const bool isStuck = (!oneShot && !isSilent && tail > held * 0.25 && tail > 2.0e-3);
    if (isSilent || isStuck) {
        std::printf("[%s] slot %3d  %-26s role=%d synth=%d drsid=%d  ch%d n%d  held=%.5f tail=%.5f\n",
                    isSilent ? "SILENT" : "STUCK ", slot,
                    def->displayName.c_str(),
                    static_cast<int>(def->usage.role),
                    static_cast<int>(def->staticState.synthMode),
                    static_cast<int>(def->staticState.drSidMode),
                    ch, note, held, tail);
        return false;
    }
    return true;
}

} // namespace

int main() {
    prewarmAllSidTables();

    int structuralBad = 0;
    for (int slot = 0; slot < kFactoryPatchSlotCount; ++slot) {
        if (!validateSlotStateRoot(slot)) ++structuralBad;
    }

    const bool fullMode = envFullAudit();
    std::vector<int> audioSlots;
    if (fullMode) {
        for (int slot = 0; slot < kFactoryPatchSlotCount; ++slot) audioSlots.push_back(slot);
    } else {
        audioSlots = representativeSlots();
    }

    int audioBad = 0;
    for (int slot : audioSlots) {
        if (!auditAudioSlot(slot, fullMode)) ++audioBad;
    }

    std::printf("\nFACTORY BANK AUDIT: structural=%d slots | audio=%zu slots | structuralBad=%d audioBad=%d mode=%s\n",
                kFactoryPatchSlotCount,
                audioSlots.size(),
                structuralBad,
                audioBad,
                fullMode ? "full" : "bounded-closure");
    return (structuralBad || audioBad) ? 1 : 0;
}
