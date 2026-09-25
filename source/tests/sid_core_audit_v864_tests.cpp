// sid_core_audit_v864_tests.cpp
//
// Low-level SID-core audit closure guard. This pins the surfaces called out by
// the v860 SID-core audit that can drift independently between audio rendering,
// C64 bus readback, and interval-native timed-write rendering.

#include "arpsid/core/c64_sid_readback.h"
#include "arpsid/core/sid_chip.h"
#include "arpsid/core/sid_chip_interval_native.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::abort();
    }
}

ArpSID::ArpSIDForensicConfig deterministicForensic() {
    ArpSID::ArpSIDForensicConfig fc{};
    fc.enable = false;
    fc.bitPerfectMode = true;
    fc.forensicFreeze = true;
    fc.startupRandomization = false;
    fc.clockJitterEnabled = false;
    fc.supplyRippleEnabled = false;
    fc.thermalDriftEnabled = false;
    fc.voiceCrosstalkEnabled = false;
    fc.externalBleedEnabled = false;
    fc.d418Asymmetry = 0.0f;
    fc.systemNoise = 0.0f;
    fc.motherboard = 0.0f;
    fc.adcBleed = 0.0f;
    fc.busCollision = 0.0f;
    fc.potInput = 0.0f;
    return fc;
}

ArpSID::SIDChip makeDeterministicChip() {
    ArpSID::SIDChip chip;
    chip.setStartupRandomization(false);
    chip.reset();
    chip.setForensicConfig(deterministicForensic());
    chip.setSampleRate(48000.0);
    chip.setClockFrequency(ArpSID::PAL_CLOCK_FREQ);
    chip.setExternalRcEnabled(false);
    chip.setVoiceCrosstalk(0.0f);
    chip.setExternalInputBleed(0.0f);
    return chip;
}

std::vector<std::uint8_t> collectVoiceNoiseAfterOptionalTest(bool useTestReset) {
    ArpSID::SIDVoice v;
    v.reset();
    v.setModel(ArpSID::SIDModel::MOS8580);
    v.setFrequency(0xFFFFu);
    v.setWaveform(0x08u); // noise waveform index -> SID control $80
    v.setGate(true);
    const auto fc = deterministicForensic();

    if (useTestReset) {
        for (int i = 0; i < 128; ++i) {
            v.stepCycle();
            (void)v.renderFromPhase(fc);
        }
        v.setTestBit(true);
        for (int i = 0; i < 32; ++i) {
            v.stepCycle();
            (void)v.renderFromPhase(fc);
            require(v.readOscillatorByte() == 0u, "SIDVoice noise OSC read is held at zero under TEST");
        }
        v.setTestBit(false);
    }

    std::vector<std::uint8_t> out;
    out.reserve(256);
    for (int i = 0; i < 256; ++i) {
        v.stepCycle();
        (void)v.renderFromPhase(fc);
        out.push_back(v.readOscillatorByte());
    }
    return out;
}

std::vector<std::uint8_t> collectReadbackNoiseAfterOptionalTest(bool useTestReset) {
    ArpSID::C64::SidReadbackModel rb;
    rb.reset(false);
    std::uint64_t phi2 = 0;
    rb.write(phi2, 0x0Eu, 0xFFu); // voice 3 freq lo
    rb.write(phi2, 0x0Fu, 0xFFu); // voice 3 freq hi
    rb.write(phi2, 0x12u, 0x81u); // noise | gate

    if (useTestReset) {
        for (int i = 0; i < 128; ++i) (void)rb.read(++phi2, 0x1Bu);
        rb.write(phi2, 0x12u, 0x88u); // noise | TEST
        for (int i = 0; i < 32; ++i) {
            require(rb.read(++phi2, 0x1Bu) == 0u,
                    "SidReadbackModel OSC3 is held at zero under TEST");
        }
        rb.write(phi2, 0x12u, 0x81u); // release TEST, noise | gate
    }

    std::vector<std::uint8_t> out;
    out.reserve(256);
    for (int i = 0; i < 256; ++i) out.push_back(rb.read(++phi2, 0x1Bu));
    return out;
}

} // namespace

