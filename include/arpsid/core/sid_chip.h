// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once

#include <array>
#include <algorithm>
#include <mutex>
#include <cmath>
#include <cstdint>
#include <atomic>
#include "arpsid/core/math_utils.h"
#include "arpsid/core/sid_combined_wave_model.h"
#include "arpsid/core/sid_envelope_core.h"
#include "arpsid/core/sid_filter_core.h"
#include "sid_interval_renderable.h"
#include "arpsid/core/sid_realtime_guard.h"
#include "arpsid/core/sid_analogue_calibration.h"


namespace ArpSID {


constexpr double PAL_CLOCK_FREQ  = 985248.0;
constexpr double NTSC_CLOCK_FREQ = 1022727.0;
constexpr double MAX_SUPPORTED_SID_CLOCK_FREQ = 10000000.0;

static inline bool sidClockFrequencySupported(double hz) noexcept {
    return std::isfinite(hz) && hz > 1.0 && hz <= MAX_SUPPORTED_SID_CLOCK_FREQ;
}

static constexpr uint32_t kSidPhaseMask24 = 0xFFFFFFu;
static constexpr int kSidHardRestartRegRelatchCycles = 45;
static constexpr int kSidHardRestartCycles = 46;
static constexpr int kSidHardRestartRegWriteDelayCycles = kSidHardRestartRegRelatchCycles;
static constexpr int kSidHardSyncSourceOf[3] = { 2, 0, 1 };

static inline bool sidPhaseMsbRose(uint32_t previousPhase, uint32_t nextPhase) noexcept {
    return (previousPhase & 0x800000u) == 0u && (nextPhase & 0x800000u) != 0u;
}

static inline bool sidHardSyncShouldReset(int destination,
                                          const bool syncEnabled[3],
                                          const bool sourceMsbRose[3]) noexcept {
    if (destination < 0 || destination >= 3 || !syncEnabled[destination]) return false;
    const int source = kSidHardSyncSourceOf[destination];
    if (!sourceMsbRose[source]) return false;
    // A source that is itself being synchronized on this edge does not
    // propagate a reset through the three-oscillator sync ring.
    const int sourceOfSource = kSidHardSyncSourceOf[source];
    return !(syncEnabled[source] && sourceMsbRose[sourceOfSource]);
}

static inline int sidHardRestartStartCountdown() noexcept {
    return std::max(0, kSidHardRestartCycles);
}

static inline bool sidHardRestartTickCountdown(int& countdown) noexcept {
    if (countdown < 0) return false;
    if (countdown > 0) --countdown;
    if (countdown == 0) {
        countdown = -1;
        return true;
    }
    return false;
}

enum class Waveform : uint8_t {
    None        = 0,
    Triangle    = 1,
    Sawtooth    = 2,
    Pulse       = 4,
    Noise       = 8,
    TriSaw      = 3,
    TriPulse    = 5,
    SawPulse    = 6,
    TriSawPulse = 7
};

enum class FilterMode : uint8_t {
    None     = 0,
    LowPass  = 1,
    BandPass = 2,
    LpBp     = 3,
    HighPass = 4,
    Notch    = 5,
    BpHp     = 6,
    LpBpHp   = 7
};

enum class SIDModel : uint8_t {
    MOS6581 = 0,
    MOS8580 = 1
};

struct SidFilterCalibration {
    double cutoffScale = 1.0;
    double resonanceScale = 1.0;
    double distortionFactor = 1.0;
};

static constexpr std::array<SidFilterCalibration, 6> kSidFilterCalibrationByRevision{{
    {0.00, 0.00, 0.00},
    {0.00, 0.00, 0.00},
    {0.72, 1.35, 1.45},
    {0.85, 1.18, 1.32},
    {1.05, 0.95, 1.12},
    {1.85, 0.68, 0.55},
}};

constexpr bool sidSelectableFilterCalibrationsValid() noexcept {
    for (std::size_t revision = 2u; revision <= 5u; ++revision) {
        const SidFilterCalibration& calibration =
            kSidFilterCalibrationByRevision[revision];
        if (!(calibration.cutoffScale > 0.0) ||
            !(calibration.resonanceScale > 0.0) ||
            !(calibration.distortionFactor > 0.0)) {
            return false;
        }
    }
    return true;
}
static_assert(sidSelectableFilterCalibrationsValid(),
              "Every user-selectable SID revision must have non-zero filter calibration");

static constexpr std::array<uint8_t, 16> kSidWaveformSelectTable{{
    0x00u, 0x10u, 0x20u, 0x30u,
    0x40u, 0x50u, 0x60u, 0x70u,
    0x80u, 0x90u, 0xA0u, 0xB0u,
    0xC0u, 0xD0u, 0xE0u, 0xF0u,
}};

static inline uint8_t sidResolveWaveformControlMask(uint8_t waveformIndex) noexcept {
    return kSidWaveformSelectTable[(size_t)(waveformIndex & 0x0Fu)];
}


struct ArpSIDForensicConfig {
    bool  enable = true;
    float intensity = 1.0f;
    float temperatureCelsius = 35.0f;
    float supplyVoltage = 5.00f;
    float supplyRippleMv = 0.0f;
    uint8_t revision = 5; // v909: HMOS-II 8580 R5 class is the factory default chip
    uint32_t chipIdSeed = 0xDEADBEEFu;
    bool  startupRandomization = true;
    bool  digifix8580 = true;
    bool  clockJitterEnabled = true;
    bool  supplyRippleEnabled = true;
    bool  thermalDriftEnabled = true;
    bool  voiceCrosstalkEnabled = true;
    bool  externalBleedEnabled = true;
    float clockJitter = 0.18f;
    float supplyRipple = 0.20f;
    float thermalDrift = 0.10f;
    float voiceCrosstalk = 1.0f;
    float externalBleed = 1.0f;
    float envelopeTDM = 0.0f;
    float d418Asymmetry = 0.0f;
    float filterOhmic = 0.0f;
    float systemNoise = 0.0f;
    float motherboard = 0.0f;
    float adcBleed = 0.0f;
    float busCollision = 0.0f;
    float potInput = 0.0f;
    bool  bitPerfectMode = false;      // Freezes stochastic/forensic runtime variation globally
    bool  forensicFreeze = false;      // alias for host/state codecs that need explicit freeze semantics
    float thermalTimeConstantSeconds = 18.0f;

    // Multi-stage thermal state. These are sanitized in resolveEffectiveForensicConfig
    // and serialized through the existing analogue runtime state, not used as ownership
    // for host parameters. They make freeze/bit-perfect semantics explicit.
    float junctionTempC = 25.0f;
    float caseTempC = 25.0f;
    float ambientTempC = 25.0f;
    float thermalResistanceJC = 8.5f;
    float thermalResistanceCA = 35.0f;
    float thermalCapacitanceJ = 0.012f;
    float thermalCapacitanceC = 0.085f;
    uint64_t lastThermalCycle = 0ull;
    uint32_t frozenNoiseSeed = 0xA5A5A5A5u;

    bool frozen() const noexcept { return bitPerfectMode || forensicFreeze; }
    bool active() const noexcept { return enable && !frozen(); }

    void freezeAtPowerOn() noexcept {
        if (frozen()) return;
        uint32_t seed = static_cast<uint32_t>(ArpSID_sanitizeFloat(junctionTempC, 25.0f) * 10000.0f)
                    ^ static_cast<uint32_t>(ArpSID_sanitizeFloat(supplyVoltage, 5.0f) * 10000.0f)
                    ^ 0xA5A5A5A5u;
        frozenNoiseSeed = ArpSID_xorshift32(seed);
        if (frozenNoiseSeed == 0u) frozenNoiseSeed = 0xA5A5A5A5u;
    }

    void updateThermal(uint64_t currentCycle, float powerDissipationW, Fixed64_32 cyclesToSecondsQ32) noexcept {
        if (frozen()) { lastThermalCycle = currentCycle; return; }
        const uint64_t deltaCycles = currentCycle - lastThermalCycle;
        lastThermalCycle = currentCycle;
        const long double dtLd = (static_cast<long double>(deltaCycles) * static_cast<long double>(cyclesToSecondsQ32)) / 4294967296.0L;
        const float dt = std::clamp(static_cast<float>(dtLd), 0.0f, 0.25f);
        if (dt <= 0.0f) return;
        const float p = std::clamp(std::isfinite(powerDissipationW) ? powerDissipationW : 0.0f, 0.0f, 0.35f);
        const float rJC = std::clamp(thermalResistanceJC, 1.0f, 80.0f);
        const float rCA = std::clamp(thermalResistanceCA, 1.0f, 160.0f);
        const float cJ = std::clamp(thermalCapacitanceJ, 0.001f, 1.0f);
        const float cC = std::clamp(thermalCapacitanceC, 0.001f, 4.0f);
        const float flowJC = (junctionTempC - caseTempC) / rJC;
        junctionTempC += (p - flowJC) * dt / cJ;
        const float flowCA = (caseTempC - ambientTempC) / rCA;
        caseTempC += (flowJC - flowCA) * dt / cC;
        junctionTempC = std::clamp(ArpSID_sanitizeFloat(junctionTempC, 25.0f), 15.0f, 92.0f);
        caseTempC = std::clamp(ArpSID_sanitizeFloat(caseTempC, 25.0f), 15.0f, 68.0f);
    }


    float clampedIntensity() const noexcept {
        return std::clamp(std::isfinite(intensity) ? intensity : 0.0f, 0.0f, 1.0f);
    }
    float active(float amount) const noexcept {
        if (!enable || frozen()) return 0.0f;
        const float a = std::clamp(std::isfinite(amount) ? amount : 0.0f, 0.0f, 1.0f);
        return a * clampedIntensity();
    }
};

struct SidAnalogReadNoise {
    uint32_t seed = 0xA5A5A5A5u;

    void freeze(const ArpSIDForensicConfig& cfg) noexcept {
        seed = cfg.frozenNoiseSeed ? cfg.frozenNoiseSeed : 0xA5A5A5A5u;
    }

    uint8_t get(uint64_t cycle) noexcept {
        if ((cycle & 7ull) == 0ull) {
            seed = ArpSID_xorshift32(seed);
        }
        return static_cast<uint8_t>((seed >> 24u) & 0xF0u);
    }
};

class SIDVoice {
public:
    SIDVoice() { reset(); }

    void reset() {
        phase = 0;
        lfsr = 0x7FFFFFu;
        prevClockBit = false;
        prevMsb = false;
        env_.reset();
        gate = false;
        testBit = false;
        frequency = 0;
        pulseWidth = 2048;
        waveform = Waveform::None;
        level = 1.0f;
        lowFreqMode = false;
        hardRestartCycles = -1;
        model = SIDModel::MOS8580;
        enableAdsrBug6581 = false;
        env_.is6581 = false;
        lastCombinedWave = 0;
        oscReadByte_ = 0;
        combinedWaveState_ = {};
    }

    void randomizeState(uint32_t& rng, bool measuredProfile = false) {
        phase = ArpSID_xorshift32(rng) & kSidPhaseMask24;
        lfsr = (ArpSID_xorshift32(rng) & 0x7FFFFFu);
        if (lfsr == 0u) lfsr = 0x7FFFFFu;
        prevClockBit = ((phase >> 19u) & 1u) != 0u;
        prevMsb = (phase & 0x800000u) != 0u;
        // Measured-like startup stays closer to low envelope values and light analog memory.
        env_.envCounter = measuredProfile ? (uint8_t)(ArpSID_xorshift32(rng) & 0x1Fu)
                                          : (uint8_t)(ArpSID_xorshift32(rng) & 0xFFu);
        env_.level      = (float)env_.envCounter * (1.0f / 255.0f);
        env_.rateCounter = (uint16_t)(ArpSID_xorshift32(rng) & Sid6581Envelope::kRateCounterMask);
        env_.expoCounter = (uint8_t)(ArpSID_xorshift32(rng) & 0x1Fu);
        env_.stage = Sid6581Envelope::Stage::Release;
        hardRestartCycles = -1;
        lastCombinedWave = (uint16_t)(ArpSID_xorshift32(rng) & 0x0FFFu);
        combinedWaveSeed = ArpSID_xorshift32(rng);
        if (combinedWaveSeed == 0u) combinedWaveSeed = 0xA341316Cu;
        gate = false;
    }

    void setModel(SIDModel m) {
        model = m;
        env_.is6581 = (m == SIDModel::MOS6581) || enableAdsrBug6581;
    }
    SIDModel getModel() const { return model; }
    void setEnableAdsrBug6581(bool on) {
        enableAdsrBug6581 = on;
        env_.is6581 = on || (model == SIDModel::MOS6581);
    }


    void setFrequency(uint16_t freq) {
        // Frequency register writes should not fabricate extra oscillator/LFSR state
        // transitions. When TEST is asserted the oscillator is already held at phase 0;
        // simply latch the new register value and let normal hardware sequencing resume.
        frequency = freq;
    }
    void setPulseWidth(uint16_t pw) {
        pulseWidth = static_cast<uint16_t>((pw & 0x0FFFu));
    }
    void setWaveform(uint8_t wave) {
        // Accept both the internal waveform-select nibble (Triangle=0x1,
        // Noise=0x8) and raw SID control-register waveform bits
        // (Triangle=0x10, Noise=0x80). Several low-level render paths and
        // tests exercise this surface directly; treating raw control masks as
        // the low nibble silently selected waveform 0.
        const uint8_t nw = ((wave & 0xF0u) != 0u)
            ? static_cast<uint8_t>((wave >> 4u) & 0x0Fu)
            : static_cast<uint8_t>(wave & 0x0Fu);
        if (((uint8_t)waveform ^ nw) & 0x0Fu) {
            // Preserve oscillator continuity; only invalidate the cached combined-wave
            // helper so the next render derives a fresh output from the new register state.
            lastCombinedWave = 0;
        }
        waveform = static_cast<Waveform>(nw);
    }
    void setAttack(uint8_t v)  { env_.attack  = v & 0x0Fu; }
    void setDecay(uint8_t v)   { env_.decay   = v & 0x0Fu; }
    void setSustain(uint8_t v) { env_.setSustainNibble(v); }
    void setRelease(uint8_t v) { env_.release = v & 0x0Fu; }
    void setTestBit(bool on) {
        const bool changed = (on != testBit);
        testBit = on;
        if (on) {
            phase = 0;
            prevClockBit = false;
            prevMsb = false;
            lfsr = 0x7FFFFFu;
            lastCombinedWave = 0;
            testBitCycleStamp_++;
        } else if (changed) {
            // Releasing TEST should resume from the held reset state rather than inject
            // another synthetic phase/LFSR perturbation.
            prevClockBit = false;
            prevMsb = false;
            if (lfsr == 0u) lfsr = 0x7FFFFFu;
        }
    }

