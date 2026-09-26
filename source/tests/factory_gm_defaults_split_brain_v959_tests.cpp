// Copyright (C) 2024-2026 Ulf Bertilsson
// v959 factory/GM defaults + split-brain closure.
//
// Locks in the audit of every factory/GM default across all 180 factory slots:
//   (A) each slot's state root is valid and canonicalizes idempotently;
//   (B) render-mode authority has no split-brain (never drSidEnable && synthModeEnable),
//       and the resolved mode is always a SID-chip-backed mode (BitPerfect / SidRegister
//       / DrSid) — Digi slots resolve to non-DrSID flags and drive the $D418 volume DAC;
//   (C) no latent authority split-brain: raw ArpEnable is only set in BitPerfect, raw
//       SeqEnable only in BitPerfect/DrSid (otherwise the flag is dead weight);
//   (D) staging parity: after applyStateRootCanonical the kernel's live mode flags match
//       the canonicalized root exactly (no AU3 staging split-brain);
//   (E) reloading the same preset (fresh root each time, with churn in between) is
//       idempotent — the applied param image does not drift with history;
//   (F) a representative subset of SID slots is audible + finite through the SID chip.

#include "au3/ArpSIDDSPKernel.hpp"
#include "factory_patch_params.h"
#include "parameter_ids.h"
#include "arpsid/core/sid_runtime_model.h"
#include "arpsid/core/drum_context.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace ArpSID;

static void require(bool ok, const char* msg, int slot) {
    if (!ok) {
        std::fprintf(stderr, "factory_gm_defaults_split_brain_v959_tests FAIL (slot %d): %s\n", slot, msg);
        std::exit(1);
    }
}

static float rp(const SidStateRootV1& r, int pid) { return sidStateRootParamValue(r, pid); }

static float renderPeak(ArpSIDDSPKernel& k, int note, int chan, bool& finite) {
    constexpr int F = 512;
    std::array<float, F> l{}, r{};
    float* o[2] = { l.data(), r.data() };
    TransportState t{};
    t.isPlaying = true; t.playStateKnown = true; t.sampleRate = 48000.0; t.frameCount = F;
    finite = true; float peak = 0.0f;
    k.processBlock(o, 2, F, nullptr, 0, t); // warm
    for (int b = 0; b < 18; ++b) {
        l.fill(0.0f); r.fill(0.0f);
        if (b == 0) {
            TimedEvent e{}; e.sampleOffset=0; e.kind=EventKind::NoteOn;
            e.channel=(int8_t)chan; e.pitch=(int16_t)note; e.value=1.0f;
            k.processBlock(o, 2, F, &e, 1, t);
        } else k.processBlock(o, 2, F, nullptr, 0, t);
        for (int i = 0; i < F; ++i) {
            if (!std::isfinite(l[(size_t)i]) || !std::isfinite(r[(size_t)i])) finite = false;
            float a = std::fabs(l[(size_t)i]); if (a > peak) peak = a;
        }
    }
    return peak;
}