int main() {
    using namespace ArpSID;
    prewarmAllSidTables();

    // Pulse-width edge cases remain pinned by SidCoreExactnessV854Tests and
    // C64PlayBridgeRoutingV855Tests. This pass guards the neighboring parity
    // surfaces that can regress without changing those explicit PW tests.

    // Waveform select edge case: direct SIDVoice callers historically used
    // both shifted waveform indices (triangle=0x01) and raw SID control masks
    // (triangle=$10). Both must select the same oscillator, otherwise rendered
    // audio guards can accidentally measure silence or DC instead of a voice.
    {
        SIDVoice internal;
        SIDVoice raw;
        internal.reset();
        raw.reset();
        internal.setModel(SIDModel::MOS8580);
        raw.setModel(SIDModel::MOS8580);
        internal.setFrequency(0x4000u);
        raw.setFrequency(0x4000u);
        internal.setAttack(0); internal.setDecay(0); internal.setSustain(15); internal.setRelease(0);
        raw.setAttack(0); raw.setDecay(0); raw.setSustain(15); raw.setRelease(0);
        internal.setWaveform(0x01u); // internal triangle nibble
        raw.setWaveform(0x10u);      // raw SID control-register triangle bit
        internal.setGate(true);
        raw.setGate(true);
        const auto fc = deterministicForensic();
        for (int i = 0; i < 64; ++i) {
            internal.stepCycle();
            raw.stepCycle();
            const float a = internal.renderFromPhase(fc);
            const float b = raw.renderFromPhase(fc);
            require(std::fabs(a - b) < 1.0e-7f,
                    "raw SID waveform mask and internal waveform nibble render identically");
        }
        require(raw.readOscillatorByte() != 0u,
                "raw triangle waveform mask selects a real oscillator, not silence");
    }

    // TEST/noise reset: after TEST release, both audio SIDVoice and C64-facing
    // readback restart from the same reset LFSR/phase sequence as a fresh voice.
    {
        require(collectVoiceNoiseAfterOptionalTest(false) == collectVoiceNoiseAfterOptionalTest(true),
                "SIDVoice TEST release restarts noise from the reset LFSR sequence");
        require(collectReadbackNoiseAfterOptionalTest(false) == collectReadbackNoiseAfterOptionalTest(true),
                "SidReadbackModel TEST release restarts noise from the reset LFSR sequence");
    }

    // Hard restart/gate timing: the re-gate lands exactly on the 46th service
    // tick, with no one-cycle-late drift.
    {
        SIDVoice v;
        v.reset();
        v.setGate(true);
        v.scheduleHardRestart();
        auto s = v.snapshot();
        require(!s.gate && s.hardRestartCycles == kSidHardRestartCycles,
                "hard restart immediately drops gate and starts the 46-cycle window");
        for (int i = 0; i < kSidHardRestartCycles - 1; ++i) v.stepCycle();
        s = v.snapshot();
        require(!s.gate && s.hardRestartCycles == 1,
                "hard restart does not re-gate before the final service tick");
        v.stepCycle();
        s = v.snapshot();
        require(s.gate && s.hardRestartCycles < 0,
                "hard restart re-gates exactly when the countdown reaches zero");
    }

    // Sync/ring source timing parity: C64 readback must use the same no-cascade
    // sync law as SIDChip. If V0 and V2 MSBs rise together while V0 is synced
    // by V2, V1 must not also reset from V0's transient edge.
    {
        ArpSID::C64::SidReadbackModel rb;
        rb.reset(false);
        rb.write(0, 0x00u, 0x00u); rb.write(0, 0x01u, 0x80u); // V1 freq $8000
        rb.write(0, 0x07u, 0x00u); rb.write(0, 0x08u, 0x10u); // V2 freq $1000
        rb.write(0, 0x0Eu, 0x00u); rb.write(0, 0x0Fu, 0x80u); // V3 freq $8000
        rb.write(0, 0x04u, 0x02u); // V1 syncs from V3
        rb.write(0, 0x0Bu, 0x02u); // V2 syncs from V1
        rb.write(0, 0x12u, 0x00u); // V3 free-runs
        (void)rb.read(256u, 0x1Bu);
        require(rb.phase(0) == 0u, "V1 resets from V3's real MSB edge");
        require(rb.phase(1) == 0x100000u,
                "V2 does not receive a cascading same-edge reset from synced V1");
    }

    // Filter routing/cutoff law: mode bits are combinable, cutoff is monotonic,
    // and 8580 high-cutoff reach remains brighter than 6581 calibration.
    {
        static_assert(static_cast<std::uint8_t>(FilterMode::Notch) ==
                      (static_cast<std::uint8_t>(FilterMode::LowPass) |
                       static_cast<std::uint8_t>(FilterMode::HighPass)),
                      "Notch is LP+HP, not an exclusive override");
        static_assert(static_cast<std::uint8_t>(FilterMode::LpBpHp) ==
                      (static_cast<std::uint8_t>(FilterMode::LowPass) |
                       static_cast<std::uint8_t>(FilterMode::BandPass) |
                       static_cast<std::uint8_t>(FilterMode::HighPass)),
                      "LP+BP+HP is a legal combined SID filter mode");
        const auto lo6581 = sidComputeFilterParityLaw(SIDModel::MOS6581, 0u, 0u, 0.0f, 1.0f, 3u);
        const auto hi6581 = sidComputeFilterParityLaw(SIDModel::MOS6581, 2047u, 15u, 0.0f, 1.0f, 3u);
        const auto hi8580 = sidComputeFilterParityLaw(SIDModel::MOS8580, 2047u, 15u, 0.0f, 1.0f, 5u);
        require(hi6581.cutoffHz > lo6581.cutoffHz, "6581 cutoff law is monotonic across the 11-bit FC range");
        require(hi6581.q > lo6581.q, "resonance nibble raises the filter Q");
        require(hi8580.cutoffHz > hi6581.cutoffHz, "8580 high cutoff remains brighter than 6581 calibration");
    }

    // D418/master-volume DC: volume zero must not leak the revision calibration
    // DC pedestal as a permanent post-volume output.
    {
        SIDChip chip = makeDeterministicChip();
        chip.setModel(SIDModel::MOS6581);
        chip.setRevision(3u);
        chip.setMasterVolume(0u);
        SIDVoice& v = chip.getVoice(0);
        v.setFrequency(0x1800u);
        v.setWaveform(0x02u); // saw
        v.setGate(true);
        float tailPeak = 0.0f;
        for (int i = 0; i < 12000; ++i) {
            float l = 0.0f, r = 0.0f;
            chip.processSample(l, r);
            require(std::isfinite(l) && std::isfinite(r), "volume-zero render remains finite");
            if (i >= 10000) tailPeak = std::max(tailPeak, std::fabs(l));
        }
        require(tailPeak < 0.0020f,
                "$D418 volume 0 settles near silence; calibration DC is not post-volume");
    }

    // Native interval cycle/subcycle advancement must be independent of host
    // sample rate. The old helper called processSample(), so 16 native cycles at
    // 48 kHz advanced roughly 16 host samples worth of SID cycles instead.
    {
        SIDChip chip = makeDeterministicChip();
        chip.getVoice(0).setFrequency(0x1000u);
        chip.getVoice(0).setWaveform(0x02u);
        (void)sidChipAdvanceCyclesNative(chip, 16);
        require(chip.getVoice(0).getPhase() == 0x010000u,
                "sidChipAdvanceCyclesNative advances exactly the requested SID cycles");

        SIDChip sub = makeDeterministicChip();
        sub.getVoice(0).setFrequency(0x1000u);
        sub.getVoice(0).setWaveform(0x02u);
        (void)sidChipAdvanceSubphasesNative(sub, 7u, 0u, 128u, 1);
        require(sub.getVoice(0).getPhase() == 0x000800u,
                "native subphase advance ignores absolute cycle index and advances half a cycle");
        (void)sidChipAdvanceSubphasesNative(sub, 7u, 128u, kSidSubcycleBoundary, 1);
        require(sub.getVoice(0).getPhase() == 0x001000u,
                "native subphase advance composes to one full SID cycle");

        SIDChip interval = makeDeterministicChip();
        interval.getVoice(0).setFrequency(0x1000u);
        interval.getVoice(0).setWaveform(0x02u);
        float l = 0.0f, r = 0.0f;
        SidRenderInterval iv{};
        iv.beginCycle = 99u;
        iv.beginSubphase = 64u;
        iv.endCycle = 99u;
        iv.endSubphase = 192u;
        sidChipRenderIntervalNative(interval, iv, l, r);
        require(interval.getVoice(0).getPhase() == 0x000800u,
                "interval-native partial cycle ignores host-planner cursor and advances exact subcycle width");
    }

    // Timed-write half-open interval semantics: begin is included, end is
    // excluded, and subphase ordering is stable.
    {
        std::array<SidSubphaseWrite, 4> writes{};
        writes[0].cycle = 9u;  writes[0].subphase = 255u; writes[0].regIndex = 0x04u; writes[0].value = 0x11u;
        writes[1].cycle = 10u; writes[1].subphase = 20u;  writes[1].regIndex = 0x04u; writes[1].value = 0x22u;
        writes[2].cycle = 10u; writes[2].subphase = 39u;  writes[2].regIndex = 0x04u; writes[2].value = 0x33u;
        writes[3].cycle = 10u; writes[3].subphase = 40u;  writes[3].regIndex = 0x04u; writes[3].value = 0x44u;
        std::vector<std::uint8_t> applied;
        const int count = sidApplyWritesInInterval(
            writes, static_cast<int>(writes.size()),
            10u, 20u, 10u, 40u, true,
            [&](std::uint8_t, std::uint8_t value) noexcept { applied.push_back(value); });
        require(count == 2 && applied.size() == 2u,
                "timed-write interval applies begin and interior writes only");
        require(applied[0] == 0x22u && applied[1] == 0x33u,
                "timed-write interval preserves in-interval order");
    }

    std::cout << "SidCoreAuditV864Tests PASS\n";
    return 0;
}