    void setGate(bool on) {
        const bool rising  = (on  && !gate);
        const bool falling = (!on && gate);
        gate = on;
        env_.is6581 = enableAdsrBug6581 || (model == SIDModel::MOS6581);
        if (rising)       env_.gateOn();
        else if (falling) env_.gateOff();
    }

    void scheduleHardRestart() {
        setGate(false);
        env_.is6581 = enableAdsrBug6581 || (model == SIDModel::MOS6581);
        env_.performHardRestart();
        // Use an exact countdown so the re-gate happens on the same service tick
        // that completes the requested hard-restart delay, rather than one tick later.
        hardRestartCycles = sidHardRestartStartCountdown();
    }

    void forceIdle() noexcept {
        gate = false;
        testBit = false;
        hardRestartCycles = -1;
        env_.reset();
        lastSample_ = 0.0f;
    }

    void setLevel(float lvl) { level = std::clamp(lvl, 0.0f, 1.0f); }
    float getLevel() const { return level; }
    void setLowFreqMode(bool on) { lowFreqMode = on; }
    bool getLowFreqMode() const { return lowFreqMode; }

    uint32_t getPhase() const { return phase & kSidPhaseMask24; }
    void resetPhase() { phase = 0; prevClockBit = false; prevMsb = false; }

    bool stepCycle() {
        if (tickHardRestartCountdown_()) {
            setGate(true);
        }

        bool wrapped = false;
        if (!testBit) {
            const uint32_t prev = phase & kSidPhaseMask24;
            // FIX Bug#21: lowFreqMode pitch in stepCycle() was `frequency/100u` (integer
            // division, loses up to 99 units of precision). The cycle path now uses
            // `inc * 0.01` (float, no truncation). Both paths now use the same scaling
            // so LowFreqMode pitch is consistent regardless of which render path is used.
            const uint32_t inc = lowFreqMode
                ? static_cast<uint32_t>(static_cast<double>(frequency) * 0.01 + 0.5)
                : static_cast<uint32_t>(frequency);
            phase = (phase + inc) & kSidPhaseMask24;
            stepNoiseFromPhase(prev, phase);
            wrapped = phase < prev;
            prevMsb = (phase & 0x800000u) != 0;
        } else {
            phase = 0;
            lfsr = 0x7FFFFFu;
            prevClockBit = false;
            prevMsb = false;
        }

        env_.tick();
        return wrapped;
    }

    bool tickHardRestartCountdown_() noexcept {
        if (hardRestartCycles < 0) return false;
        return sidHardRestartTickCountdown(hardRestartCycles);
    }

    float renderFromPhase(const ArpSIDForensicConfig& forensicConfig,
                          bool ringMod = false,
                          bool sourceMsb = false) {
        const uint8_t wfIndex = static_cast<uint8_t>(waveform) & 0x0Fu;
        const uint8_t wf = sidResolveWaveformControlMask(wfIndex);
        const bool hasTri   = (wf & 0x10u) != 0u;
        const bool hasSaw   = (wf & 0x20u) != 0u;
        const bool hasPulse = (wf & 0x40u) != 0u;
        const bool hasNoise = (wf & 0x80u) != 0u;

        uint16_t mix12 = 0;
        if (wf == 0u || testBit) {
            // TEST holds the accumulator at 0 (tri/saw output 0) and the LFSR in
            // reset. The pulse comparator, however, is forced HIGH while TEST is
            // set on real hardware (reSID: `if (test) return 0xfff`) — this is
            // the basis of the test-bit digi technique. Pulse-only under TEST
            // therefore renders full-scale; combined waveforms stay pulled low
            // by the grounded tri/saw bus, matching the pinned v864 closure for
            // noise/tri/saw under TEST.
            mix12 = (testBit && hasPulse && !hasTri && !hasSaw && !hasNoise)
                ? 0x0FFFu : 0u;
        } else if (hasNoise && !hasTri && !hasSaw && !hasPulse) {
            mix12 = generateNoise12();
        } else {
            // Route all combined waveforms through the same analog-combined path
            // so TriSaw obeys the same ring-mod and charge-sharing law as the rest.
            const uint16_t saw12 = (uint16_t)((phase >> 12u) & 0x0FFFu);
            const uint16_t tri12 = generateTriangle12(ringMod, sourceMsb);
            const uint16_t pul12 = hasPulse ? generatePulse12() : 0u;
            const uint16_t noise12 = hasNoise ? generateNoise12() : 0u;

            mix12 = combineWaveforms12(tri12, saw12, pul12, noise12,
                                       hasTri, hasSaw, hasPulse, hasNoise,
                                       forensicConfig);
        }

        // sidDac12ToBipolar_ owns the model-dependent 12-bit DAC law.
        // Do not pre-curve here: pre-applying applyModelDacCurve12() and then
        // calling sidDac12ToBipolar_ would double-apply the 6581 sag/DC model.
        const float osc = sidDac12ToBipolar_(mix12 & 0x0FFFu);
        oscReadByte_ = static_cast<uint8_t>((mix12 >> 4u) & 0xFFu);
        // Scope tap — store post-envelope output for GUI oscilloscope
        const float result = ArpSID_sanitizeFloat(osc * env_.level * level);
        lastSample_ = result;
        return result;
    }

    // FIX Bug#7: processSample() was dead code that also double-ticked the envelope
    // (tickEnvelopeCycle() runs inside stepCycle(), which is called by SIDChip::processSample).
    // Calling this function directly would tick the envelope twice per SID cycle.
    // Removed. All rendering goes through SIDChip::processSample → stepCycle + renderFromPhase.

    float getEnvelopeLevel() const { return env_.level; }
    uint8_t readOscillatorByte() const noexcept { return oscReadByte_; }
    uint8_t readEnvelopeByte() const noexcept { return env_.dacOutput(); }
    uint8_t getWaveformBits() const { return static_cast<uint8_t>(waveform); }
    bool isActive() const { return gate || hardRestartCycles >= 0 || env_.envCounter != 0u; }
    // Oscilloscope tap — most-recently-rendered post-envelope sample
    float getLastSample() const { return lastSample_; }
    void setCombinedWaveSeed(uint32_t seed) noexcept {
        combinedWaveSeed = seed != 0u ? seed : 0xA341316Cu;
    }

    struct Snapshot {
        uint32_t phase = 0;
        uint32_t lfsr = 0x7FFFFFu;
        bool prevClockBit = false;
        bool prevMsb = false;
        Sid6581Envelope::Snapshot envelope{};
        bool gate = false;
        bool testBit = false;
        uint16_t frequency = 0;
        uint16_t pulseWidth = 2048;
        uint8_t waveform = 0;
        float level = 1.0f;
        bool lowFreqMode = false;
        int hardRestartCycles = -1;
        uint16_t lastCombinedWave = 0;
        uint8_t oscReadByte = 0;
        uint32_t combinedWaveSeed = 0xA341316Cu;
        uint8_t lastWaveformWrite = 0u;
        uint32_t testBitCycleStamp = 0u;
    };

    Snapshot snapshot() const noexcept {
        Snapshot s{};
        s.phase = phase & kSidPhaseMask24;
        s.lfsr = lfsr & 0x7FFFFFu;
        if (s.lfsr == 0u) s.lfsr = 0x7FFFFFu;
        s.prevClockBit = prevClockBit;
        s.prevMsb = prevMsb;
        s.envelope = env_.snapshot();
        s.gate = gate;
        s.testBit = testBit;
        s.frequency = frequency;
        s.pulseWidth = pulseWidth & 0x0FFFu;
        s.waveform = static_cast<uint8_t>(waveform) & 0x0Fu;
        s.level = level;
        s.lowFreqMode = lowFreqMode;
        s.hardRestartCycles = hardRestartCycles;
        s.lastCombinedWave = lastCombinedWave & 0x0FFFu;
        s.oscReadByte = oscReadByte_;
        s.combinedWaveSeed = combinedWaveSeed ? combinedWaveSeed : 0xA341316Cu;
        s.lastWaveformWrite = lastWaveformWrite_;
        s.testBitCycleStamp = testBitCycleStamp_;
        return s;
    }

    void restore(const Snapshot& s) noexcept {
        phase = s.phase & kSidPhaseMask24;
        lfsr = s.lfsr & 0x7FFFFFu;
        if (lfsr == 0u) lfsr = 0x7FFFFFu;
        prevClockBit = s.prevClockBit;
        prevMsb = s.prevMsb;
        env_.restore(s.envelope);
        gate = s.gate;
        testBit = s.testBit;
        frequency = s.frequency;
        pulseWidth = s.pulseWidth & 0x0FFFu;
        waveform = static_cast<Waveform>(s.waveform & 0x0Fu);
        level = std::clamp(std::isfinite(s.level) ? s.level : 1.0f, 0.0f, 1.0f);
        lowFreqMode = s.lowFreqMode;
        hardRestartCycles = s.hardRestartCycles;
        lastCombinedWave = s.lastCombinedWave & 0x0FFFu;
        oscReadByte_ = s.oscReadByte;
        combinedWaveSeed = s.combinedWaveSeed ? s.combinedWaveSeed : 0xA341316Cu;
        lastWaveformWrite_ = s.lastWaveformWrite;
        testBitCycleStamp_ = s.testBitCycleStamp;
        combinedWaveState_.stochasticSeed = combinedWaveSeed;
        combinedWaveState_.lastWaveformWrite = lastWaveformWrite_;
        combinedWaveState_.testBitCycleStamp = testBitCycleStamp_;
        env_.is6581 = enableAdsrBug6581 || (model == SIDModel::MOS6581);
    }

    static inline std::once_flag s_tablesInitOnce_;
    static inline std::atomic<bool> s_tablesReady_{false};
    static inline std::array<uint16_t, 4096> s_dac_6581{};
    static inline std::array<uint16_t, 4096> s_dac_8580{};
    static inline std::array<uint16_t, 2048> s_filterCutoff6581Hz_x64{};
    static inline std::array<uint16_t, 2048> s_filterCutoff8580Hz_x64{};
    static inline std::array<uint16_t, 16> s_filterQ6581_x1024{};
    static inline std::array<uint16_t, 16> s_filterQ8580_x1024{};

    static bool sidTablesReady() noexcept {
        return s_tablesReady_.load(std::memory_order_acquire);
    }

    static void initTablesOnce() {
        if (!sidTablesReady() && ArpSID::sidRealtimeGuardActive()) {
            ArpSID::sidRealtimeGuardForbidLateTableInit("SIDVoice::initTablesOnce late first init on realtime thread");
        }
        std::call_once(s_tablesInitOnce_, [](){
            for (uint32_t i = 0; i < 4096u; ++i) {
                const double x = (double)i / 4095.0;
                // Bowed, slightly irregular 6581 DAC transfer; nearly-linear 8580.
                // Model-dependent 12-bit DAC: 8580 is treated as effectively linear,
                // while 6581 uses an analytic sag/DC-bias approximation of the
                // non-ideal NMOS ladder.
                const double y8580 = x;
                const double sag = 0.105 * x * (1.0 - x);
                const double bias = -0.012 * (1.0 - x) + 0.006 * x;
                const double y6581 = std::clamp(x - sag + bias + 0.018 * x * (1.0 - x) * std::sin(2.0 * ArpSID_pi() * x), 0.0, 1.0);
                s_dac_6581[i] = (uint16_t)std::clamp<int>((int)std::lround(y6581 * 4095.0), 0, 4095);
                s_dac_8580[i] = (uint16_t)std::clamp<int>((int)std::lround(y8580 * 4095.0), 0, 4095);
            }

            const auto fillCutoffTable = [](auto& table, const std::array<std::pair<int, double>, 9>& anchors) {
                for (size_t seg = 1; seg < anchors.size(); ++seg) {
                    const int x0 = anchors[seg - 1].first;
                    const int x1 = anchors[seg].first;
                    const double y0 = anchors[seg - 1].second;
                    const double y1 = anchors[seg].second;
                    const int span = std::max(1, x1 - x0);
                    for (int x = x0; x <= x1 && x < (int)table.size(); ++x) {
                        const double t = (double)(x - x0) / (double)span;
                        const double sm = t * t * (3.0 - 2.0 * t);
                        const double hz = y0 + (y1 - y0) * sm;
                        table[(size_t)x] = (uint16_t)std::clamp<int>((int)std::lround(hz * 64.0), 1, 65535);
                    }
                }
                for (size_t i = 1; i < table.size(); ++i) {
                    if (table[i] < table[i - 1]) table[i] = table[i - 1];
                }
            };
            // Named hardware-anchor surfaces for maintainability/documentation.
            static constexpr std::array<std::pair<int, double>, 9> kFilterCutoffAnchors6581 =
                {{{0, 30.0}, {64, 55.0}, {192, 120.0}, {384, 320.0}, {768, 1100.0}, {1152, 2800.0}, {1536, 5400.0}, {1856, 8800.0}, {2047, 11800.0}}};
            static constexpr std::array<std::pair<int, double>, 9> kFilterCutoffAnchors8580 =
                {{{0, 18.0}, {64, 28.0}, {192, 55.0}, {384, 180.0}, {768, 900.0}, {1152, 3100.0}, {1536, 7600.0}, {1856, 13200.0}, {2047, 19800.0}}};
            fillCutoffTable(s_filterCutoff6581Hz_x64, kFilterCutoffAnchors6581);
            fillCutoffTable(s_filterCutoff8580Hz_x64, kFilterCutoffAnchors8580);

            for (int i = 0; i < 16; ++i) {
                const double n = (double)i / 15.0;
                const double q6581 = 0.72 + 0.58 * n + 4.6 * n * n + 2.4 * n * n * n;
                const double q8580 = 0.74 + 0.85 * n + 8.2 * n * n + 6.8 * n * n * n;
                s_filterQ6581_x1024[(size_t)i] = (uint16_t)std::clamp<int>((int)std::lround(q6581 * 1024.0), 1, 65535);
                s_filterQ8580_x1024[(size_t)i] = (uint16_t)std::clamp<int>((int)std::lround(q8580 * 1024.0), 1, 65535);
            }
            s_tablesReady_.store(true, std::memory_order_release);
        });
    }

