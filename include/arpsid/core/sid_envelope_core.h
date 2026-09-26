// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once

#include <array>
#include <cstdint>
#include <algorithm>

namespace ArpSID {

/// Isolated, authoritative MOS6581/MOS8580 ADSR envelope state machine.
///
/// Owns all envelope state: rate counter, exponential-decay divider,
/// envelope level byte, and stage FSM. Drives both the BitPerfect
/// (SIDVoice) and the SidRegisterEngine (Voice) render paths from a
/// single canonical implementation.
///
/// Hardware reference: SID die analysis, reSID 1.0 documentation.
struct Sid6581Envelope {

    // ----------------------------------------------------------------------
    // Stage (mirrors hardware ADSR FSM)
    // ----------------------------------------------------------------------
    enum class Stage : uint8_t { Attack, Decay, Sustain, Release };

    // ----------------------------------------------------------------------
    // Runtime state
    // ----------------------------------------------------------------------
    uint8_t  envCounter  = 0;              // 0-255 hardware envelope level byte
    uint16_t rateCounter = 0;              // φ₂-cycle rate divider
    uint8_t  expoCounter = 0;              // exponential-decay step divider
    Stage    stage       = Stage::Release;

    // ----------------------------------------------------------------------
    // ADSR parameter nibbles (0-15)
    // ----------------------------------------------------------------------
    uint8_t attack  = 0;
    uint8_t decay   = 0;
    uint8_t sustain = 0;   // actual level = nibble * 17 (0-255)
    uint8_t release = 0;

    // ----------------------------------------------------------------------
    // Model flag
    // false (8580): reset rateCounter/expoCounter on every gate edge
    // true (6581): preserve counters → authentic ADSR delay bug
    // ----------------------------------------------------------------------
    bool is6581 = false;
    bool adsrDelayHold = false;
    uint8_t hardRestartWindowCycles = 0; // bounded 6581 restart discharge window, cycle-accurate service-side countdown

    // ----------------------------------------------------------------------
    // Output (updated every tick())
    // ----------------------------------------------------------------------
    float level = 0.0f;    // normalised 0..1


    struct Snapshot {
        uint8_t envCounter = 0;
        uint16_t rateCounter = 0;
        uint8_t expoCounter = 0;
        uint8_t stage = static_cast<uint8_t>(Stage::Release);
        uint8_t attack = 0;
        uint8_t decay = 0;
        uint8_t sustain = 0;
        uint8_t release = 0;
        bool is6581 = false;
        bool adsrDelayHold = false;
        uint8_t hardRestartWindowCycles = 0;
    };

    Snapshot snapshot() const noexcept {
        Snapshot s{};
        s.envCounter = envCounter;
        s.rateCounter = static_cast<uint16_t>(rateCounter & kRateCounterMask);
        s.expoCounter = expoCounter;
        s.stage = static_cast<uint8_t>(stage);
        s.attack = attack & 0x0Fu;
        s.decay = decay & 0x0Fu;
        s.sustain = sustain & 0x0Fu;
        s.release = release & 0x0Fu;
        s.is6581 = is6581;
        s.adsrDelayHold = adsrDelayHold;
        s.hardRestartWindowCycles = hardRestartWindowCycles;
        return s;
    }

    void restore(const Snapshot& s) noexcept {
        envCounter = s.envCounter;
        rateCounter = static_cast<uint16_t>(s.rateCounter & kRateCounterMask);
        expoCounter = s.expoCounter;
        const uint8_t st = std::min<uint8_t>(s.stage, static_cast<uint8_t>(Stage::Release));
        stage = static_cast<Stage>(st);
        attack = s.attack & 0x0Fu;
        decay = s.decay & 0x0Fu;
        sustain = s.sustain & 0x0Fu;
        release = s.release & 0x0Fu;
        is6581 = s.is6581;
        adsrDelayHold = s.adsrDelayHold;
        hardRestartWindowCycles = std::min<uint8_t>(s.hardRestartWindowCycles, 46u);
        level = static_cast<float>(dacOutput()) * (1.0f / 255.0f);
    }

    // ----------------------------------------------------------------------
    // Hardware constants — rate periods from SID die analysis / reSID
    // ----------------------------------------------------------------------
    // v854 exactness fix: rate 15 (8 s) is 31251 on the die (reSID die-measured
    // LFSR period table), not the naive 8s*1MHz/256 = 31250 rounding.
    static constexpr uint16_t kRatePeriods[16] = {
        9, 32, 63, 95, 149, 220, 267, 313,
        392, 977, 1954, 3126, 3907, 11720, 19532, 31251
    };
    static constexpr uint16_t kRateCounterMask = 0x7FFFu;

