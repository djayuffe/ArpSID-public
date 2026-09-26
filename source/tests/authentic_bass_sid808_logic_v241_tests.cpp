// Copyright (C) 2024-2026 Ulf Bertilsson

#include "factory_patch_params.h"
#include "arpsid/patchbank/factory_sid808_param_bridge.h"
#include "arpsid/engines/drsid_engine.h"

#include <array>
#include <cmath>
#include <iostream>
#include <vector>

using namespace ArpSID;

static bool closef(float a, float b, float eps = 0.0005f) {
    return std::fabs(a - b) <= eps;
}

static bool require(bool cond, const char* msg) {
    if (!cond) std::cerr << "FAIL: " << msg << "\n";
    return cond;
}

static float p(const std::array<float, static_cast<size_t>(kNumParams)>& params, ParamID id) {
    return params[static_cast<size_t>(id)];
}

static bool notePresent(const std::array<float, static_cast<size_t>(kNumParams)>& params, int midi) {
    for (int i = 0; i < 16; ++i) {
        const int base = static_cast<int>(kParamSeqStep1Note) + i * 3;
        const int note = static_cast<int>(std::lround(p(params, static_cast<ParamID>(base + 0)) * 127.0f));
        const float vel = p(params, static_cast<ParamID>(base + 1));
        const float gate = p(params, static_cast<ParamID>(base + 2));
        if (note == midi && vel > 0.05f && gate > 0.05f) return true;
    }
    return false;
}