    // Scope tap — readable by SIDChip::getVoiceLastSample() for oscilloscope display
private:
    // lastSample_ must live in private ahead of envelope — see below
    float lastSample_ = 0.0f;
    friend class SIDChip; // subphase renderer calls env_.tick() / hard-restart helper

    uint32_t phase = 0; // literal 24-bit accumulator held in low 24 bits
    uint32_t lfsr = 0x7FFFFFu;
    bool prevClockBit = false;
    bool prevMsb = false;
    Sid6581Envelope env_{};   // isolated ADSR core — authoritative envelope state
    bool gate = false;
    bool testBit = false;
    uint16_t frequency = 0;
    uint16_t pulseWidth = 2048;
    Waveform waveform = Waveform::None;
    float level = 1.0f;
    bool lowFreqMode = false;
    int hardRestartCycles = -1;
    uint16_t lastCombinedWave = 0;
    uint8_t oscReadByte_ = 0;
    uint32_t combinedWaveSeed = 0xA341316Cu;
    uint8_t lastWaveformWrite_ = 0u;
    uint32_t testBitCycleStamp_ = 0u;
    SidCombinedWaveState combinedWaveState_{};
    SIDModel model = SIDModel::MOS8580;
    bool enableAdsrBug6581 = false;  // user-set flag; synced to env_.is6581 via setModel/setGate

    uint16_t top12() const { return (uint16_t)((phase >> 12u) & 0x0FFFu); }

    uint16_t generateSawtooth12() const { return top12(); }

    uint16_t generateTriangle12(bool ringMod = false, bool sourceMsb = false) const {
        const bool msb = ((phase & 0x800000u) != 0u) ^ (ringMod && sourceMsb);
        const uint16_t triBase = (uint16_t)((phase >> 11u) & 0x0FFFu);
        return msb ? (uint16_t)(triBase ^ 0x0FFFu) : triBase;
    }

    uint16_t generatePulse12() const {
        // v895: delegate to the single canonical comparator authority
        // (sidPulseComparator12 in sid_combined_wave_model.h) shared with the
        // register engine and the C64 readback model, so the $000/$FFF edge
        // cases and the 6581 comparator bias can never drift between engines.
        // TEST is handled by the caller's waveform selection (renderFromPhase),
        // which zeroes/forces the mix before this comparator is consulted.
        return sidPulseComparator12(top12(), pulseWidth, model == SIDModel::MOS6581);
    }

    uint16_t generateNoise12() const {
        // Hardware-authentic: SID noise output uses 8 bits from the LFSR mapped into
        // bits 4-11 of the 12-bit waveform DAC. Bits 0-3 are always zero on real
        // hardware (dead low nibble). This is intentional, not a missing precision bug.
        uint16_t out = 0;
        out |= (uint16_t)(((lfsr >> 22u) & 1u) << 11u);
        out |= (uint16_t)(((lfsr >> 20u) & 1u) << 10u);
        out |= (uint16_t)(((lfsr >> 16u) & 1u) << 9u);
        out |= (uint16_t)(((lfsr >> 13u) & 1u) << 8u);
        out |= (uint16_t)(((lfsr >> 11u) & 1u) << 7u);
        out |= (uint16_t)(((lfsr >>  7u) & 1u) << 6u);
        out |= (uint16_t)(((lfsr >>  4u) & 1u) << 5u);
        out |= (uint16_t)(((lfsr >>  2u) & 1u) << 4u);
        return out & 0x0FFFu;
    }

    void clockNoiseLfsr() {
        const uint32_t fb = ((lfsr >> 22u) ^ (lfsr >> 17u)) & 1u;
        lfsr = ((lfsr << 1u) | fb) & 0x7FFFFFu;
        if (lfsr == 0u) lfsr = 0x7FFFFFu;
    }

    void stepNoiseFromPhase(uint32_t prev, uint32_t next) {
        // Advance the LFSR based on the actual phase delta rather than branchy
        // wrap-specific heuristics. The SID noise clock is tied to phase bit 19,
        // so each crossed 0x100000 period implies one potential LFSR advance.
        const uint32_t delta = (next - prev) & 0xFFFFFFu;
        uint32_t rises = delta >> 20u;
        const bool prevBit = ((prev >> 19u) & 1u) != 0u;
        const bool nextBit = ((next >> 19u) & 1u) != 0u;
        if (!prevBit && nextBit) ++rises;
        rises = std::min<uint32_t>(rises, 32u);
        for (uint32_t i = 0; i < rises; ++i) clockNoiseLfsr();
        prevClockBit = nextBit;
    }

    // v895 split-brain cleanup: the private blendNeighborBits12 / smooth12Tap /
    // bitWeightedLadder12 duplicates were removed. They had NO callers — the
    // live combined-wave law is sidAnalogCombined12_Ultra and the shared
    // sidCombined* helpers in sid_combined_wave_model.h. Dead local copies of
    // an audio law are drift incubators (edit one, the other engines keep the
    // old behavior); any future variant belongs in the shared model header.

    uint16_t computeAnalogCombined12(uint16_t tri12, uint16_t saw12, uint16_t pul12, uint16_t noise12,
                                     bool hasTri, bool hasSaw, bool hasPulse, bool hasNoise,
                                     const ArpSIDForensicConfig& forensicConfig) {
        const bool is6581 = model == SIDModel::MOS6581;
        uint32_t seed = forensicConfig.frozen() ? 0xA341316Cu : combinedWaveSeed;
        uint16_t out = sidAnalogCombined12_Ultra(tri12, saw12, pul12, noise12,
                                                hasTri, hasSaw, hasPulse, hasNoise,
                                                is6581,
                                                lastCombinedWave,
                                                forensicConfig.temperatureCelsius,
                                                forensicConfig.supplyVoltage,
                                                forensicConfig.revision,
                                                seed);
        const uint8_t wfCtrl = static_cast<uint8_t>(((hasTri ? 0x10u : 0u) | (hasSaw ? 0x20u : 0u) | (hasPulse ? 0x40u : 0u) | (hasNoise ? 0x80u : 0u) | (testBit ? 0x08u : 0u)) & 0xF8u);
        combinedWaveState_.stochasticSeed = seed ? seed : 0x87654321u;
        (void)sidAnalogCombined12_Ultra(combinedWaveState_,
                                        static_cast<uint8_t>((tri12 >> 4u) & 0xFFu),
                                        static_cast<uint8_t>((pul12 >> 4u) & 0xFFu),
                                        wfCtrl,
                                        testBitCycleStamp_,
                                        forensicConfig);
        if (hasNoise) lastWaveformWrite_ = static_cast<uint8_t>((out >> 4u) & 0xFFu);
        if (testBit) testBitCycleStamp_ = combinedWaveState_.testBitCycleStamp;
        return testBit ? 0u : out;
    }

    // ADSR rate periods, exponential divider, and tick logic live in Sid6581Envelope (sid_envelope_core.h).

    uint16_t combineWaveforms12(uint16_t tri12, uint16_t saw12, uint16_t pul12, uint16_t noise12,
                                bool hasTri, bool hasSaw, bool hasPulse, bool hasNoise,
                                const ArpSIDForensicConfig& forensicConfig) {
        // Keep the combined-wave authority on the analog reconstruction path.
        // The older tri+saw LUT shortcut was useful musically, but it creates a
        // split authority model where one combination bypasses the same memory /
        // charge-sharing path used by the others.
        const uint16_t analogCombined = computeAnalogCombined12(tri12, saw12, pul12, noise12,
                                                                hasTri, hasSaw, hasPulse, hasNoise,
                                                                forensicConfig);
        lastCombinedWave = static_cast<uint16_t>(analogCombined & 0x0FFFu);
        return lastCombinedWave;
    }

    uint16_t applyModelDacCurve12(uint16_t x) const {
        return (model == SIDModel::MOS6581)
            ? s_dac_6581[x & 0x0FFFu]
            : s_dac_8580[x & 0x0FFFu];
    }

    float sidDac12ToBipolar_(uint16_t x) const noexcept {
        const uint16_t dac = applyModelDacCurve12(x & 0x0FFFu);
        float y = static_cast<float>(dac) * (2.0f / 4095.0f) - 1.0f;
        if (model == SIDModel::MOS6581) {
            const float u = static_cast<float>(x & 0x0FFFu) * (1.0f / 4095.0f);
            y += -0.018f + 0.010f * u;
        }
        return ArpSID_sanitizeFloat(y);
    }
};

inline void ensureSidTablesReadyForNonRealtimeUse(const char* site) noexcept {
    if (SIDVoice::sidTablesReady()) return;
    if (ArpSID::sidRealtimeGuardActive()) {
        ArpSID::sidRealtimeGuardForbidLateTableInit(site ? site : "late SID table init on realtime thread");
        return;
    }
    SIDVoice::initTablesOnce();
}


struct SidFilterParityLaw {
    double cutoffHz = 20.0;
    double q = 0.74;
    double integratorLeak = 0.0;
};

inline SidFilterParityLaw sidComputeFilterParityLaw(SIDModel model,
                                                    uint16_t cutoff,
                                                    uint8_t resonance,
                                                    float thermalDrift,
                                                    float supplyScale,
                                                    uint8_t revision = 3u) noexcept {
    ensureSidTablesReadyForNonRealtimeUse("sidComputeFilterParityLaw requires prewarmed SID tables");
    SidFilterParityLaw out{};
    const uint8_t calibratedRevision = sidCombinedRevisionForModel(revision, model == SIDModel::MOS6581);
    SidAnalogueCalibration analogue = sidDefaultAnalogueCalibration(model == SIDModel::MOS6581 ? SidFamily::MOS6581 : SidFamily::MOS8580, calibratedRevision);
    const uint16_t fcReg = static_cast<uint16_t>(cutoff & 0x07FFu);
    double rawHz = analogue.cutoffAnchors.front().hz;
    for (size_t seg = 1; seg < analogue.cutoffAnchors.size(); ++seg) {
        const auto a0 = analogue.cutoffAnchors[seg - 1];
        const auto a1 = analogue.cutoffAnchors[seg];
        if (fcReg <= a1.reg) {
            const double span = std::max(1.0, static_cast<double>(a1.reg - a0.reg));
            const double t = std::clamp(static_cast<double>(fcReg - a0.reg) / span, 0.0, 1.0);
            const double sm = t * t * (3.0 - 2.0 * t);
            rawHz = static_cast<double>(a0.hz) + (static_cast<double>(a1.hz) - static_cast<double>(a0.hz)) * sm;
            break;
        }
    }
    const double resNorm = (double)(resonance & 0x0Fu) / 15.0;
    const double modelScale = (model == SIDModel::MOS6581) ? 0.92 : 0.99;
    const SidFilterCalibration calibration = kSidFilterCalibrationByRevision[(size_t)calibratedRevision];
    out.cutoffHz = std::max((model == SIDModel::MOS6581) ? 12.0 : 10.0, rawHz * modelScale * calibration.cutoffScale);
    const double thermalCutoff = 1.0 + ((model == SIDModel::MOS6581) ? 0.035 : 0.015) * std::clamp((double)thermalDrift, 0.0, 1.0);
    out.cutoffHz *= std::clamp((double)std::clamp(std::isfinite(supplyScale) ? supplyScale : 1.0f, 0.85f, 1.15f) * thermalCutoff, 0.90, 1.10);
    out.cutoffHz = std::clamp(out.cutoffHz, 8.0, 48000.0);
    out.q = std::clamp(static_cast<double>(analogue.resonanceQ[(size_t)(resonance & 0x0Fu)]) * calibration.resonanceScale, 0.25, 7.0);
    out.integratorLeak = (model == SIDModel::MOS6581)
        ? (0.0030 + 0.0020 * (1.0 - std::min(1.0, out.cutoffHz / 12000.0)) + 0.0008 * resNorm)
        : (0.0007 + 0.0005 * (1.0 - std::min(1.0, out.cutoffHz / 20000.0)) + 0.00012 * resNorm);
    if (model == SIDModel::MOS6581 && (resonance & 0x0Fu) > 8u) {
        out.integratorLeak += 0.018 * calibration.distortionFactor * ((double)((resonance & 0x0Fu) - 8u) / 7.0);
    }
    out.integratorLeak = std::clamp(out.integratorLeak + (1.0 - std::clamp((double)std::clamp(std::isfinite(supplyScale) ? supplyScale : 1.0f, 0.85f, 1.15f), 0.85, 1.15)) * 0.004,
                                    0.0, 0.05);
    return out;
}

inline float sidComputeFilterRipple(SIDModel model,
                                    double& ripplePhase,
                                    double sampleRate,
                                    float supplyRippleAmount) noexcept {
    const double sr = std::max(1.0, sampleRate);
    const double rippleHz = (model == SIDModel::MOS6581) ? 17.0 : 11.0;
    ripplePhase += rippleHz / sr;
    ripplePhase -= std::floor(ripplePhase);
    const double rippleDepth = ((model == SIDModel::MOS6581) ? 0.020 : 0.012)
        * (double)std::clamp(std::isfinite(supplyRippleAmount) ? supplyRippleAmount : 0.0f, 0.0f, 1.0f);
    return (float)(std::sin(2.0 * ArpSID_pi() * ripplePhase) * rippleDepth);
}

class SIDFilter {
public:
    SIDFilter() { reset(); }