    // v854 exactness fix: on hardware the exponential-counter period LATCHES when
    // the envelope counter REACHES 0xFF/93/54/26/14/6/0, so the decrement FROM
    // level 93 already costs divisor 2 (the comparator fired when the counter
    // became 93). The level→divisor map is therefore strict-greater at each
    // boundary: [94..255]→1, [55..93]→2, [27..54]→4, [15..26]→8, [7..14]→16,
    // [0..6]→30 (level 0 is frozen by the stage logic regardless).
    static constexpr std::array<uint8_t, 256> kExpoDivByLevel = []() constexpr {
        std::array<uint8_t, 256> table{};
        for (std::size_t i = 0; i < table.size(); ++i) {
            const uint8_t e = static_cast<uint8_t>(i);
            table[i] = (e > 93u) ? 1u
                     : (e > 54u) ? 2u
                     : (e > 26u) ? 4u
                     : (e > 14u) ? 8u
                     : (e >  6u) ? 16u
                                 : 30u;
        }
        return table;
    }();

    static constexpr uint8_t expoDiv(uint8_t e) noexcept {
        return kExpoDivByLevel[e];
    }

    [[maybe_unused]] static constexpr uint8_t bitReverse8(uint8_t v) noexcept {
        v = static_cast<uint8_t>(((v & 0xF0u) >> 4u) | ((v & 0x0Fu) << 4u));
        v = static_cast<uint8_t>(((v & 0xCCu) >> 2u) | ((v & 0x33u) << 2u));
        v = static_cast<uint8_t>(((v & 0xAAu) >> 1u) | ((v & 0x55u) << 1u));
        return v;
    }

    uint8_t dacOutput() const noexcept {
        // The 8-bit envelope counter drives the envelope DAC directly on BOTH
        // the 6581 and 8580 — the counter value IS the per-sample volume
        // multiplier (0..255). An earlier revision bit-reversed the 6581
        // counter here on the mistaken belief that the 6581 DAC ladder is
        // reversed; it is not. That scrambled every amplitude level (quiet
        // attacks became loud, sustain levels jumbled), breaking the sound and
        // perceived envelope timing of EVERY 6581 voice. The genuine 6581 DAC
        // nonlinearity is a mild, MONOTONIC bit-weight error (modeled in the
        // analogue/waveform DAC path + calibration), never a bit reversal.
        return envCounter;
    }

    // ----------------------------------------------------------------------
    // Queries
    // ----------------------------------------------------------------------
    uint8_t sustainLevel() const noexcept {
        return static_cast<uint8_t>((sustain & 0x0Fu) * 17u);
    }

    uint16_t currentPeriod() const noexcept {
        switch (stage) {
            case Stage::Attack:  return kRatePeriods[attack  & 0x0Fu];
            case Stage::Decay:   return kRatePeriods[decay   & 0x0Fu];
            case Stage::Sustain: return kRatePeriods[decay   & 0x0Fu]; // Sustain clocks at Decay rate
            case Stage::Release: return kRatePeriods[release & 0x0Fu];
        }
        return kRatePeriods[release & 0x0Fu];
    }

    // ----------------------------------------------------------------------
    // Parameter setters
    // ----------------------------------------------------------------------
    void setAttackNibble (uint8_t a) noexcept { attack  = a & 0x0Fu; }
    void setDecayNibble  (uint8_t d) noexcept { decay   = d & 0x0Fu; }
    void setReleaseNibble(uint8_t r) noexcept { release = r & 0x0Fu; }

    void setSustainNibble(uint8_t s) noexcept {
        sustain = s & 0x0Fu;
        onSustainChanged();
    }

    void setADFromByte(uint8_t adReg) noexcept {
        attack = (adReg >> 4) & 0x0Fu;
        decay  =  adReg       & 0x0Fu;
    }

    void setSRFromByte(uint8_t srReg) noexcept {
        sustain = (srReg >> 4) & 0x0Fu;
        release =  srReg       & 0x0Fu;
        onSustainChanged();
    }

    /// Adjust running envelope after sustain nibble changes.
    /// Guard: never leave the FSM stuck in Decay when automation moves the
    /// target across the current byte-domain level. The SID compares against the
    /// current sustain DAC target, not against a stale state label.
    void onSustainChanged() noexcept {
        const uint8_t target = sustainLevel();
        if (stage == Stage::Decay) {
            if (envCounter <= target) {
                envCounter = target;
                stage = Stage::Sustain;
                rateCounter = 0;
                expoCounter = 0;
            } else {
                // Keep decaying toward the new lower target, but restart the expo
                // divider so the transition is bounded and deterministic.
                expoCounter = 0;
            }
        } else if (stage == Stage::Sustain) {
            if (envCounter > target) {
                stage = Stage::Decay;
                expoCounter = 0;
            } else {
                envCounter = target;
                rateCounter = 0;
                expoCounter = 0;
            }
        }
    }

