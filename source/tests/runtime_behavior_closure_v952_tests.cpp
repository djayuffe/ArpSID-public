// v952 final runtime behavior closure.
//
// This is intentionally an executable DSP test, not a source-text contract:
//  * a GUI-authored SID808 KIT step must render after Stop->Play;
//  * the same pattern must survive the AU transport-reset path;
//  * stopped follow-host transport must not consume/audition step zero;
//  * queue-full dirty fallback must run the ordinary parameter side effects and
//    older queued generations must not overwrite its latest value;
//  * normalized repair must enforce the shared per-parameter type contract.

#include "au3/ArpSIDDSPKernel.hpp"
#include "factory_patch_params.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>

namespace {

using namespace ArpSID;

void require(bool ok, const char* message) {
    if (!ok) {
        std::cerr << "RuntimeBehaviorClosureV952Tests FAIL: " << message << '\n';
        std::exit(1);
    }
}

void requireNear(float actual, float expected, float epsilon, const char* message) {
    require(std::isfinite(actual) && std::fabs(actual - expected) <= epsilon, message);
}

struct RenderObservation {
    float peak = 0.0f;
    std::uint64_t routedHits = 0u;
};

RenderObservation renderBlock(ArpSIDDSPKernel& kernel, bool playing, double beat) {
    constexpr int kFrames = 512;
    std::array<float, kFrames> left{};
    std::array<float, kFrames> right{};
    float* outputs[2] = {left.data(), right.data()};

    TransportState transport{};
    transport.isPlaying = playing;
    transport.playStateKnown = true;
    transport.bpm = 120.0;
    transport.beatPosition = beat;
    transport.sampleRate = 48000.0;
    transport.frameCount = kFrames;
    kernel.processBlock(outputs, 2, kFrames, nullptr, 0, transport);

    RenderObservation out{};
    for (int i = 0; i < kFrames; ++i) {
        require(std::isfinite(left[(size_t)i]) && std::isfinite(right[(size_t)i]),
                "rendered SID808 samples must remain finite");
        out.peak = std::max(out.peak, std::max(std::fabs(left[(size_t)i]),
                                               std::fabs(right[(size_t)i])));
    }
    out.routedHits = kernel.readTelemetry(false).sid808RoutedHitCount;
    return out;
}

SidStateRootV1 makeSid808SequencerRoot() {
    SidStateRootV1 root = makeFactoryPatchStateRootForSlot(120);
    require(root.valid(), "factory slot 120 root must be valid");
    sidSetStateRootParamValue(root, kParamDrSidEnable, 1.0f);
    sidSetStateRootParamValue(root, kParamSynthModeEnable, 0.0f);
    sidSetStateRootParamValue(root, kParamDrSidMachineModel, 1.0f);
    sidSetStateRootParamValue(root, kParamSeqEnable, 1.0f);
    sidSetStateRootParamValue(root, kParamSeqTempo, 0.0f); // follow host
    sidSetStateRootParamValue(root, kParamSeqLength, 1.0f);
    // The audible assertion must come from the GUI KIT grid, not the generic
    // note sequencer. Keep all generic step gates inactive while its cursor
    // still drives the shared 32-step transport.
    for (int step = 0; step < 32; ++step) {
        const int base = static_cast<int>(kParamSeqStep1Note) + step * 3;
        sidSetStateRootParamValue(root, base + 1, 0.0f);
        sidSetStateRootParamValue(root, base + 2, 0.0f);
    }
    sidCanonicalizeStateRootForApply(root);
    return root;
}

void testSid808PatternAcrossTransportAndReset() {
    auto kernel = std::make_unique<ArpSIDDSPKernel>();
    kernel->setup(48000.0, 512);
    kernel->setComponentFlavor(static_cast<int>(ComponentFlavor::Sid808));
    kernel->setStickyPresetDisplaySlot(120);

    SidStateRootV1 root = makeSid808SequencerRoot();
    kernel->applyStateRootCanonical(root, false);
    require(kernel->runtimeModel().followHostTempoSeq(),
            "state-root SeqTempo=0 must reconcile follow-host policy");
    require(kernel->getParameter(kParamSeqEnable) > 0.5f,
            "SID808 state-root apply must preserve SeqEnable");

    GUI::KitStateBlob kit = GUI::makeDefaultKitStateBlob();
    GUI::kitStepClearAll(kit.stepGrid);
    kit.panelModel.activeEngineTarget = static_cast<std::uint8_t>(GUI::KitEngineTarget::SID808);
    GUI::kitStepSetActive(kit.stepGrid,
                          static_cast<std::uint8_t>(GUI::KitDrumClass::Kick),
                          0u, 127u);
    require(GUI::kitStateBlobIsWellFormed(kit), "authored SID808 KIT blob must be valid");
    kernel->publishGuiRealtimeModels(nullptr, &kit, nullptr, nullptr);

    const RenderObservation stopped0 = renderBlock(*kernel, false, 0.0);
    require(stopped0.routedHits == 0u,
            "stopped follow-host transport must not audition/consume KIT step zero");

    const RenderObservation firstStart = renderBlock(*kernel, true, 0.0);
    require(firstStart.routedHits == stopped0.routedHits + 1u,
            "first Play edge must route the authored SID808 step-zero kick");
    require(firstStart.peak > 1.0e-5f,
            "first SID808 pattern hit must produce non-silent audio");

    const RenderObservation stopped1 = renderBlock(*kernel, false, 0.0);
    require(stopped1.routedHits == firstStart.routedHits,
            "Stop edge must not create a SID808 pattern hit");
    const RenderObservation secondStart = renderBlock(*kernel, true, 0.0);
    require(secondStart.routedHits == stopped1.routedHits + 1u,
            "Stop->Play must re-arm and route KIT step zero");
    require(secondStart.peak > 1.0e-5f,
            "SID808 pattern must remain audible after Stop->Play");

    std::array<float, kNumParams> snapshot{};
    for (int pid = 0; pid < kNumParams; ++pid)
        snapshot[(size_t)pid] = kernel->getParameter(pid);
    kernel->resetPreservingHostParameterSnapshot(snapshot.data(), kNumParams, 120, true);
    kernel->publishGuiRealtimeModels(nullptr, &kit, nullptr, nullptr);
    require(kernel->getParameter(kParamSeqEnable) > 0.5f,
            "AU reset must retain DrSID/SID808 SeqEnable authority");

    const RenderObservation resetStopped = renderBlock(*kernel, false, 0.0);
    const RenderObservation resetStart = renderBlock(*kernel, true, 0.0);
    require(resetStart.routedHits == resetStopped.routedHits + 1u,
            "AU transport reset must re-arm the actual SID808 KIT pattern");
    require(resetStart.peak > 1.0e-5f,
            "SID808 KIT pattern must render audible audio after AU reset");
}

void testDirtyFallbackUsesNormalSideEffectsAndOrdering() {
    auto kernel = std::make_unique<ArpSIDDSPKernel>();
    kernel->setup(48000.0, 512);
    require(!kernel->runtimeModel().followHostTempoSeq(),
            "default nonzero SeqTempo must begin in internal-tempo mode");

    bool queueFilled = false;
    for (int i = 0; i < 20000; ++i) {
        const float value = (i & 1) ? 0.25f : 0.75f;
        if (!kernel->enqueueParameterIntent(kParamSeqTempo, value)) {
            queueFilled = true;
            break;
        }
    }
    require(queueFilled, "test must reach the bounded parameter queue fallback");
    require(!kernel->enqueueParameterIntent(kParamSeqTempo, 0.0f),
            "latest follow-host SeqTempo intent must enter queue-full fallback");
    require(kernel->paramIntentDirtyFlushFallbackCount() > 0u,
            "queue-full parameter fallback must be observable");

    (void)renderBlock(*kernel, false, 0.0);
    require(kernel->runtimeModel().followHostTempoSeq(),
            "dirty fallback must run the normal SeqTempo follow-host side effect");
    requireNear(sidStateRootParamValue(kernel->runtimeModel().stateRoot(), kParamSeqTempo),
                0.0f, 1.0e-7f,
                "older queued SeqTempo generations must not overwrite the fallback");
    requireNear(kernel->runtimeParameterValues()[(size_t)kParamSeqTempo],
                0.0f, 1.0e-7f,
                "render snapshot must retain the latest fallback value");

    const float nan = std::numeric_limits<float>::quiet_NaN();
    kernel->kernelApplyNormalizedParameter(static_cast<uint32_t>(kParamMasterVolume), nan);
    require(std::isfinite(kernel->runtimeParameterValues()[(size_t)kParamMasterVolume]),
            "NaN automation must be repaired before the backend side-effect path");
    requireNear(sidStateRootParamValue(kernel->runtimeModel().stateRoot(), kParamMasterVolume),
                defaultNormalizedParamValue(kParamMasterVolume), 1.0e-7f,
                "NaN automation must reach runtime state only as the sanitized default");
}

void testParameterSpecificNormalization() {
    require(normalizedParamStepCount(kParamFilterMode) == 7,
            "FilterMode must expose all eight backend modes");
    require(normalizedParamStepCount(kParamArpMode) == 6,
            "ArpMode must expose all seven arpeggiator modes");
    require(normalizedParamStepCount(kParamSeqLength) == 31,
            "SeqLength must expose 1..32 steps");
    require(isBooleanNormalizedParam(kParamSeqEnable),
            "SeqEnable must be typed as a boolean");
    require(!isBooleanNormalizedParam(kParamDrSidMachineModel),
            "two-valued machine model is a binary enum, not an on/off flag");

    requireNear(sanitizeNormalizedParamValue(kParamSeqEnable, 0.49f, 1.0f),
                0.0f, 0.0f, "boolean values below midpoint must normalize off");
    requireNear(sanitizeNormalizedParamValue(kParamSeqEnable, 0.51f, 0.0f),
                1.0f, 0.0f, "boolean values above midpoint must normalize on");
    requireNear(sanitizeNormalizedParamValue(kParamSeqLength, 0.5f, 0.0f),
                16.0f / 31.0f, 1.0e-7f,
                "SeqLength must normalize to a 32-step semantic grid");
    requireNear(sanitizeNormalizedParamValue(kParamFilterMode, 0.26f, 0.0f),
                2.0f / 7.0f, 1.0e-7f,
                "FilterMode must normalize to one of eight semantic choices");
    requireNear(sanitizeNormalizedParamValue(kParamVCO1Waveform, 0.22f, 0.0f),
                1.0f / 7.0f, 1.0e-7f,
                "waveform repair must preserve legacy equal-width bin identity");
    requireNear(sanitizeNormalizedParamValue(kParamArpOctaves, 0.22f, 0.0f),
                0.0f, 0.0f,
                "arp-octave repair must preserve legacy floor-bin identity");
    requireNear(sanitizeNormalizedParamValue(kParamSidRegD400, 0.5f, 0.0f),
                128.0f / 255.0f, 1.0e-7f,
                "SID register values must normalize to byte values");

    SidStateRootV1 root{};
    sidSetStateRootParamValue(root, kParamSeqEnable, 0.6f);
    sidSetStateRootParamValue(root, kParamSeqLength, 0.5f);
    requireNear(sidStateRootParamValue(root, kParamSeqEnable), 1.0f, 0.0f,
                "state-root writer must use boolean normalization");
    requireNear(sidStateRootParamValue(root, kParamSeqLength), 16.0f / 31.0f, 1.0e-7f,
                "state-root writer must use stepped normalization");

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float repaired = sanitizeNormalizedParamValue(kParamSeqEnable, nan, 0.7f);
    require(repaired == 1.0f,
            "non-finite input must use and semantically normalize its finite fallback");
}

} // namespace

int main() {
    ArpSID::prewarmAllSidTables();
    testParameterSpecificNormalization();
    testDirtyFallbackUsesNormalSideEffectsAndOrdering();
    testSid808PatternAcrossTransportAndReset();
    std::cout << "RuntimeBehaviorClosureV952Tests PASS\n";
    return 0;
}