    void reset() {
        z1 = z2 = 0.0f;
        lpState = bpState = hpState = 0.0f;
        cutoff = 0;
        resonance = 0;
        revision_ = 5u;
        filterMode = FilterMode::LowPass;
        routeVoice1 = routeVoice2 = routeVoice3 = true;
        model = SIDModel::MOS8580;
        cutoffHz_ = 20.0;
        smoothedCutoffHz_ = 20.0;
        q_ = 0.74;
        smoothedQ_ = 0.74;
        integratorLeak_ = 0.0;
        updateSmoothCoeff_();
    }

    void setModel(SIDModel m) { model = m; updateFilterLaw_(); }
    void setRevision(uint8_t revision) {
        revision_ = sidCombinedRevisionForModel(revision, model == SIDModel::MOS6581);
        updateFilterLaw_();
    }
    void setSampleRate(double sr) {
        const double s = std::max(1.0, sr);
        sampleRate_ = s;
    }
    void setClockFrequency(double f) noexcept {
        if (!sidClockFrequencySupported(f)) return;
        clockFrequency_ = f;
        updateSmoothCoeff_();
    }
    void setCutoff(uint16_t fc) {
        cutoff = (uint16_t)std::min<uint16_t>(fc, 2047u);
        updateFilterLaw_();
    }
    void setResonance(uint8_t r) {
        resonance = r & 0x0Fu;
        updateFilterLaw_();
    }
    void setMode(FilterMode m) { filterMode = m; }
    FilterMode getFilterMode() const { return filterMode; }
    // Collapse the cutoff/Q smoothers onto their configured targets. Used after
    // an engine reconfigures the filter from cold so the first rendered block
    // reflects the real cutoff instead of the 20 Hz reset placeholder (otherwise
    // a filter-routed one-shot hit on the very first block is muted while the
    // smoother is still ramping up from cold).
    void snapSmoothingToTarget() noexcept { smoothedCutoffHz_ = cutoffHz_; smoothedQ_ = q_; }
    void setVoiceRouting(bool v1, bool v2, bool v3) { routeVoice1 = v1; routeVoice2 = v2; routeVoice3 = v3; }
    bool shouldFilterVoice(int i) const { return (i == 0) ? routeVoice1 : (i == 1) ? routeVoice2 : routeVoice3; }
    void setThermalDrift(float amount) { thermalDrift_ = std::clamp(std::isfinite(amount) ? amount : 0.0f, 0.0f, 1.0f); updateFilterLaw_(); }
    void setSupplyScale(float scale) {
        const float clamped = std::clamp(std::isfinite(scale) ? scale : 1.0f, 0.85f, 1.15f);
        if (std::fabs(clamped - supplyScale_) < 1.0e-4f) return;
        supplyScale_ = clamped;
        updateFilterLaw_();
    }
    void randomizeState(uint32_t& rng, bool measuredProfile = false) {
        const float span = measuredProfile ? 0.05f : 0.18f;
        z1 = ArpSID_rand_bipolar(rng) * span;
        z2 = ArpSID_rand_bipolar(rng) * span;
        lpState = ArpSID_rand_bipolar(rng) * span;
        bpState = ArpSID_rand_bipolar(rng) * span;
        hpState = ArpSID_rand_bipolar(rng) * span;
        smoothedCutoffHz_ = cutoffHz_;
        smoothedQ_ = q_;
        updateSmoothCoeff_();
    }

    float process(float input) {
        const double sr = sampleRate_;
        const double nyquistSafe = std::min(48000.0, 0.45 * sr);
        smoothedCutoffHz_ += (cutoffHz_ - smoothedCutoffHz_) * smoothCoeff_;
        smoothedQ_ += (q_ - smoothedQ_) * smoothCoeff_;
        smoothedCutoffHz_ = std::clamp(smoothedCutoffHz_, 8.0, nyquistSafe);
        smoothedQ_ = std::clamp(smoothedQ_, 0.20, 8.0);
        const double sidClock = std::max(1.0, clockFrequency_);
        const float resNorm = (float)resonance * (1.0f / 15.0f);
        const float driveNorm = std::clamp(std::fabs(input), 0.0f, 2.0f) * 0.5f;
        const bool is6581 = (model == SIDModel::MOS6581);
        // Audit #31/#32: route the 6581 op-amp loading cutoff-squash through
        // the canonical shared helper in `sid_filter_core.h`. SidRegisterEngine's
        // filter now calls the SAME helper, so the audit-flagged drift between
        // the two engines is eliminated by construction.
        const double effectiveCutoffHz =
            FilterCore::effectiveCutoffHz_6581_loading(smoothedCutoffHz_, driveNorm, resNorm, is6581);
        const double g = std::clamp(2.0 * ArpSID_pi() * effectiveCutoffHz / sidClock, 1.0e-7, 0.49);
        const double k = 1.0 / std::max(0.20, smoothedQ_);

        float x = ArpSID_sanitizeFloat(input);
        // Audit #31: shared resonance-feedback law.
        const float feedback = FilterCore::resonanceFeedbackCoefficient(resNorm, is6581);
        x -= bpState * feedback;
        // Audit #32: shared 6581 op-amp loading + tanh saturation.
        x = FilterCore::applyModelNonlinearity(x, driveNorm, resNorm, is6581);

        // Audit #31: shared integrator-leak law.
        const double leak1 = FilterCore::integratorLeak1(integratorLeak_);
        const double leak2 = FilterCore::integratorLeak2(integratorLeak_, is6581);
        const double z1Prev = (double)z1;
        const double z2Prev = (double)z2;
        const double hp = ((double)x - (k + g) * z1Prev - z2Prev) / (1.0 + g * (g + k));
        const double bpAccum = z1Prev + g * hp;
        const double lpAccum = z2Prev + g * bpAccum;
        const double bp = bpAccum * leak1;
        const double lp = lpAccum * leak2;
        // Flush denormals from integrator states to prevent silent CPU stalls.
        const float bpF = ArpSID_sanitizeFloat((float)bp);
        const float lpF = ArpSID_sanitizeFloat((float)lp);
        z1 = (std::fabs(bpF) < 1.0e-15f) ? 0.0f : std::clamp(bpF, -8.0f, 8.0f);
        z2 = (std::fabs(lpF) < 1.0e-15f) ? 0.0f : std::clamp(lpF, -8.0f, 8.0f);
        lpState = z2;
        bpState = z1;
        hpState = ArpSID_sanitizeFloat((float)hp);

        // FIX BUG-0010: Remove explicit Notch override. On real SID hardware, filter
        // modes are combinable via bits 4-6 of $D418 (LP=bit4, BP=bit5, HP=bit6).
        // Notch is simply LP+HP active simultaneously. The old override prevented
        // LP+BP+HP (all three) from working, which IS possible on real hardware.
        float out = 0.0f;
        if ((uint8_t)filterMode & (uint8_t)FilterMode::LowPass)  out += lpState;
        if ((uint8_t)filterMode & (uint8_t)FilterMode::BandPass) out += bpState;
        if ((uint8_t)filterMode & (uint8_t)FilterMode::HighPass) out += hpState;

        // Audit #38: ±8 absolute safety net (shared with SidRegisterEngine
        // via `FilterCore::absoluteSafetyClamp`). The legacy ±2 soft-limit
        // is gone; downstream master-volume stage owns the ±1 final clamp.
        const float sanitized = ArpSID_sanitizeFloat(out);
        const float absClamp = FilterCore::absoluteSafetyClamp(sanitized);
        if (sanitized != absClamp) {
            // Diagnostic: record that the absolute safety clamp fired.
            // Real SID rarely (if ever) exceeds ±2 — this counter being
            // non-zero indicates degenerate filter coefficients (resonance
            // approaching feedback instability).
            ++filterAbsClampHitCount_;
        }
        return absClamp;
    }

    // Audit #38 diagnostic: how many samples since boot have triggered the
    // absolute safety clamp (±8). Real SID output rarely exceeds ±2; a
    // non-zero value here indicates degenerate filter coefficients
    // (resonance approaching feedback instability) or extreme drive law.
    uint64_t filterAbsClampHitCount() const noexcept { return filterAbsClampHitCount_; }
    void resetFilterAbsClampHitCount() noexcept { filterAbsClampHitCount_ = 0; }

private:
    float z1 = 0.0f, z2 = 0.0f;
    float lpState = 0.0f, bpState = 0.0f, hpState = 0.0f;
    uint16_t cutoff = 0;
    uint8_t resonance = 0;
    uint8_t revision_ = 3u;
    FilterMode filterMode = FilterMode::LowPass;
    bool routeVoice1 = true, routeVoice2 = true, routeVoice3 = true;
    SIDModel model = SIDModel::MOS8580;
    double cutoffHz_ = 20.0;
    double smoothedCutoffHz_ = 20.0;
    mutable uint64_t filterAbsClampHitCount_ = 0u; // audit #38 diagnostic counter
    double q_ = 0.74;
    double smoothedQ_ = 0.74;
    double integratorLeak_ = 0.0;
    double smoothCoeff_ = 0.0;
    double sampleRate_ = 44100.0;
    double clockFrequency_ = PAL_CLOCK_FREQ;

    void updateSmoothCoeff_() noexcept {
        // process() runs once per SID cycle, so this time constant belongs to
        // the chip clock rather than the host sample rate.
        smoothCoeff_ = 1.0 - std::exp(-1.0 / std::max(1.0, clockFrequency_ * 0.0015));
    }

    void updateFilterLaw_() {
        const SidFilterParityLaw law = sidComputeFilterParityLaw(model, cutoff, resonance, thermalDrift_, supplyScale_, revision_);
        cutoffHz_ = law.cutoffHz;
        q_ = law.q;
        integratorLeak_ = law.integratorLeak;
    }

    float thermalDrift_ = 0.0f;
    float supplyScale_ = 1.0f; 
};

class SIDChip {
public:
    SIDChip() { reset(); }

    void reset() {
        for (auto& v : voices) {
            v.reset();
            v.setModel(model);
            v.setEnableAdsrBug6581(enableAdsrBug6581);
            v.setLevel(1.0f);
        }
        filter.reset();
        filter.setModel(model);
        filter.setRevision(forensicConfig_.revision);
        filter.setSampleRate(sampleRate);
        filter.setThermalDrift(thermalDrift_);
        filter.setSupplyScale(supplyScale_);
        filter.setClockFrequency(clockFrequency);
        masterVolume = 15;
        cycleFrac = 0.0;
        extInSample_ = 0.0f;
        dcIn_ = dcOut_ = 0.0f;
        externalRcLp_ = externalRcHp_ = externalRcPrevIn_ = 0.0f;
        openBusValue_ = openBusTarget_ = openBusInfluence_ = 0.0f;
        openBusLatchedByte_ = 0u;
        openBusDecayCycle_ = 0u;
        driftPhase_ = 0.0f;
        jitterState_ = 0x51D5A1D1u;
        updateDcBlockCoeff();
        limiter.reset(sampleRate);
        plannedSample_ = {};
        for (size_t i = 0; i < 3u; ++i) {
            voiceLevels[i] = 1.0f;
            syncEnabled[i] = false;
            ringModEnabled[i] = false;
        }
        voice3Off = false;
        if (startupRandomized_) {
            uint32_t rng = startupSeed_;
            for (auto& v : voices) v.randomizeState(rng, measuredStartup_);
            filter.randomizeState(rng, measuredStartup_);
            startupSeed_ = ArpSID_mixSeed(rng, 0x51DCAFE1u);
        }
    }

    void setSampleRate(double sr) {
        hostSampleRate_ = std::max(1.0, sr);
        applyEffectiveSampleRate_();
    }
    void setOversamplingFactor(uint8_t factor) noexcept {
        const uint8_t f = (factor >= 8u) ? 8u : (factor >= 4u) ? 4u : (factor >= 2u) ? 2u : 1u;
        oversamplingFactor_ = f;
        applyEffectiveSampleRate_();
    }
    uint8_t oversamplingFactor() const noexcept { return oversamplingFactor_; }
    void setClockFrequency(double f) {
        if (!sidClockFrequencySupported(f)) return;
        clockFrequency = f;
        filter.setClockFrequency(clockFrequency);
        rcCacheDirty_ = true;
    }
    void setStartupRandomization(bool enabled, bool measuredProfile = false, uint32_t seed = 0u) {
        startupRandomized_ = enabled;
        measuredStartup_ = measuredProfile;
        if (seed != 0u) startupSeed_ = seed;
    }
    void setClockJitterAmount(float amount) { clockJitterAmount_ = std::clamp(std::isfinite(amount) ? amount : 0.0f, 0.0f, 1.0f); }
    void setSupplyRippleAmount(float amount) { supplyRippleAmount_ = std::clamp(std::isfinite(amount) ? amount : 0.0f, 0.0f, 1.0f); }
    void setThermalDrift(float amount) {
        thermalDrift_ = std::clamp(std::isfinite(amount) ? amount : 0.0f, 0.0f, 1.0f);
        filter.setThermalDrift(thermalDrift_);
    }
    void setSupplyScale(float scale) {
        supplyScale_ = std::clamp(std::isfinite(scale) ? scale : 1.0f, 0.85f, 1.15f);
        filter.setSupplyScale(supplyScale_);
    }
    void setVoiceCrosstalk(float amount) { voiceCrosstalk_ = std::clamp(std::isfinite(amount) ? amount : 0.0f, 0.0f, 1.0f); }
    void setExternalInputBleed(float amount) { externalInputBleed_ = std::clamp(std::isfinite(amount) ? amount : 0.0f, 0.0f, 1.0f); }
    void setExternalInputSample(float sample) { extInSample_ = ArpSID_sanitizeFloat(sample); }