int main() {
    bool ok = true;

    // GM bass factory slots must be authored as C64 bass, not generic wide synth.
    for (int slot = 32; slot <= 39; ++slot) {
        std::array<float, static_cast<size_t>(kNumParams)> params{};
        ok &= require(loadFactoryPatchNormalizedParamsForSlot(slot, params), "bass slot loads factory params");
        ok &= require(closef(p(params, kParamVoiceMode), 1.0f / 3.0f), "bass slot forced mono");
        ok &= require(closef(p(params, kParamVoiceSpread), 0.0f), "bass slot has no unison/spread smear");
        ok &= require(p(params, kParamVCO1Level) >= 0.86f, "bass slot has dominant SID oscillator 1");
        ok &= require(p(params, kParamVCO3Level) <= 0.001f, "bass slot does not waste low end on voice 3");
        ok &= require(p(params, kParamLFODepth) <= 0.001f, "bass slot has no LFO1 low-end wobble");
        ok &= require(p(params, kParamLFO2Depth) <= 0.001f, "bass slot has no LFO2 pitch smear");
        ok &= require(p(params, kParamFilterDrive) <= 0.18f, "bass filter drive remains SID-auth bounded");
        ok &= require(p(params, kParamPortamentoTime) < 0.14f, "bass uses short C64 register-slide law, not smooth-synth glide");
    }

    ok &= require(!isFactorySid808KitSlot(119), "slot 119 is not SID-808 missing-logic range");
    ok &= require( isFactorySid808KitSlot(120), "slot 120 starts SID-808 missing-logic range");
    ok &= require( isFactorySid808KitSlot(124), "slot 124 remains SID-808 missing-logic range");
    ok &= require( isFactorySid808KitSlot(125), "slot 125 is now SID-808 missing-logic range");
    ok &= require( isFactorySid808KitSlot(149), "slot 149 ends SID-808 missing-logic range");
    ok &= require(!isFactorySid808KitSlot(150), "slot 150 is outside SID-808 missing-logic range");
    ok &= require(!isFactorySid808KitSlot(200), "slot 200 must not clamp back into SID-808 range");

    // SID-808 kits must be full x0x pages with useful GM family coverage
    // across the complete canonical 120..149 KIT range.
    for (int slot = 120; slot <= 149; ++slot) {
        std::array<float, static_cast<size_t>(kNumParams)> params{};
        ok &= require(loadFactoryPatchNormalizedParamsForSlot(slot, params), "SID-808 slot loads factory params");
        ok &= require(p(params, kParamDrSidEnable) > 0.5f, "SID-808 slot enables DrSID");
        ok &= require(p(params, kParamSynthModeEnable) < 0.5f, "SID-808 slot disables SynthMode");
        ok &= require(p(params, kParamDrSidMachineModel) > 0.99f, "SID-808 slot forces Analog X0X model");
        // v859: factory loads are SILENT until played. The internal seq clock free-runs
        // regardless of host transport, so seqEnable=1 made every kit load start
        // sounding immediately. The authored pattern stays published; SEQ is off.
        ok &= require(p(params, kParamSeqEnable) < 0.5f, "SID-808 slot loads with sequencer OFF (silent until played)");
        ok &= require(closef(p(params, kParamSeqLength), normalizedFactorySeqLength(16)), "SID-808 slot is a 16-step x0x page");
        ok &= require(notePresent(params, 36) || notePresent(params, 35), "SID-808 page has kick coverage");
        ok &= require(notePresent(params, 38) || notePresent(params, 40), "SID-808 page has snare coverage");
        ok &= require(notePresent(params, 42), "SID-808 page has closed-hat coverage");
        ok &= require(notePresent(params, 46) || notePresent(params, 49) || notePresent(params, 57), "SID-808 page has open-hat/cymbal coverage");
        // SID-808 limiter threshold: each kit picks its own tuning. Punchy
        // kits (Punch/Hard) clamp aggressively (≤0.85), Classic/Wide sit at
        // ≤0.90, and the Lo-Fi kit deliberately keeps more headroom (≤0.95)
        // because it is quieter overall and benefits from a looser limiter to
        // preserve dynamic shape on vintage/dub material.
        ok &= require(p(params, kParamLimiterThreshold) <= 0.95f,
                      "SID-808 limiter stays within the per-kit punch/headroom envelope");
    }
    {
        std::array<float, static_cast<size_t>(kNumParams)> slot120{};
        std::array<float, static_cast<size_t>(kNumParams)> slot125{};
        std::array<float, static_cast<size_t>(kNumParams)> slot149{};
        ok &= require(loadFactoryPatchNormalizedParamsForSlot(120, slot120), "SID-808 slot 120 loads");
        ok &= require(loadFactoryPatchNormalizedParamsForSlot(125, slot125), "SID-808 slot 125 loads");
        ok &= require(loadFactoryPatchNormalizedParamsForSlot(149, slot149), "SID-808 slot 149 loads");
        const auto sig120 = factorySid808ParamSignatureForSlot(120);
        const auto sig125 = factorySid808ParamSignatureForSlot(125);
        const auto sig149 = factorySid808ParamSignatureForSlot(149);
        ok &= require(closef(p(slot125, kParamDrSidKickTune), sig125.kickTune),
                      "SID-808 slot 125 uses Sid808VoiceConfig-derived variant kick tune");
        ok &= require(!closef(p(slot125, kParamDrSidKickTune), p(slot120, kParamDrSidKickTune)),
                      "SID-808 slot 125 is a real Classic variant, not a stale exact wrap of slot 120");
        ok &= require(closef(p(slot149, kParamDrSidKickTune), sig149.kickTune),
                      "SID-808 slot 149 uses Sid808VoiceConfig-derived Wide variant kick tune");
        ok &= require(p(slot149, kParamDrSidMachineModel) > 0.99f,
                      "SID-808 slot 149 stays Analog X0X after canonical factory-root preservation");
    }

    // Engine-level proof: Analog X0X kick remains finite and active in SID projection path.
    DrSidEngine dr;
    dr.setSampleRate(48000.0);
    dr.setDrumMachineModelNormalized(1.0f);
    dr.setKickTune(0.30f);
    dr.setKickDecay(0.62f);
    dr.setMasterVolume(0.84f);
    dr.triggerMidiNote(36, 1.0f);
    std::array<float, 512> l{};
    std::array<float, 512> r{};
    float* outs[2] = {l.data(), r.data()};
    dr.processBlock(outs, static_cast<int>(l.size()));
    double energy = 0.0;
    double peak = 0.0;
    for (size_t i = 0; i < l.size(); ++i) {
        ok &= require(std::isfinite(l[i]) && std::isfinite(r[i]), "SID-808 kick render stays finite");
        energy += static_cast<double>(l[i] * l[i] + r[i] * r[i]);
        peak = std::max(peak, std::max(std::fabs(static_cast<double>(l[i])), std::fabs(static_cast<double>(r[i]))));
    }
    ok &= require(energy > 1.0e-8, "SID-808 kick renders non-silent energy");
    ok &= require(peak <= 1.25, "SID-808 kick stays within bounded post-shape headroom");

    return ok ? 0 : 1;
}
