#include "au3/ArpSIDDSPKernel.hpp"
#include "factory_patch_params.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

static void requireNear(double a, double b, const char* msg, double eps) {
    if (!std::isfinite(a) || !std::isfinite(b) || std::fabs(a - b) > eps) {
        std::cerr << "FAIL: " << msg << " got=" << a << " expected=" << b << " eps=" << eps << "\n";
        std::exit(1);
    }
}

struct Fingerprint {
    double rms = 0.0;
    double peak = 0.0;
    double sum = 0.0;
    double weighted = 0.0;
};

static void applyEditedSnapshot(ArpSID::ArpSIDDSPKernel& k,
                                int slot,
                                const std::array<float, ArpSID::kNumParams>& snapshot,
                                bool poisonMetadata) {
    std::array<float, ArpSID::kNumParams> snap = snapshot;
    if (poisonMetadata) {
        snap[(size_t)ArpSID::kParamProgram] = 0.0f;
        snap[(size_t)ArpSID::kParamBankSlot] = 0.0f;
        snap[(size_t)ArpSID::kParamVirtualGate] = 1.0f;
        snap[(size_t)ArpSID::kParamVirtualNote] = 1.0f;
    }
    k.resetPreservingHostParameterSnapshot(snap.data(), ArpSID::kNumParams, slot, true);
}

static Fingerprint renderKickFingerprint(ArpSID::ArpSIDDSPKernel& k, int note = 36) {
    using namespace ArpSID;
    constexpr int frames = 1024;
    std::array<float, frames> l{};
    std::array<float, frames> r{};
    float* outs[2] = {l.data(), r.data()};
    TimedEvent ev{};
    ev.sampleOffset = 0;
    ev.kind = EventKind::NoteOn;
    ev.channel = 9;
    ev.pitch = (int16_t)note;
    ev.value = 1.0f;
    TransportState t{};
    t.isPlaying = true;
    t.playStateKnown = true;
    t.sampleRate = 48000.0;
    t.frameCount = frames;
    k.processBlock(outs, 2, frames, &ev, 1, t);

    Fingerprint fp{};
    for (int i = 0; i < frames; ++i) {
        const double v = 0.5 * (static_cast<double>(l[(size_t)i]) + static_cast<double>(r[(size_t)i]));
        require(std::isfinite(v), "rendered DrSID sample must be finite");
        fp.sum += v;
        fp.weighted += v * static_cast<double>(i + 1);
        fp.rms += v * v;
        fp.peak = std::max(fp.peak, std::fabs(v));
    }
    fp.rms = std::sqrt(fp.rms / static_cast<double>(frames));
    return fp;
}

int main() {
    using namespace ArpSID;
    prewarmAllSidTables();

    constexpr int slot = 120;
    SidStateRootV1 root = makeFactoryPatchStateRootForSlot(slot);
    require(root.valid(), "slot 120 root must be valid");

    std::array<float, kNumParams> edited{};
    for (int pid = 0; pid < kNumParams; ++pid) {
        edited[(size_t)pid] = sidStateRootParamValue(root, pid);
    }
    edited[(size_t)kParamDrSidEnable] = 1.0f;
    edited[(size_t)kParamSynthModeEnable] = 0.0f;
    edited[(size_t)kParamDrSidMachineModel] = 1.0f;
    edited[(size_t)kParamDrSidKickTune] = 0.73f;
    edited[(size_t)kParamDrSidKickDecay] = 0.61f;
    edited[(size_t)kParamDrSidSnareTone] = 0.42f;
    edited[(size_t)kParamDrSidOutputDrive] = 0.31f;
    edited[(size_t)kParamDrSidAccentAmount] = 0.57f;
    edited[(size_t)kParamDrSidHatMetal] = 0.86f;
    edited[(size_t)kParamDrSidClapSpread] = 0.24f;

    auto baseline = std::make_unique<ArpSIDDSPKernel>();
    baseline->setup(48000.0, 512);
    baseline->setStickyPresetDisplaySlot(slot);
    applyEditedSnapshot(*baseline, slot, edited, false);
    const Fingerprint pre = renderKickFingerprint(*baseline);
    require(pre.rms > 1.0e-4, "edited DrSID kick fingerprint must be non-silent");
    require(pre.peak <= 2.0, "edited DrSID kick fingerprint must remain bounded");

    auto afterReset = std::make_unique<ArpSIDDSPKernel>();
    afterReset->setup(48000.0, 512);
    afterReset->setStickyPresetDisplaySlot(slot);
    applyEditedSnapshot(*afterReset, slot, edited, true);
    const Fingerprint post = renderKickFingerprint(*afterReset);

    requireNear(post.rms, pre.rms, "Stop/Start reset must preserve DrSID kick RMS fingerprint", 1.0e-7);
    requireNear(post.peak, pre.peak, "Stop/Start reset must preserve DrSID kick peak fingerprint", 1.0e-7);
    requireNear(post.sum, pre.sum, "Stop/Start reset must preserve DrSID kick sum fingerprint", 1.0e-6);
    requireNear(post.weighted, pre.weighted, "Stop/Start reset must preserve DrSID kick weighted fingerprint", 1.0e-4);

    auto slot0 = std::make_unique<ArpSIDDSPKernel>();
    slot0->setup(48000.0, 512);
    slot0->setStickyPresetDisplaySlot(0);
    SidStateRootV1 slot0Root = makeFactoryPatchStateRootForSlot(0);
    require(slot0Root.valid(), "slot 0 root must be valid");
    slot0->applyStateRootCanonical(slot0Root);
    const Fingerprint zero = renderKickFingerprint(*slot0);
    require(std::fabs(zero.rms - post.rms) > 1.0e-5 || std::fabs(zero.weighted - post.weighted) > 1.0e-3,
            "reset fingerprint must not collapse to factory slot 0");

    std::cout << "FactoryPatchAudioFingerprintResetParityV240Tests PASS\n";
    return 0;
}