    void setModel(SIDModel m) {
        model = m;
        for (auto& v : voices) v.setModel(model);
        filter.setModel(model);
        filter.setRevision(forensicConfig_.revision);
        rcCacheDirty_ = true;
    }
    SIDModel getModel() const { return model; }

    void setRevision(uint8_t revision) noexcept {
        forensicConfig_.revision = sidCombinedRevisionForModel(revision, model == SIDModel::MOS6581);
        filter.setRevision(forensicConfig_.revision);
        rcCacheDirty_ = true;
    }
    uint8_t revision() const noexcept { return forensicConfig_.revision; }

    void setEnableAdsrBug6581(bool on) {
        enableAdsrBug6581 = on;
        for (auto& v : voices) v.setEnableAdsrBug6581(on);
    }

    SIDVoice& getVoice(int i) { return voices[(size_t)i]; }
    const SIDVoice& getVoice(int i) const { return voices[(size_t)i]; }
    bool isVoiceActive(int osc) const { return (osc >= 0 && osc < 3) ? voices[(size_t)osc].isActive() : false; }
    // Oscilloscope tap — returns most-recently-rendered sample for OSC i (pre-filter)
    float getVoiceLastSample(int i) const { return (i >= 0 && i < 3) ? voices[(size_t)i].getLastSample() : 0.0f; }
    float getVoiceEnvelopeLevel(int i) const noexcept {
        return (i >= 0 && i < 3)
            ? std::clamp(ArpSID_sanitizeFloat(voices[(size_t)i].getEnvelopeLevel()), 0.0f, 1.0f)
            : 0.0f;
    }
    uint8_t readOsc3() const noexcept { return voices[2].readOscillatorByte(); }
    uint8_t readEnv3() const noexcept { return voices[2].readEnvelopeByte(); }
    float getLastFilterInputSample() const noexcept { return lastFilterInput_; }
    float getLastFilterOutputSample() const noexcept { return lastFilterOutput_; }
    double getCycleFrac() const noexcept { return cycleFrac; }

    float getVoiceLevel(int i) const noexcept {
        return (i >= 0 && i < 3) ? voiceLevels[(size_t)i] : 0.0f;
    }

    void setVoiceLevel(int i, float lvl) {
        if (i < 0 || i >= 3) return;
        voiceLevels[(size_t)i] = std::clamp(lvl, 0.0f, 1.0f);
        voices[(size_t)i].setLevel(voiceLevels[(size_t)i]);
    }
    void setVoiceSyncEnable(int i, bool on) { if (i >= 0 && i < 3) syncEnabled[(size_t)i] = on; }
    void setVoiceRingModEnable(int i, bool on) { if (i >= 0 && i < 3) ringModEnabled[(size_t)i] = on; }
    void setVoice3Off(bool on) { voice3Off = on; writeOpenBus_(on ? 0x80u : 0x00u); }
    bool getVoice3Off() const { return voice3Off; }
    void setDigifix8580(bool on) { digifix8580_ = on; }
    void setForensicConfig(const ArpSIDForensicConfig& cfg) {
        forensicConfig_ = cfg;
        const bool en = cfg.enable;
        setStartupRandomization(en && cfg.startupRandomization, true, startupSeed_);
        // Store raw forensic amounts and apply intensity consistently at render time via
        // forensicConfig_.active(...). This keeps all subpaths responsive when intensity
        // changes and avoids configure-time / render-time scaling skew.
        setClockJitterAmount(en ? std::clamp(cfg.clockJitter, 0.0f, 1.0f) : 0.0f);
        setSupplyRippleAmount(en ? std::clamp(cfg.supplyRipple, 0.0f, 1.0f) : 0.0f);
        setThermalDrift(en ? std::clamp(cfg.thermalDrift, 0.0f, 1.0f) : 0.0f);
        setSupplyScale(std::clamp(std::isfinite(cfg.supplyVoltage) ? (cfg.supplyVoltage / 5.0f) : 1.0f, 0.85f, 1.15f));
        filter.setRevision(cfg.revision);
        runtimeTemperatureCelsius_ = std::clamp(std::isfinite(cfg.temperatureCelsius) ? cfg.temperatureCelsius : 35.0f, 20.0f, 70.0f);
        setVoiceCrosstalk(en ? std::clamp(forensicConfig_.voiceCrosstalk, 0.0f, 1.0f) : 0.0f);
        setExternalInputBleed(en ? std::clamp(forensicConfig_.externalBleed, 0.0f, 1.0f) : 0.0f);
        setDigifix8580(en && cfg.digifix8580);
        combinedWaveSeed_ = cfg.chipIdSeed != 0u ? cfg.chipIdSeed : 0xDEADBEEFu;
        for (size_t i = 0; i < voices.size(); ++i)
            voices[i].setCombinedWaveSeed(ArpSID_mixSeed(combinedWaveSeed_, static_cast<uint32_t>(i) + 1u));
    }
    const ArpSIDForensicConfig& getForensicConfig() const noexcept { return forensicConfig_; }

    void setFilterCutoff(uint16_t fc) { filter.setCutoff(fc); writeOpenBus_(static_cast<uint8_t>(fc & 0xFFu)); }
    void setFilterResonance(uint8_t r) { filter.setResonance(r); writeOpenBus_(static_cast<uint8_t>(r & 0x0Fu)); }
    void setFilterMode(FilterMode m) { filter.setMode(m); writeOpenBus_(static_cast<uint8_t>(m)); }
    void setFilterVoiceRouting(bool v1, bool v2, bool v3) { filter.setVoiceRouting(v1, v2, v3); writeOpenBus_(static_cast<uint8_t>((v1 ? 1u : 0u) | (v2 ? 2u : 0u) | (v3 ? 4u : 0u))); }
    // Snap the filter cutoff/Q smoothers to their configured targets (see
    // SIDFilter::snapSmoothingToTarget) — used after a cold reconfigure so the
    // first rendered block reflects the real cutoff, not the reset placeholder.
    void snapFilterSmoothing() noexcept { filter.snapSmoothingToTarget(); }
    void setMasterVolume(uint8_t vol) { masterVolume = vol & 0x0Fu; writeOpenBus_(static_cast<uint8_t>(masterVolume)); }
    void setExternalRcEnabled(bool enabled) noexcept { externalRcEnabled_ = enabled; }
    bool externalRcEnabled() const noexcept { return externalRcEnabled_; }
    uint8_t readOpenBusByte() const noexcept { return openBusLatchedByte_; }

    struct Snapshot {
        std::array<SIDVoice::Snapshot, 3> voices{};
        std::array<bool, 3> envelopeAdsrDelayHold{};
        float externalRcLp = 0.0f;
        float externalRcHp = 0.0f;
        float externalRcPrevIn = 0.0f;
        float openBusValue = 0.0f;
        float openBusTarget = 0.0f;
        float openBusInfluence = 0.0f;
        uint8_t openBusLatchedByte = 0;
        uint16_t openBusDecayCycle = 0;
        uint8_t masterVolume = 15;
        bool voice3Off = false;
        bool externalRcEnabled = true;
        uint8_t oversamplingFactor = 1;
        uint8_t revision = 3;
        SIDModel model = SIDModel::MOS8580;
    };

    Snapshot snapshot() const noexcept {
        Snapshot s{};
        for (size_t i = 0; i < voices.size(); ++i) {
            s.voices[i] = voices[i].snapshot();
            s.envelopeAdsrDelayHold[i] = s.voices[i].envelope.adsrDelayHold;
        }
        s.externalRcLp = externalRcLp_;
        s.externalRcHp = externalRcHp_;
        s.externalRcPrevIn = externalRcPrevIn_;
        s.openBusValue = openBusValue_;
        s.openBusTarget = openBusTarget_;
        s.openBusInfluence = openBusInfluence_;
        s.openBusLatchedByte = openBusLatchedByte_;
        s.openBusDecayCycle = openBusDecayCycle_;
        s.masterVolume = masterVolume & 0x0Fu;
        s.voice3Off = voice3Off;
        s.externalRcEnabled = externalRcEnabled_;
        s.oversamplingFactor = oversamplingFactor_;
        s.revision = forensicConfig_.revision;
        s.model = model;
        return s;
    }

    void restore(const Snapshot& s) noexcept {
        model = s.model;
        filter.setModel(model);
        forensicConfig_.revision = sidCombinedRevisionForModel(s.revision, model == SIDModel::MOS6581);
        filter.setRevision(forensicConfig_.revision);
        externalRcEnabled_ = s.externalRcEnabled;
        setOversamplingFactor(s.oversamplingFactor);
        externalRcLp_ = ArpSID_sanitizeFloat(s.externalRcLp);
        externalRcHp_ = ArpSID_sanitizeFloat(s.externalRcHp);
        externalRcPrevIn_ = ArpSID_sanitizeFloat(s.externalRcPrevIn);
        openBusValue_ = std::clamp(ArpSID_sanitizeFloat(s.openBusValue), 0.0f, 255.0f);
        openBusTarget_ = std::clamp(ArpSID_sanitizeFloat(s.openBusTarget), 0.0f, 255.0f);
        openBusInfluence_ = std::clamp(ArpSID_sanitizeFloat(s.openBusInfluence), 0.0f, 1.0f);
        openBusLatchedByte_ = s.openBusLatchedByte;
        openBusDecayCycle_ = s.openBusDecayCycle;
        masterVolume = s.masterVolume & 0x0Fu;
        voice3Off = s.voice3Off;
        for (size_t i = 0; i < voices.size(); ++i) {
            voices[i].setModel(model);
            voices[i].setEnableAdsrBug6581(enableAdsrBug6581);
            auto vs = s.voices[i];
            vs.envelope.adsrDelayHold = s.envelopeAdsrDelayHold[i];
            voices[i].restore(vs);
        }
    }

    // Persist/restore names used by AU/VST state layers. This is the canonical
    // serialization payload for non-register analogue memory: envelope rate/expo
    // counters, RC highpass/lowpass state and open-bus decay state.
    Snapshot serializeAnalogueRuntimeState() const noexcept { return snapshot(); }
    void restoreAnalogueRuntimeState(const Snapshot& s) noexcept { restore(s); }

    void scheduleHardRestart() {
        for (auto& v : voices) v.scheduleHardRestart();
    }

    // True native cycle-by-cycle rendering. Advances the chip exactly [cycleStart, cycleEnd)
    // SID clock cycles. Each cycle: oscillators step by their frequency register value,
    // sync/ring-mod are resolved, waveform and envelope are evaluated at exact position.
    // No lerp, no blend between chip copies — this IS the authoritative sound path.
    void renderCycleWindowContribution(uint16_t cycleStart, uint16_t cycleEnd, float& outL, float& outR) {
        outL = outR = 0.0f;
        ensurePlannedSample_();
        if (plannedSample_.totalRenderCount <= 0) return;

        const int start = std::clamp<int>((int)cycleStart, 0, plannedSample_.cyclesTotal);
        const int end   = std::clamp<int>((int)cycleEnd,   start, plannedSample_.cyclesTotal);
        const int span  = end - start;
        if (span <= 0) return;

        // Advance past any subphase cursor within the start cycle.
        if (plannedSample_.renderedSubphase > 0u && plannedSample_.renderedCycles <= start) {
            bool sourceMsbRose[3] = {};
            for (int i = 0; i < 3; ++i) {
                const uint32_t prev = voices[(size_t)i].phase & kSidPhaseMask24;
                uint32_t next = 0u;
                if (!voices[(size_t)i].testBit) {
                    uint32_t advanced = 0u;
                    for (uint16_t sub = plannedSample_.renderedSubphase; sub < kSidSubcycleResolution; ++sub)
                        advanced += subphaseIncrementForVoice_(voices[(size_t)i], sub);
                    next = (prev + advanced) & kSidPhaseMask24;
                    voices[(size_t)i].phase = next;
                } else {
                    voices[(size_t)i].phase = 0u;
                    voices[(size_t)i].lfsr = 0x7FFFFFu;
                    voices[(size_t)i].prevClockBit = false;
                    voices[(size_t)i].prevMsb = false;
                }
                sourceMsbRose[i] = sidPhaseMsbRose(prev, next);
                voices[(size_t)i].prevMsb = (next & 0x800000u) != 0u;
                if (!voices[(size_t)i].testBit) voices[(size_t)i].stepNoiseFromPhase(prev, next);
            }
            for (int i = 0; i < 3; ++i)
                if (sidHardSyncShouldReset(i, syncEnabled.data(), sourceMsbRose)) voices[(size_t)i].resetPhase();
            plannedSample_.renderedSubphase = 0u;
            ++plannedSample_.renderedCycles;
        }

        // Advance to cycleStart (skip cycles before our window).
        while (plannedSample_.renderedCycles < start) {
            stepVoicesOneCycle_();
            ++plannedSample_.renderedCycles;
            plannedSample_.cycleEnvelopeTicked = false;
        }

        // Render exactly [start, end) discrete SID cycles.
        float accum = 0.0f;
        for (int c = start; c < end; ++c) {
            // Step oscillators first (matches hardware: advance then evaluate).
            stepVoicesOneCycle_();
            accum += renderCurrentState_();
            ++plannedSample_.renderedCycles;
            plannedSample_.cycleEnvelopeTicked = false;
        }

        const float denom = static_cast<float>(std::max(1, plannedSample_.totalRenderCount));
        outL = outR = std::clamp(accum / denom, -1.0f, 1.0f);
    }