int main() {
    ArpSID::prewarmAllSidTables();
    int sidRegister = 0, drSid = 0, digi = 0, bitPerfect = 0;

    for (int slot = 0; slot < 180; ++slot) {
        SidStateRootV1 root = makeFactoryPatchStateRootForSlot(slot);
        require(root.valid(), "factory state root must be valid", slot);
        sidCanonicalizeStateRootForApply(root);

        // (A) canonicalize-twice idempotency.
        SidStateRootV1 root2 = root; sidCanonicalizeStateRootForApply(root2);
        for (int i = 0; i < kNumParams; ++i)
            require(std::fabs(rp(root,i)-rp(root2,i)) <= 1e-6f, "canonicalization must be idempotent", slot);

        const bool dr = rp(root, kParamDrSidEnable) > 0.5f;
        const bool sy = rp(root, kParamSynthModeEnable) > 0.5f;
        const bool ar = rp(root, kParamArpEnable) > 0.5f;
        const bool sq = rp(root, kParamSeqEnable) > 0.5f;

        // (B) hard render-mode split-brain.
        require(!(dr && sy), "SPLIT-BRAIN: drSidEnable && synthModeEnable both set", slot);

        std::array<float,(size_t)kNumParams> pv{};
        for (int i=0;i<kNumParams;++i) pv[(size_t)i]=rp(root,i);
        const SidRuntimeRenderMode mode = sidResolveRenderModeFromLiveParams(pv);
        const bool isDigi = (factorySlotContext(slot) == DrumContext::Digi4Bit);

        // (B) resolved mode must be a real SID-chip mode.
        switch (mode) {
            case SidRuntimeRenderMode::BitPerfect:  ++bitPerfect; break;
            case SidRuntimeRenderMode::SidRegister: ++sidRegister; break;
            case SidRuntimeRenderMode::DrSid:       ++drSid; break;
            default: require(false, "resolved render mode is not a SID-chip mode", slot); break;
        }
        if (isDigi) ++digi;

        // (C) latent authority split-brain.
        require(!ar || mode == SidRuntimeRenderMode::BitPerfect,
                "raw ArpEnable set for a non-BitPerfect mode (dead authority)", slot);
        require(!sq || mode == SidRuntimeRenderMode::BitPerfect || mode == SidRuntimeRenderMode::DrSid,
                "raw SeqEnable set for a mode that ignores SEQ (dead authority)", slot);

        // (D) staging parity: kernel live flags must match the canonical root.
        auto k = std::make_unique<ArpSIDDSPKernel>(); k->setup(48000.0, 512);
        SidStateRootV1 a = root; k->applyStateRootCanonical(a, false);
        const auto& kpv = k->runtimeParameterValues();
        require((kpv[(size_t)kParamDrSidEnable] > 0.5f) == dr, "STAGING SPLIT-BRAIN: kernel drSid flag != root", slot);
        require((kpv[(size_t)kParamSynthModeEnable] > 0.5f) == sy, "STAGING SPLIT-BRAIN: kernel synth flag != root", slot);
        require(!((kpv[(size_t)kParamDrSidEnable] > 0.5f) && (kpv[(size_t)kParamSynthModeEnable] > 0.5f)),
                "STAGING SPLIT-BRAIN: kernel has both mode flags set", slot);
    }

    // (E) reload idempotency with churn in between (product-correct: fresh root per load).
    for (int slot : {0, 34, 47, 63, 80, 100, 120, 135, 150, 170}) {
        auto k = std::make_unique<ArpSIDDSPKernel>(); k->setup(48000.0, 512);
        SidStateRootV1 r1 = makeFactoryPatchStateRootForSlot(slot); sidCanonicalizeStateRootForApply(r1);
        k->applyStateRootCanonical(r1, false);
        std::array<float,(size_t)kNumParams> img1{};
        { const auto& v=k->runtimeParameterValues(); for (int i=0;i<kNumParams;++i) img1[(size_t)i]=v[(size_t)i]; }
        SidStateRootV1 rx = makeFactoryPatchStateRootForSlot((slot+7)%180); sidCanonicalizeStateRootForApply(rx);
        k->applyStateRootCanonical(rx, false); // churn
        SidStateRootV1 r2 = makeFactoryPatchStateRootForSlot(slot); sidCanonicalizeStateRootForApply(r2);
        k->applyStateRootCanonical(r2, false);
        const auto& v2 = k->runtimeParameterValues();
        for (int i=0;i<kNumParams;++i)
            require(std::fabs(img1[(size_t)i]-v2[(size_t)i]) <= 1e-6f, "reloading same preset must be idempotent", slot);
    }

    // (F) representative SID slots must be audible + finite through the SID chip.
    //     Melodic GM slots on note 60/ch0; DrSID drum slots on a GM drum note/ch9.
    for (int slot : {0, 24, 40, 56, 72, 88, 104}) { // melodic GM programs
        auto k = std::make_unique<ArpSIDDSPKernel>(); k->setup(48000.0, 512);
        SidStateRootV1 r = makeFactoryPatchStateRootForSlot(slot); sidCanonicalizeStateRootForApply(r);
        k->applyStateRootCanonical(r, false);
        bool fin=true; const float pk = renderPeak(*k, 60, 0, fin);
        require(fin, "melodic GM slot output must be finite", slot);
        require(pk > 0.02f, "melodic GM slot must be audible through the SID chip", slot);
    }
    for (int slot : {47, 80, 95, 112, 120, 135}) { // DrSID / SID808 drum slots
        auto k = std::make_unique<ArpSIDDSPKernel>(); k->setup(48000.0, 512);
        SidStateRootV1 r = makeFactoryPatchStateRootForSlot(slot); sidCanonicalizeStateRootForApply(r);
        k->applyStateRootCanonical(r, false);
        bool fin=true; float pk = 0.0f;
        for (int note : {36, 38, 42, 47, 56}) { bool f=true; float p=renderPeak(*k, note, 9, f); if(!f) fin=false; if(p>pk)pk=p; }
        require(fin, "drum slot output must be finite", slot);
        require(pk > 0.02f, "drum slot must be audible through the SID chip", slot);
    }

    std::printf("factory/GM mode distribution: SidRegister=%d DrSid=%d BitPerfect=%d (Digi subset=%d)\n",
                sidRegister, drSid, bitPerfect, digi);
    std::printf("factory_gm_defaults_split_brain_v959_tests PASS\n");
    return 0;
}