    void performHardRestart() noexcept {
        // 6581 hard restart discharge window: hold the envelope DAC at zero for
        // exactly 46 SID cycles. This is deliberately a countdown rather than a
        // host-time delay so render interval subdivision cannot move the restart.
        envCounter = 0;
        rateCounter = 0;
        expoCounter = 0;
        adsrDelayHold = false;
        hardRestartWindowCycles = 46u;
        stage = Stage::Release;
        level = static_cast<float>(dacOutput()) / 255.0f;
    }

    void performHardRestart(uint64_t /*currentCycle*/) noexcept {
        performHardRestart();
    }

    // ----------------------------------------------------------------------
    // Gate control
    // ----------------------------------------------------------------------
    /// Rising gate edge → Attack.
    /// 8580: reset rateCounter + expoCounter (clean re-trigger).
    /// 6581: preserve counters → authentic 1-cycle ADSR delay bug.
    void gateOn() noexcept {
        // A rising gate edge ends any pending hard-restart discharge window.
        // Otherwise, when scheduleHardRestart() re-asserts the gate while the
        // 46-cycle discharge countdown is still active (the countdown is serviced
        // twice per SID cycle, so it elapses well inside the window), tick() would
        // keep forcing stage=Release/level=0 and the Attack set here would be
        // clobbered — leaving the voice stuck in Release at zero (silent note).
        hardRestartWindowCycles = 0;
        if (!is6581) { rateCounter = 0; expoCounter = 0; }
        adsrDelayHold = is6581;
        stage = Stage::Attack;
    }

    /// Falling gate edge → Release (same counter-reset policy as gateOn).
    void gateOff() noexcept {
        if (!is6581) { rateCounter = 0; expoCounter = 0; hardRestartWindowCycles = 0; }
        // Regular note-off is Release, not forced zero. performHardRestart() is the
        // explicit 46-cycle discharge primitive used by hard-restart scheduling.
        adsrDelayHold = is6581;
        stage = Stage::Release;
    }

    // ----------------------------------------------------------------------
    // Reset
    // ----------------------------------------------------------------------
    void reset() noexcept {
        envCounter = 0; rateCounter = 0; expoCounter = 0; adsrDelayHold = false; hardRestartWindowCycles = 0;
        stage = Stage::Release;
        level = 0.0f;
    }

    // ----------------------------------------------------------------------
    // Core tick — advance one SID φ₂ clock cycle
    // ----------------------------------------------------------------------
    void tick(uint32_t cycles, uint64_t /*currentCycle*/) noexcept {
        while (cycles-- > 0u) tick();
    }

    void tick() noexcept {
        if (hardRestartWindowCycles > 0u) {
            --hardRestartWindowCycles;
            envCounter = 0;
            rateCounter = 0;
            expoCounter = 0;
            adsrDelayHold = false;
            stage = Stage::Release;
            level = static_cast<float>(dacOutput()) / 255.0f;
            return;
        }
        rateCounter = static_cast<uint16_t>((rateCounter + 1u) & kRateCounterMask);
        if (adsrDelayHold) {
            adsrDelayHold = false;
            level = static_cast<float>(dacOutput()) / 255.0f;
            return;
        }
        if (rateCounter != currentPeriod()) {
            level = static_cast<float>(dacOutput()) / 255.0f;
            return;
        }
        rateCounter = 0;

        const uint8_t target = sustainLevel();
        switch (stage) {
            case Stage::Attack:
                if (envCounter < 255u) ++envCounter;
                // Attack reaches 0xff and changes directly to Decay on this exact
                // rate-counter event. Do not leave Attack alive for one more period.
                if (envCounter == 255u) { stage = Stage::Decay; expoCounter = 0; adsrDelayHold = false; }
                break;

            case Stage::Decay:
                if (envCounter > target) {
                    if (++expoCounter >= expoDiv(envCounter)) { expoCounter = 0; --envCounter; }
                    if (envCounter <= target) { envCounter = target; stage = Stage::Sustain; expoCounter = 0; }
                } else {
                    envCounter = target; stage = Stage::Sustain; expoCounter = 0;
                }
                break;

            case Stage::Sustain:
                if (envCounter > target) { stage = Stage::Decay; expoCounter = 0; }
                else envCounter = target;
                break;

            case Stage::Release:
                if (envCounter > 0u) {
                    if (++expoCounter >= expoDiv(envCounter)) { expoCounter = 0; --envCounter; }
                }
                break;
        }
        level = static_cast<float>(dacOutput()) / 255.0f;
    }
};

} // namespace ArpSID