    // Deterministic fractional-cycle rendering at 8-bit subcycle resolution.
    // Within each SID clock cycle, oscillator phase advances continuously; each
    // fractional step receives its exact proportional share of the full-cycle increment.
    // Oscillator/envelope boundaries follow the SID-cycle lattice. Analogue
    // filter/output state is evaluated at each substep, a bounded numerical
    // approximation rather than a transistor-level continuous-time proof.
    void renderSubCyclePhaseContribution(uint16_t cycleIndex, uint16_t subphaseStart, uint16_t subphaseEnd, float& outL, float& outR) {
        outL = outR = 0.0f;
        ensurePlannedSample_();
        if ((int)cycleIndex >= plannedSample_.cyclesTotal) return;
        if ((int)cycleIndex != plannedSample_.renderedCycles) return;

        const uint16_t s0 = static_cast<uint16_t>(std::clamp<int>(static_cast<int>(subphaseStart), static_cast<int>(plannedSample_.renderedSubphase), static_cast<int>(kSidSubcycleBoundary)));
        const uint16_t s1 = static_cast<uint16_t>(std::clamp<int>(static_cast<int>(subphaseEnd), static_cast<int>(s0), static_cast<int>(kSidSubcycleBoundary)));
        if (s1 <= s0) return;

                float accum = 0.0f;
        uint32_t cyclePhaseBefore[3]{};
        uint32_t cyclePhaseAfter[3]{};
        bool capturedCycleStart = false;

        for (uint16_t sub = s0; sub < s1; ++sub) {
            bool sourceMsbRose[3] = {};
            uint32_t prevPhase[3]{};
            uint32_t nextPhase[3]{};
            for (int i = 0; i < 3; ++i) {
                prevPhase[i] = voices[(size_t)i].phase & kSidPhaseMask24;
                if (!capturedCycleStart) cyclePhaseBefore[i] = prevPhase[i];
                if (!voices[(size_t)i].testBit) {
                    const uint32_t subInc = subphaseIncrementForVoice_(voices[(size_t)i], sub);
                    voices[(size_t)i].phase = (prevPhase[i] + subInc) & kSidPhaseMask24;
                    nextPhase[i] = voices[(size_t)i].phase & kSidPhaseMask24;
                    cyclePhaseAfter[i] = nextPhase[i];
                    sourceMsbRose[i] = sidPhaseMsbRose(prevPhase[i], nextPhase[i]);
                    voices[(size_t)i].prevMsb = (nextPhase[i] & 0x800000u) != 0u;
                } else {
                    voices[(size_t)i].phase = 0u;
                    voices[(size_t)i].lfsr = 0x7FFFFFu;
                    voices[(size_t)i].prevClockBit = false;
                    voices[(size_t)i].prevMsb = false;
                    nextPhase[i] = 0u;
                    cyclePhaseAfter[i] = 0u;
                    sourceMsbRose[i] = false;
                }
            }
            capturedCycleStart = true;
            for (int i = 0; i < 3; ++i)
                if (sidHardSyncShouldReset(i, syncEnabled.data(), sourceMsbRose)) voices[(size_t)i].resetPhase();

            accum += renderCurrentState_();

            if (sub + 1u >= kSidSubcycleBoundary) {
                for (int i = 0; i < 3; ++i) {
                    if (!voices[(size_t)i].testBit) voices[(size_t)i].stepNoiseFromPhase(cyclePhaseBefore[i], cyclePhaseAfter[i]);
                }
            }
        }

        const float denom = static_cast<float>(std::max(1, plannedSample_.totalRenderCount)) *
                            static_cast<float>(kSidSubcycleResolution);
        const float s = std::clamp(accum / denom, -1.0f, 1.0f);
        outL = outR = s;

        plannedSample_.renderedSubphase = s1;
        if (plannedSample_.renderedSubphase >= kSidSubcycleBoundary) {
            serviceCycleBoundary_();
            plannedSample_.renderedSubphase = 0u;
            ++plannedSample_.renderedCycles;
            plannedSample_.cycleEnvelopeTicked = false;
        }
    }

    void finalizePlannedSample(float& outL, float& outR) {
        outL = outR = 0.0f;
        ensurePlannedSample_();
        if (plannedSample_.cyclesTotal <= 0) {
            // Audit #33: zero-cycle path used to emit a frozen state
            // snapshot for every output sample whose budget was < 1 SID
            // cycle (typical at oversampling × very-high host SR). That
            // produced "held samples" instead of true sub-cycle
            // interpolation. The fix below advances each voice's phase
            // by the fractional cycle this sample covers (cycleFracEnd            // cycleFracStart), then renders. Envelopes are NOT ticked
            // (envelopes advance only at whole-cycle boundaries), but
            // oscillator phase does advance fractionally — so two
            // consecutive zero-cycle samples now produce distinct outputs
            // proportional to their fractional positions in the cycle.
            if (!plannedSample_.zeroCycleRendered) {
                const double fracAdvance =
                    std::clamp(plannedSample_.cycleFracEnd - cycleFrac, 0.0, 1.0);
                const float s = renderZeroCycleSnapshot_(fracAdvance);
                outL = outR = s;
                plannedSample_.zeroCycleRendered = true;
                ++zeroCycleSampleCount_; // audit #33 diagnostic
            }
            cycleFrac = plannedSample_.cycleFracEnd;
            plannedSample_.active = false;
            return;
        }
        while (plannedSample_.renderedCycles < plannedSample_.cyclesTotal) {
            stepVoicesOneCycle_();
            const float s = renderCurrentState_();
            outL += s;
            outR += s;
            ++plannedSample_.renderedCycles;
            plannedSample_.cycleEnvelopeTicked = false;
        }
        const float denom = (float)std::max(1, plannedSample_.totalRenderCount);
        outL /= denom;
        outR /= denom;
        cycleFrac = plannedSample_.cycleFracEnd;
        plannedSample_.active = false;
    }

    void processSample(float& outL, float& outR) {
        if (oversamplingFactor_ <= 1u) {
            plannedSample_.active = false;
            ensurePlannedSample_();
            finalizePlannedSample(outL, outR);
            return;
        }
        float accL = 0.0f;
        float accR = 0.0f;
        float norm = 0.0f;
        const uint8_t n = oversamplingFactor_;
        for (uint8_t i = 0; i < n; ++i) {
            float l = 0.0f, r = 0.0f;
            plannedSample_.active = false;
            ensurePlannedSample_();
            finalizePlannedSample(l, r);
            const float phase = (static_cast<float>(i) + 0.5f) / static_cast<float>(n);
            const float w = 0.5f - 0.5f * std::cos(2.0f * static_cast<float>(ArpSID_pi()) * phase);
            accL += l * w;
            accR += r * w;
            norm += w;
        }
        const float inv = norm > 0.0f ? (1.0f / norm) : 1.0f;
        outL = std::clamp(ArpSID_sanitizeFloat(accL * inv), -1.0f, 1.0f);
        outR = std::clamp(ArpSID_sanitizeFloat(accR * inv), -1.0f, 1.0f);
    }

    // Native interval helpers for C64/PSID timed-write rendering. These bypass
    // the host-sample scheduler and advance the SID lattice directly. Calling
    // them invalidates any partially planned host sample; callers that use this
    // surface own the interval cursor externally.
    float advanceSidCyclesNative(int numCycles) noexcept {
        plannedSample_ = {};
        float accum = 0.0f;
        const int cycles = std::max(0, numCycles);
        for (int c = 0; c < cycles; ++c) {
            stepVoicesOneCycle_();
            accum += renderCurrentState_();
            plannedSample_.cycleEnvelopeTicked = false;
        }
        plannedSample_ = {};
        cycleFrac = 0.0;
        return ArpSID_sanitizeFloat(accum);
    }

    float advanceSidSubcyclesNative(uint16_t subphaseStart, uint16_t subphaseEnd) noexcept {
        plannedSample_ = {};
        const uint16_t s0 = static_cast<uint16_t>(
            std::clamp<int>(static_cast<int>(subphaseStart), 0, static_cast<int>(kSidSubcycleBoundary)));
        const uint16_t s1 = static_cast<uint16_t>(
            std::clamp<int>(static_cast<int>(subphaseEnd), static_cast<int>(s0), static_cast<int>(kSidSubcycleBoundary)));
        if (s1 <= s0) {
            cycleFrac = 0.0;
            return 0.0f;
        }

        float accum = 0.0f;
        uint32_t cyclePhaseBefore[3]{};
        uint32_t cyclePhaseAfter[3]{};
        bool capturedCycleStart = false;

        for (uint16_t sub = s0; sub < s1; ++sub) {
            bool sourceMsbRose[3] = {};
            uint32_t prevPhase[3]{};
            uint32_t nextPhase[3]{};
            for (int i = 0; i < 3; ++i) {
                prevPhase[i] = voices[(size_t)i].phase & kSidPhaseMask24;
                if (!capturedCycleStart) cyclePhaseBefore[i] = prevPhase[i];
                auto& v = voices[(size_t)i];
                if (!v.testBit) {
                    const uint32_t subInc = subphaseIncrementForVoice_(v, sub);
                    v.phase = (prevPhase[i] + subInc) & kSidPhaseMask24;
                    nextPhase[i] = v.phase & kSidPhaseMask24;
                    cyclePhaseAfter[i] = nextPhase[i];
                    sourceMsbRose[i] = sidPhaseMsbRose(prevPhase[i], nextPhase[i]);
                    v.prevMsb = (nextPhase[i] & 0x800000u) != 0u;
                } else {
                    v.phase = 0u;
                    v.lfsr = 0x7FFFFFu;
                    v.prevClockBit = false;
                    v.prevMsb = false;
                    nextPhase[i] = 0u;
                    cyclePhaseAfter[i] = 0u;
                }
            }
            capturedCycleStart = true;
            for (int i = 0; i < 3; ++i) {
                if (sidHardSyncShouldReset(i, syncEnabled.data(), sourceMsbRose)) {
                    voices[(size_t)i].resetPhase();
                }
            }
            accum += renderCurrentState_();
        }

        if (s1 >= kSidSubcycleBoundary) {
            for (int i = 0; i < 3; ++i) {
                auto& v = voices[(size_t)i];
                if (!v.testBit) v.stepNoiseFromPhase(cyclePhaseBefore[i], cyclePhaseAfter[i]);
            }
            serviceCycleBoundary_();
            plannedSample_.cycleEnvelopeTicked = false;
        }

        plannedSample_ = {};
        cycleFrac = 0.0;
        return std::clamp(ArpSID_sanitizeFloat(accum / static_cast<float>(s1 - s0)), -1.0f, 1.0f);
    }

private:
    void applyEffectiveSampleRate_() noexcept {
        const uint8_t f = oversamplingFactor_ == 0u ? 1u : oversamplingFactor_;
        sampleRate = std::max(1.0, hostSampleRate_ * static_cast<double>(f));
        updateDcBlockCoeff();
        limiter.reset(sampleRate);
        filter.setSampleRate(sampleRate);
        filter.setClockFrequency(clockFrequency);
        plannedSample_ = {};
    }
    uint32_t currentCycleIncrementForVoice_(const SIDVoice& v) const noexcept {
        return v.getLowFreqMode()
            ? static_cast<uint32_t>(static_cast<double>(v.frequency) * 0.01 + 0.5)
            : static_cast<uint32_t>(v.frequency);
    }

    // Audit #33: render a sub-cycle interpolated snapshot. `fracAdvance`
    // ∈ [0, 1] is the fraction of one SID cycle this output sample covers.
    // We advance each voice's phase by `fracAdvance × fullCycleIncrement`
    // (a true fractional cycle advance), render the resulting state, but
    // do NOT tick envelopes (envelopes only tick at whole cycles).
    //
    // The phase update is permanent — once the next sample's planSample_
    // accumulates `cycleFracStart` to non-zero, the voice phase already
    // reflects the prior sub-cycle position. This keeps phase coherent
    // across the zero-cycle / whole-cycle handoff at the next planning
    // boundary.
    float renderZeroCycleSnapshot_(double fracAdvance) {
        const double fa = std::clamp(fracAdvance, 0.0, 1.0);
        if (fa <= 0.0) {
            return renderCurrentState_();
        }
        for (int i = 0; i < 3; ++i) {
            auto& v = voices[(size_t)i];
            if (v.testBit) continue; // TEST holds phase at 0
            const uint32_t fullInc = currentCycleIncrementForVoice_(v);
            const uint32_t subInc =
                static_cast<uint32_t>(static_cast<double>(fullInc) * fa + 0.5);
            v.phase = (v.phase + subInc) & kSidPhaseMask24;
            v.prevMsb = (v.phase & 0x800000u) != 0u;
        }
        return renderCurrentState_();
    }

    uint32_t subphaseIncrementForVoice_(const SIDVoice& v, uint16_t subphase) const noexcept {
        const uint32_t full = currentCycleIncrementForVoice_(v);
        const uint32_t start = static_cast<uint32_t>((static_cast<uint64_t>(full) * static_cast<uint32_t>(subphase)) / static_cast<uint32_t>(kSidSubcycleResolution));
        const uint32_t end = static_cast<uint32_t>((static_cast<uint64_t>(full) * static_cast<uint32_t>(subphase + 1u)) / static_cast<uint32_t>(kSidSubcycleResolution));
        return end - start;
    }

