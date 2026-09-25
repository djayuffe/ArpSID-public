#include "au3/ArpSIDDSPKernel.hpp"
#include "factory_patch_params.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

static bool finiteUnit(float v) {
    return std::isfinite(v) && v >= 0.0f && v <= 1.0f;
}

static std::array<float, ArpSID::kNumParams> baseSnapshot(int chipRevisionIndex) {
    std::array<float, ArpSID::kNumParams> snap{};
    ArpSID::SidStateRootV1 root = ArpSID::makeFactoryPatchStateRootForSlot(0);
    require(root.valid(), "slot 0 root valid");
    for (int i = 0; i < ArpSID::kNumParams; ++i) {
        snap[(size_t)i] = ArpSID::sidStateRootParamValue(root, i);
    }
    snap[(size_t)ArpSID::kParamSidChipRevision] = ArpSID::sidChipRevisionIndexToNormalized(chipRevisionIndex);
    snap[(size_t)ArpSID::kParamForensicEnable] = 1.0f;
    snap[(size_t)ArpSID::kParamForensicIntensity] = 0.72f;
    snap[(size_t)ArpSID::kParamForensicClockJitterEnable] = 1.0f;
    snap[(size_t)ArpSID::kParamForensicClockJitter] = 0.41f;
    snap[(size_t)ArpSID::kParamForensicSupplyRippleEnable] = 1.0f;
    snap[(size_t)ArpSID::kParamForensicSupplyRipple] = 0.39f;
    snap[(size_t)ArpSID::kParamForensicThermalDriftEnable] = 1.0f;
    snap[(size_t)ArpSID::kParamForensicThermalDrift] = 0.37f;
    snap[(size_t)ArpSID::kParamForensicVoiceCrosstalkEnable] = 1.0f;
    snap[(size_t)ArpSID::kParamForensicVoiceCrosstalk] = 0.35f;
    snap[(size_t)ArpSID::kParamForensicExternalBleedEnable] = 1.0f;
    snap[(size_t)ArpSID::kParamForensicExternalBleed] = 0.33f;
    snap[(size_t)ArpSID::kParamForensicFilterOhmic] = 0.31f;
    snap[(size_t)ArpSID::kParamForensicSystemNoise] = 0.29f;
    snap[(size_t)ArpSID::kParamForensicD418Asymmetry] = 0.27f;
    snap[(size_t)ArpSID::kParamForensicEnvelopeTDM] = 0.25f;
    snap[(size_t)ArpSID::kParamForensicMotherboard] = 0.23f;
    snap[(size_t)ArpSID::kParamForensicADCBleed] = 0.21f;
    snap[(size_t)ArpSID::kParamForensicBusCollision] = 0.19f;
    snap[(size_t)ArpSID::kParamForensicPOTInput] = 0.17f;
    snap[(size_t)ArpSID::kParamForensicDigifix8580] = 1.0f;
    return snap;
}

static void renderAndCheck(ArpSID::SidRuntimeRenderMode mode,
                           int chipRevisionIndex,
                           bool expectDigifix8580,
                           const char* label,
                           ArpSID::ComponentFlavor flavor = ArpSID::ComponentFlavor::Hybrid) {
    using namespace ArpSID;
    ArpSIDDSPKernel k;
    k.setup(48000.0, 512);
    k.setComponentFlavor(static_cast<int>(flavor));
    std::array<float, kNumParams> snap = baseSnapshot(chipRevisionIndex);

    if (mode == SidRuntimeRenderMode::DrSid) {
        snap[(size_t)kParamDrSidEnable] = 1.0f;
        snap[(size_t)kParamSynthModeEnable] = 0.0f;
        snap[(size_t)kParamDrSidMachineModel] = 1.0f;
    } else if (mode == SidRuntimeRenderMode::SidRegister) {
        snap[(size_t)kParamDrSidEnable] = 0.0f;
        snap[(size_t)kParamSynthModeEnable] = 1.0f;
        snap[(size_t)kParamVCO1Waveform] = 0.45f;
        snap[(size_t)kParamVirtualGate] = 1.0f;
        snap[(size_t)kParamMasterVolume] = 0.85f;
    } else {
        snap[(size_t)kParamDrSidEnable] = 0.0f;
        snap[(size_t)kParamSynthModeEnable] = 0.0f;
        snap[(size_t)kParamVCO1Waveform] = 0.35f;
        snap[(size_t)kParamVirtualGate] = 1.0f;
        snap[(size_t)kParamMasterVolume] = 0.85f;
    }

    k.restoreHostParameterSnapshotImmediate(snap.data(), kNumParams);

    constexpr int frames = 512;
    std::array<float, frames> l{};
    std::array<float, frames> r{};
    float* outs[2] = {l.data(), r.data()};
    TimedEvent ev{};
    ev.sampleOffset = 0;
    ev.kind = EventKind::NoteOn;
    ev.channel = (mode == SidRuntimeRenderMode::DrSid) ? 9 : 0;
    ev.pitch = (mode == SidRuntimeRenderMode::DrSid) ? 36 : 48;
    ev.value = 1.0f;
    TransportState t{};
    t.isPlaying = true;
    t.playStateKnown = true;
    t.sampleRate = 48000.0;
    t.frameCount = frames;
    k.processBlock(outs, 2, frames, &ev, 1, t);

    auto tel = k.readTelemetry();
    require(tel.forensicEnabled, label);
    require(finiteUnit(tel.forensicIntensity) && tel.forensicIntensity > 0.70f, "forensic intensity must publish in every mode");
    require(finiteUnit(tel.forensicActivity) && tel.forensicActivity > 0.10f, "forensic activity must publish in every mode");
    require(finiteUnit(tel.forensicClockJitter) && tel.forensicClockJitter > 0.40f, "clock jitter telemetry must publish");
    require(finiteUnit(tel.forensicSupplyRipple) && tel.forensicSupplyRipple > 0.38f, "supply ripple telemetry must publish");
    require(finiteUnit(tel.forensicThermalDrift) && tel.forensicThermalDrift > 0.36f, "thermal drift telemetry must publish");
    require(finiteUnit(tel.forensicVoiceCrosstalk) && tel.forensicVoiceCrosstalk > 0.34f, "voice crosstalk telemetry must publish");
    require(finiteUnit(tel.forensicExternalBleed) && tel.forensicExternalBleed > 0.32f, "external bleed telemetry must publish");
    require(tel.forensicDigifix8580 == expectDigifix8580, "digifix forensic telemetry must follow selected SID chip");
    require(tel.renderMode == static_cast<int>(mode), "render mode telemetry must match selected engine");
    if (mode == SidRuntimeRenderMode::DrSid) {
        require(tel.drumMachineModel > 0.99f,
                "DrSID parameter telemetry must publish from the coherent render snapshot");
    }

    float voice[8][256]{};
    float osc[3][256]{};
    float filter[2][256]{};
    uint8_t activeMask = 0;
    uint32_t writePos = 0;
    k.getScopeSnapshot(voice, osc, filter, activeMask, writePos);
    for (int c = 0; c < 3; ++c) {
        for (int i = 0; i < 256; ++i) require(std::isfinite(osc[c][i]), "osc scope must be finite");
    }
    for (int c = 0; c < 2; ++c) {
        for (int i = 0; i < 256; ++i) require(std::isfinite(filter[c][i]), "filter scope must be finite");
    }
    require(writePos < 256u || activeMask == 0u, "scope write position must be bounded");
}

int main() {
    ArpSID::prewarmAllSidTables();
    renderAndCheck(ArpSID::SidRuntimeRenderMode::BitPerfect, 3, true, "forensic telemetry enabled in BitPerfect/8580 mode");
    renderAndCheck(ArpSID::SidRuntimeRenderMode::SidRegister, 3, true, "forensic telemetry enabled in SID register/8580 mode");
    renderAndCheck(ArpSID::SidRuntimeRenderMode::DrSid, 3, true, "forensic telemetry enabled in DrSID/8580 mode");
    renderAndCheck(ArpSID::SidRuntimeRenderMode::BitPerfect, 1, false, "forensic telemetry enabled in BitPerfect/6581 mode");
    renderAndCheck(ArpSID::SidRuntimeRenderMode::SidRegister, 1, false, "forensic telemetry enabled in SID register/6581 mode");
    renderAndCheck(ArpSID::SidRuntimeRenderMode::DrSid, 1, false, "forensic telemetry enabled in DrSID/6581 mode");
    renderAndCheck(ArpSID::SidRuntimeRenderMode::DrSid, 3, true,
                   "forensic telemetry enabled in SID-808 component flavor",
                   ArpSID::ComponentFlavor::Sid808);
    std::cout << "TelemetryAllModesForensicScopeV242Tests PASS\n";
    return 0;
}