    struct PlannedSampleState {
        bool active = false;
        bool zeroCycleRendered = false;
        bool cycleEnvelopeTicked = false;
        int cyclesTotal = 0;
        int totalRenderCount = 1;
        int renderedCycles = 0;
        uint16_t renderedSubphase = 0;
        double cycleFracEnd = 0.0;
    } plannedSample_{};

    void ensurePlannedSample_() {
        if (plannedSample_.active) return;
        const double rippleHz = (model == SIDModel::MOS6581) ? 17.0 : 11.0;
        driftPhase_ += (float)(rippleHz / std::max(1.0, sampleRate));
        if (driftPhase_ > 1.0f) driftPhase_ -= 1.0f;
        const bool forensicFrozen = forensicConfig_.frozen();
        const float jitter = forensicFrozen ? 0.0f : (ArpSID_rand_bipolar(jitterState_) * (2.2e-4f * clockJitterAmount_));
        const float thermalTarget = std::clamp(forensicConfig_.temperatureCelsius + std::fabs(lastFilterInput_) * (model == SIDModel::MOS6581 ? 7.5f : 3.0f), 20.0f, 70.0f);
        const float tau = std::clamp(forensicConfig_.thermalTimeConstantSeconds, 1.0f, 180.0f);
        const float thermalCoeff = forensicFrozen ? 0.0f : std::clamp((float)(1.0 / std::max(1.0, sampleRate * (double)tau)), 0.0f, 0.01f);
        runtimeTemperatureCelsius_ += (thermalTarget - runtimeTemperatureCelsius_) * thermalCoeff;
        const float thermalDelta = std::clamp((runtimeTemperatureCelsius_ - 35.0f) * (1.0f / 35.0f), -1.0f, 1.0f);
        const float thermalClock = forensicFrozen ? 0.0f : (((model == SIDModel::MOS6581) ? -7.5e-4f : -3.5e-4f) * thermalDrift_ * (1.0f + 0.55f * thermalDelta));
        const float rippleDepth = forensicFrozen ? 0.0f : (((model == SIDModel::MOS6581) ? 0.020f : 0.012f) * supplyRippleAmount_);
        const float ripple = forensicFrozen ? 0.0f : (std::sin(2.0 * ArpSID_pi() * driftPhase_) * rippleDepth);
        const float modulatedSupply = std::clamp(supplyScale_ * (1.0f + ripple), 0.85f, 1.15f);
        filter.setSupplyScale(modulatedSupply);
        const double forensicClockScale = std::clamp((double)(1.0f + jitter + thermalClock + ripple * 0.08f), 0.95, 1.05);
        (void)forensicClockScale;
        // v903 timing-authority closure: the host dispatcher owns the physical
        // SID-cycle lattice for a host sample. SIDChip must not independently
        // shorten/lengthen the cycle budget with forensic jitter/thermal/ripple,
        // or sub-cycle events can be dispatched for cycles the chip decides do
        // not exist. Keep forensic variation in analogue/filter state above, but
        // budget whole PHI2 cycles from the same nominal clockFrequency/sampleRate
        // law as SidCycleClockState.
        const double budget = cycleFrac + clockFrequency / std::max(1.0, sampleRate);
        const int cycles = std::max(0, (int)std::floor(budget));
        plannedSample_.active = true;
        plannedSample_.zeroCycleRendered = false;
        plannedSample_.cyclesTotal = cycles;
        // FIX 2.1: totalRenderCount must be exactly cycles when cycles>0 so the
        // accumulator is normalized by the actual rendered count, not a fixed 1.
        // When cycles==0 we render exactly one state snapshot (finalizePlannedSample
        // zero-cycle path); set totalRenderCount=1 for that single-sample output.
        plannedSample_.totalRenderCount = (cycles > 0) ? cycles : 1;
        plannedSample_.renderedCycles = 0;
        plannedSample_.renderedSubphase = 0u;
        plannedSample_.cycleEnvelopeTicked = false;
        plannedSample_.cycleFracEnd = budget - (double)cycles;
    }

    void serviceCycleBoundary_() noexcept {
        if (plannedSample_.cycleEnvelopeTicked) return;
        for (auto& v : voices) {
            if (v.tickHardRestartCountdown_()) v.setGate(true);
            v.env_.tick();
        }
        plannedSample_.cycleEnvelopeTicked = true;
    }

    void stepVoicesOneCycle_() {
        // Step all oscillators from the same pre-cycle snapshot so sync decisions
        // observe a single hardware edge epoch instead of a sequentially-mutating
        // voice array. Hard-restart and envelope service still happen once per
        // voice per SID cycle, but phase/MSB transitions are resolved in parallel.
        bool sourceMsbRose[3] = {};
        uint32_t prevPhase[3] = {};
        for (int i = 0; i < 3; ++i) {
            prevPhase[i] = voices[(size_t)i].phase & kSidPhaseMask24;
        }

        for (int i = 0; i < 3; ++i) {
            auto& v = voices[(size_t)i];
            if (v.tickHardRestartCountdown_()) {
                v.setGate(true);
            }

            if (!v.testBit) {
                const uint32_t inc = currentCycleIncrementForVoice_(v);
                const uint32_t next = (prevPhase[i] + inc) & kSidPhaseMask24;
                v.phase = next;
                v.stepNoiseFromPhase(prevPhase[i], next);
                const bool nextMsb = (next & 0x800000u) != 0u;
                sourceMsbRose[i] = sidPhaseMsbRose(prevPhase[i], next);
                v.prevMsb = nextMsb;
            } else {
                v.phase = 0u;
                v.lfsr = 0x7FFFFFu;
                v.prevClockBit = false;
                v.prevMsb = false;
                sourceMsbRose[i] = false;
            }
        }

        for (int i = 0; i < 3; ++i) {
            if (sidHardSyncShouldReset(i, syncEnabled.data(), sourceMsbRose)) {
                voices[(size_t)i].resetPhase();
            }
        }

        serviceCycleBoundary_();
    }

    float renderCurrentState_() noexcept {
                float v[3] = {};
        for (int i = 0; i < 3; ++i) {
            const bool srcMsb = (voices[(size_t)kSidHardSyncSourceOf[i]].getPhase() & 0x800000u) != 0u;
            v[i] = voices[(size_t)i].renderFromPhase(forensicConfig_, ringModEnabled[(size_t)i], srcMsb);
        }

        float filtIn = 0.0f;
        float dry = 0.0f;
        for (int i = 0; i < 3; ++i) {
            const bool suppressDryOnly = (voice3Off && i == 2);
            const float base = v[i];
            float bleed = 0.0f;
            for (int j = 0; j < 3; ++j) {
                if (i == j) continue;
                const float coeff = (model == SIDModel::MOS6581 ? 0.0075f : 0.0035f) * voiceCrosstalk_;
                bleed += v[j] * coeff;
            }
            float tdmSkew = 1.0f;
            if (forensicConfig_.enable && forensicConfig_.envelopeTDM > 0.0f) {
                const float amt = forensicConfig_.active(forensicConfig_.envelopeTDM);
                const float env = std::clamp(voices[(size_t)i].getEnvelopeLevel(), 0.0f, 1.0f);
                const float charge = (model == SIDModel::MOS6581 ? 0.010f : 0.016f) + (model == SIDModel::MOS6581 ? 0.070f : 0.050f) * env;
                const float discharge = (model == SIDModel::MOS6581 ? 0.0015f : 0.0025f) + (model == SIDModel::MOS6581 ? 0.015f : 0.010f) * (1.0f - env);
                const float target = env * (0.75f + 0.25f * std::fabs(base));
                const float coeff = (target > envTdmHold_[(size_t)i]) ? charge : discharge;
                envTdmHold_[(size_t)i] += (target - envTdmHold_[(size_t)i]) * std::clamp(coeff, 0.0f, 1.0f);
                const float held = std::clamp(0.65f * env + 0.35f * envTdmHold_[(size_t)i], 0.0f, 1.0f);
                const float slot = (float)i - 1.0f;
                const float skewBase = (model == SIDModel::MOS6581 ? 0.020f : 0.010f) * amt;
                tdmSkew = 1.0f + skewBase * slot * (0.20f + 0.80f * held);
            }
            const float voiceDc = (model == SIDModel::MOS6581)
                ? ((static_cast<float>(i) - 1.0f) * 0.0035f + 0.0020f * voices[(size_t)i].getEnvelopeLevel())
                : 0.0004f * (static_cast<float>(i) - 1.0f);
            const float mixedVoice = (base + bleed + voiceDc) * tdmSkew;
            const bool routeToFilter = filter.shouldFilterVoice(i) && filter.getFilterMode() != FilterMode::None;
            if (routeToFilter) filtIn += mixedVoice;
            else if (!suppressDryOnly) dry += mixedVoice;
        }
        dry += extInSample_ * ((model == SIDModel::MOS6581 ? 0.016f : 0.009f) * externalInputBleed_);
        filtIn += extInSample_ * ((model == SIDModel::MOS6581 ? 0.010f : 0.005f) * externalInputBleed_);
        lastFilterInput_ = ArpSID_sanitizeFloat(filtIn);
        float filtered = filter.process(filtIn);
        if (forensicConfig_.enable && forensicConfig_.filterOhmic > 0.0f) {
            const float amt = forensicConfig_.active(forensicConfig_.filterOhmic);
            const float currentLoad = std::clamp(std::fabs(filtIn) * (model == SIDModel::MOS6581 ? 0.16f : 0.09f), 0.0f, 1.0f);
            filterOhmicMem_ += (currentLoad - filterOhmicMem_) * (0.010f + 0.030f * amt);
            const float gain = 1.0f - (model == SIDModel::MOS6581 ? 0.16f : 0.08f) * amt * (0.30f + 0.70f * filterOhmicMem_);
            filtered = filtered * gain + filtIn * (model == SIDModel::MOS6581 ? 0.010f : 0.004f) * amt;
        }
        lastFilterOutput_ = ArpSID_sanitizeFloat(filtered);
        float y = dry + filtered;
        if (model == SIDModel::MOS6581) {
            y += 0.018f + 0.0012f * static_cast<float>(masterVolume & 0x0Fu);
        }
        const float volNorm = (masterVolume / 15.0f);
        y *= volNorm;
        if (forensicConfig_.enable && forensicConfig_.d418Asymmetry > 0.0f) {
            const float a = forensicConfig_.active(forensicConfig_.d418Asymmetry);
            const float targetBias = ((model == SIDModel::MOS6581) ? 0.020f : 0.012f) * (15.0f - (float)masterVolume) * (1.0f / 15.0f);
            d418BiasMem_ += (targetBias - d418BiasMem_) * (0.010f + 0.060f * a);
            const float sign = (y >= 0.0f) ? 1.0f : -1.0f;
            const float transition = std::fabs((float)masterVolume - d418PrevVolume_) * (1.0f / 15.0f);
            y = (y + sign * d418BiasMem_ * a) * (1.0f + (0.10f + 0.08f * transition) * a * (1.0f - volNorm));
            d418PrevVolume_ = (float)masterVolume;
        } else {
            d418PrevVolume_ = (float)masterVolume;
        }
        if (digifix8580_ && model == SIDModel::MOS8580 && masterVolume < 15) {
            const float digiDrive = (15.0f - (float)masterVolume) * (1.0f / 15.0f);
            y *= (1.0f + 0.18f * digiDrive);
        }
        if (forensicConfig_.enable && forensicConfig_.systemNoise > 0.0f) {
            const float amt = forensicConfig_.active(forensicConfig_.systemNoise);
            y += ((model == SIDModel::MOS6581) ? 0.0045f : 0.0020f) * amt * ArpSID_rand_bipolar(jitterState_);
        }
        if (forensicConfig_.enable && forensicConfig_.adcBleed > 0.0f) {
            const float amt = forensicConfig_.active(forensicConfig_.adcBleed);
            y += (lastFilterInput_ * ((model == SIDModel::MOS6581) ? 0.010f : 0.006f)
               +  lastFilterOutput_ * ((model == SIDModel::MOS6581) ? 0.006f : 0.003f)) * amt;
        }
        if (forensicConfig_.enable && forensicConfig_.busCollision > 0.0f) {
            const float amt = forensicConfig_.active(forensicConfig_.busCollision);
            const float crush = 1.0f - ((model == SIDModel::MOS6581) ? 0.035f : 0.020f) * amt;
            y = y * crush + lastFilterInput_ * ((model == SIDModel::MOS6581 ? 0.010f : 0.006f) * amt);
        }
        if (forensicConfig_.enable && forensicConfig_.potInput > 0.0f) {
            const float amt = forensicConfig_.active(forensicConfig_.potInput);
            const float potLeak = ((model == SIDModel::MOS6581) ? 0.0040f : 0.0025f) * amt;
            y += potLeak * std::sin(2.0 * ArpSID_pi() * driftPhase_ * 0.5f);
        }
        // The revision calibration DC belongs to the chip's analogue output
        // before the board/DC-blocking stages, and it is owned by the $D418
        // master-volume DAC. Keeping it after the high-pass made volume 0 leak
        // a permanent calibrated DC pedestal; putting it here preserves D418
        // volume-step transients while allowing static DC to settle away.
        const SidAnalogueCalibration outCal = sidDefaultAnalogueCalibration(
            model == SIDModel::MOS6581 ? SidFamily::MOS6581 : SidFamily::MOS8580,
            forensicConfig_.revision);
        const float calibrationGain = std::clamp(outCal.outputGain, 0.10f, 4.0f);
        const float calibrationDc = std::clamp(outCal.dcOffsetMillivolts, -250.0f, 250.0f)
                                  * 0.001f * volNorm;
        y = y * calibrationGain + calibrationDc;

        const float dcOut = y - dcIn_ + dcBlockR_ * dcOut_;
        dcIn_ = ArpSID_sanitizeFloat(y);
        dcOut_ = ArpSID_sanitizeFloat(dcOut);
        // Flush denormals from DC block state — small residuals in quiet passages
        // accumulate in dcIn_/dcOut_ and cause CPU stalls without host FTZ/DAZ.
        if (std::fabs(dcIn_) < 1.0e-15f)  dcIn_  = 0.0f;
        if (std::fabs(dcOut_) < 1.0e-15f) dcOut_ = 0.0f;
        tickOpenBusDecay_();
        float post = dcOut_ + ((openBusValue_ - 127.5f) * (1.0f / 127.5f)) * (0.0012f * openBusInfluence_);
        post = processExternalRc_(post);
        post = post / (1.0f + std::fabs(post));
        if (forensicConfig_.enable && forensicConfig_.motherboard > 0.0f) {
            const float amt = forensicConfig_.active(forensicConfig_.motherboard);
            const float shelf = ((model == SIDModel::MOS6581) ? 0.08f : 0.04f) * amt;
            motherboardLp_ += (post - motherboardLp_) * (0.010f + 0.030f * amt);
            motherboardLp2_ += (motherboardLp_ - motherboardLp2_) * (0.008f + 0.020f * amt);
            motherboardHp_ += (post - motherboardHp_) * (0.0015f + 0.0035f * amt);
            const float motherboardHp = post - motherboardHp_;
            post = post * (1.0f - shelf) + motherboardLp2_ * shelf + motherboardHp * ((model == SIDModel::MOS6581 ? 0.030f : 0.018f) * amt);
        }
        // Raw SID/board output must not hide a lookahead limiter inside the
        // chip core. Musical/plugin limiting belongs in the host output stage.
        // Keep only finite clamping here so raw SID-core tests are physical-path
        // deterministic and latency-free.
        post = ArpSID_sanitizeFloat(post);
        return std::clamp(post, -1.0f, 1.0f);
    }

    struct LookaheadLimiter {
        std::array<float, 1024> buf{};
        std::array<float, 1024> peakVals{};
        // Audit #34: pin counter widths at uint64. The legacy comment here
        // claimed uint32 and "~27 hours at 44 100 Hz" — but the actual
        // declarations below are uint64. With uint64 the overflow horizon
        // is ~13 billion years at 192 kHz, so the practical concern is
        // documentation drift, not real overflow. Static_asserts at the
        // bottom of this struct enforce the widths so the next time someone
        // edits these fields the build catches any silent narrowing.
        //
        // Historical note (Bug #9): the original code used `int`, which
        // wrapped after ~2.1 billion samples (~13.5 hours at 44 100 Hz).
        // On wrap the deque saw `oldest ≈ INT_MIN + delay`, flushed all
        // entries, and the limiter silently stopped working. The uint64
        // upgrade made overflow effectively impossible.
        std::array<uint64_t, 1024> peakIdx{};
        int pos = 0;
        int delay = 240;
        uint64_t dequeHead = 0;
        uint64_t dequeTail = 0;
        uint64_t sampleCounter = 0;
        float env = 1.0f;
        float releaseCoeff = 0.9995f;

        void reset(double sr) {
            buf.fill(0.0f);
            peakVals.fill(0.0f);
            peakIdx.fill(0uLL);
            pos = 0;
            delay = std::clamp((int)std::lround(sr * 0.005), 1, (int)buf.size() - 1);
            if (delay >= (int)peakVals.size()) delay = (int)peakVals.size() - 1;
            dequeHead = dequeTail = 0uLL;
            sampleCounter = 0uLL;
            env = 1.0f;
            releaseCoeff = std::exp(-1.0f / (0.050f * (float)std::max(1.0, sr)));
        }

        float process(float x) {
            buf[(size_t)pos] = x;
            const float ax = std::fabs(x);
            while (dequeTail > dequeHead && peakVals[(size_t)((dequeTail - 1uLL) % (uint64_t)peakVals.size())] <= ax) {
                --dequeTail;
            }
            peakVals[(size_t)(dequeTail % (uint64_t)peakVals.size())] = ax;
            peakIdx[(size_t)(dequeTail % (uint64_t)peakIdx.size())] = sampleCounter;
            ++dequeTail;

            const uint64_t oldest = sampleCounter - (uint64_t)delay + 1uLL;
            while (dequeTail > dequeHead && peakIdx[(size_t)(dequeHead % (uint64_t)peakIdx.size())] < oldest) {
                ++dequeHead;
            }

            const float peak = (dequeTail > dequeHead)
                ? peakVals[(size_t)(dequeHead % (uint64_t)peakVals.size())]
                : 0.0f;
            const float threshold = 0.9885531f; // about -0.1 dBFS
            const float target = (peak > threshold) ? (threshold / peak) : 1.0f;
            if (target < env) env = target;
            else env = 1.0f - (1.0f - env) * releaseCoeff;
            const int readPos = (pos - delay + (int)buf.size()) % (int)buf.size();
            const float y = buf[(size_t)readPos] * env;
            pos = (pos + 1) % (int)buf.size();
            ++sampleCounter;
            return y;
        }
    };
    // Audit #34: pin LookaheadLimiter counter widths. Using direct member
    // access via the type's pointer-to-member works inside the enclosing
    // class without requiring the type to be fully-default-constructible.
    static_assert(sizeof(LookaheadLimiter::sampleCounter) == 8 ||
                  // The field has no static address in C++ before C++17
                  // designated-init takes hold; fall back to declaring an
                  // un-instantiated probe.
                  std::is_same<decltype(LookaheadLimiter::sampleCounter), uint64_t>::value,
                  "audit #34 — LookaheadLimiter::sampleCounter must remain uint64_t");
    static_assert(std::is_same<decltype(LookaheadLimiter::dequeHead), uint64_t>::value,
                  "audit #34 — LookaheadLimiter::dequeHead must remain uint64_t");
    static_assert(std::is_same<decltype(LookaheadLimiter::dequeTail), uint64_t>::value,
                  "audit #34 — LookaheadLimiter::dequeTail must remain uint64_t");

    void writeOpenBus_(uint8_t value) noexcept {
        openBusLatchedByte_ = value;
        openBusDecayCycle_ = 0u;
        openBusValue_ = static_cast<float>(value);
        openBusTarget_ = openBusValue_;
        openBusInfluence_ = std::min(1.0f, openBusInfluence_ + 0.18f);
    }
    void tickOpenBusDecay_() noexcept {
        // Deterministic bit-by-bit open-bus decay over SID cycles. The latch
        // never hard-drops to zero; weak bits clear one at a time, biased by
        // previous writes, then the analogue value slews toward the new latch.
        const uint16_t period = (model == SIDModel::MOS6581) ? 512u : 768u;
        if (++openBusDecayCycle_ >= period) {
            openBusDecayCycle_ = 0u;
            if (openBusLatchedByte_ != 0u) {
                const uint8_t rot = static_cast<uint8_t>((openBusLatchedByte_ * 13u + 5u) & 7u);
                for (uint8_t n = 0; n < 8u; ++n) {
                    const uint8_t bit = static_cast<uint8_t>((rot + n) & 7u);
                    const uint8_t mask = static_cast<uint8_t>(1u << bit);
                    if (openBusLatchedByte_ & mask) { openBusLatchedByte_ = static_cast<uint8_t>(openBusLatchedByte_ & ~mask); break; }
                }
            }
        }
        openBusTarget_ = static_cast<float>(openBusLatchedByte_);
        openBusInfluence_ *= (model == SIDModel::MOS6581) ? 0.99984f : 0.99990f;
        openBusValue_ += (openBusTarget_ - openBusValue_) * ((model == SIDModel::MOS6581) ? 0.0030f : 0.0018f);
        if (std::fabs(openBusValue_) < 0.01f && openBusLatchedByte_ == 0u) openBusValue_ = 0.0f;
        if (std::fabs(openBusInfluence_) < 1.0e-5f) openBusInfluence_ = 0.0f;
    }
    void rebuildExternalRcCache_() noexcept {
        const double sidClock = std::max(1.0, clockFrequency);
        const SidAnalogueCalibration cal = sidDefaultAnalogueCalibration(model == SIDModel::MOS6581 ? SidFamily::MOS6581 : SidFamily::MOS8580, forensicConfig_.revision);
        const float lpHz = std::clamp(cal.externalRcLowpassHz, 1000.0f, static_cast<float>(0.45 * sidClock));
        const float hpHz = std::clamp(cal.externalRcHighpassHz, 1.0f, 200.0f);
        cachedRcLpA_ = std::clamp(1.0f - std::exp(-2.0f * static_cast<float>(ArpSID_pi()) * lpHz / static_cast<float>(sidClock)), 0.0f, 1.0f);
        cachedRcHpA_ = std::clamp(std::exp(-2.0f * static_cast<float>(ArpSID_pi()) * hpHz / static_cast<float>(sidClock)), 0.0f, 0.999999f);
        rcCacheDirty_ = false;
    }

    float processExternalRc_(float x) noexcept {
        if (!externalRcEnabled_) return x;
        if (rcCacheDirty_) rebuildExternalRcCache_();
        externalRcLp_ = ArpSID_sanitizeFloat(externalRcLp_ + (x - externalRcLp_) * cachedRcLpA_);
        const float hp = cachedRcHpA_ * (externalRcHp_ + externalRcLp_ - externalRcPrevIn_);
        externalRcPrevIn_ = externalRcLp_;
        externalRcHp_ = ArpSID_sanitizeFloat(hp);
        return ArpSID_sanitizeFloat(externalRcHp_);
    }
    std::array<SIDVoice, 3> voices;
    std::array<float, 3> voiceLevels{};
    std::array<bool, 3> syncEnabled{};
    std::array<bool, 3> ringModEnabled{};
    bool voice3Off = false;
    SIDFilter filter;
    uint8_t masterVolume = 15;
    double hostSampleRate_ = 44100.0;
    double sampleRate = 44100.0;
    // Audit #33 diagnostic: how many times the zero-cycle interpolation
    // path fired. Non-zero indicates the chip is running at oversampling    // boosted SR > clock, which now produces true sub-cycle interpolation
    // instead of the legacy "held sample".
    mutable uint64_t zeroCycleSampleCount_ = 0u;
public:
    uint64_t zeroCycleSampleCount() const noexcept { return zeroCycleSampleCount_; }
    void resetZeroCycleSampleCount() noexcept { zeroCycleSampleCount_ = 0u; }
private:
    uint8_t oversamplingFactor_ = 1u;
    double clockFrequency = PAL_CLOCK_FREQ;
    double cycleFrac = 0.0;
    bool startupRandomized_ = true;
    bool measuredStartup_ = true;
    bool digifix8580_ = true;
    ArpSIDForensicConfig forensicConfig_{};
    float motherboardLp_ = 0.0f;
    float motherboardLp2_ = 0.0f;
    float motherboardHp_ = 0.0f;
    std::array<float, 3> envTdmHold_{};
    float d418BiasMem_ = 0.0f;
    float d418PrevVolume_ = 15.0f;
    float filterOhmicMem_ = 0.0f;
    float runtimeTemperatureCelsius_ = 35.0f;
    float clockJitterAmount_ = 0.18f;
    float supplyRippleAmount_ = 0.20f;
    float thermalDrift_ = 0.10f;
    float supplyScale_ = 1.0f;
    bool externalRcEnabled_ = true;
    float externalRcLp_ = 0.0f;
    float externalRcHp_ = 0.0f;
    float externalRcPrevIn_ = 0.0f;
    float cachedRcLpA_ = 0.0f;
    float cachedRcHpA_ = 0.0f;
    bool rcCacheDirty_ = true;
    float openBusValue_ = 0.0f;
    float openBusTarget_ = 0.0f;
    float openBusInfluence_ = 0.0f;
    uint8_t openBusLatchedByte_ = 0u;
    uint16_t openBusDecayCycle_ = 0u;
    float voiceCrosstalk_ = 1.0f;
    float externalInputBleed_ = 1.0f;
    float extInSample_ = 0.0f;
    float driftPhase_ = 0.0f;
    uint32_t jitterState_ = 0x51D5A1D1u;
    uint32_t combinedWaveSeed_ = 0xDEADBEEFu;
    uint32_t startupSeed_ = 0x13579BDFu;
    float dcBlockR_ = 0.0f; // updated from sample rate in updateDcBlockCoeff()
    SIDModel model = SIDModel::MOS8580;
    bool enableAdsrBug6581 = false;
    float dcIn_ = 0.0f;
    float dcOut_ = 0.0f;
    float lastFilterInput_ = 0.0f;
    float lastFilterOutput_ = 0.0f;
    LookaheadLimiter limiter;

    void updateDcBlockCoeff() {
        dcBlockR_ = static_cast<float>(std::clamp(std::exp(-2.0 * ArpSID_pi() * 16.0 / std::max(1.0, sampleRate)), 0.0, 0.99999));
    }
};


inline bool sidTablesPrewarmed() noexcept {
    return SIDVoice::sidTablesReady();
}

inline void prewarmAllSidTables() noexcept {
    SIDVoice::initTablesOnce();
}

inline void requireSidTablesPrewarmedForRealtime(const char* site) noexcept {
    // Realtime contract: guard only. Never repair this by invoking
    // SIDVoice::initTablesOnce()/std::call_once from render; table builds are
    // non-realtime and must be done by component/plugin initialize.
    if (!sidTablesPrewarmed()) {
        ArpSID::sidRealtimeGuardForbidLateTableInit(site);
    }
}
} // namespace ArpSID
