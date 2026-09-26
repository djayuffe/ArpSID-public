// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once

#include "arpsid/core/sid_combined_wave_model.h"
#include "arpsid/core/sid_envelope_core.h"
#include <array>
#include <cstdint>

namespace ArpSID::C64 {

// Digital SID bus-read model clocked directly from C64 PHI2 timestamps.
// It exists separately from host-sample audio rendering because a 6510 can
// read OSC3/ENV3 between two host samples. Advancing readback from writes or
// rendered audio samples fabricates state and makes polling loops host-rate
// dependent.
class SidReadbackModel {
public:
    static constexpr bool kCycleExact = true;
    static constexpr uint32_t kPotConversionCycles = 512u;

    void reset(bool mos6581 = true) noexcept {
        is6581_ = mos6581;
        for (auto& v : voices_) {
            v = Voice{};
            v.env.is6581 = is6581_;
        }
        potTargetX_ = potTargetY_ = 0xFFu;
        potLatchedX_ = potLatchedY_ = 0xFFu;
        potCycle_ = 0;
        clockCursor_ = 0;
        clockStarted_ = false;
    }

    void setModel6581(bool mos6581) noexcept {
        is6581_ = mos6581;
        for (auto& v : voices_) v.env.is6581 = is6581_;
    }

    void setPotTargets(uint8_t x, uint8_t y) noexcept {
        potTargetX_ = x;
        potTargetY_ = y;
    }

    void write(uint64_t phi2, uint8_t reg, uint8_t value) noexcept {
        advanceTo(phi2);
        reg &= 0x1Fu;
        if (reg >= 0x15u) return;
        const uint8_t voice = static_cast<uint8_t>(reg / 7u);
        if (voice >= voices_.size()) return;
        Voice& v = voices_[voice];
        const uint8_t local = static_cast<uint8_t>(reg % 7u);
        switch (local) {
            case 0: v.freq = uint16_t((v.freq & 0xFF00u) | value); break;
            case 1: v.freq = uint16_t((uint16_t(value) << 8u) | (v.freq & 0x00FFu)); break;
            case 2: v.pulse = uint16_t((v.pulse & 0x0F00u) | value); break;
            case 3: v.pulse = uint16_t((uint16_t(value & 0x0Fu) << 8u) | (v.pulse & 0x00FFu)); break;
            case 4: writeControl_(v, value); break;
            case 5: v.env.setADFromByte(value); break;
            case 6: v.env.setSRFromByte(value); break;
            default: break;
        }
        if (voice == 2u) updateOscillatorByte_(2u);
    }

    uint8_t read(uint64_t phi2, uint8_t reg, uint8_t openBus = 0xFFu) noexcept {
        advanceTo(phi2);
        switch (reg & 0x1Fu) {
            case 0x19u: return potLatchedX_;
            case 0x1Au: return potLatchedY_;
            case 0x1Bu: return voices_[2].oscReadByte;
            case 0x1Cu: return voices_[2].env.dacOutput();
            default: return openBus;
        }
    }

    void advanceTo(uint64_t phi2) noexcept {
        if (!clockStarted_) {
            clockStarted_ = true;
            clockCursor_ = phi2;
            return;
        }
        if (phi2 <= clockCursor_) return;
        uint64_t cycles = phi2 - clockCursor_;
        while (cycles-- != 0u) clockOne_();
        clockCursor_ = phi2;
    }

    uint32_t phase(uint8_t voice) const noexcept {
        return voice < voices_.size() ? voices_[voice].phase : 0u;
    }
    uint8_t envelope(uint8_t voice) const noexcept {
        return voice < voices_.size() ? voices_[voice].env.dacOutput() : 0u;
    }

private:
    struct Voice {
        uint32_t phase = 0;
        uint32_t lfsr = 0x7FFFFFu;
        uint16_t freq = 0;
        uint16_t pulse = 0;
        uint8_t control = 0;
        ArpSID::Sid6581Envelope env{};
        uint16_t lastCombined = 0;
        uint32_t combinedSeed = 0xA341316Cu;
        uint8_t oscReadByte = 0;
    };

    static void writeControl_(Voice& v, uint8_t value) noexcept {
        const bool oldGate = (v.control & 0x01u) != 0u;
        const bool newGate = (value & 0x01u) != 0u;
        const bool oldTest = (v.control & 0x08u) != 0u;
        const bool newTest = (value & 0x08u) != 0u;
        v.control = value;
        if (newTest) {
            v.phase = 0;
            v.lfsr = 0x7FFFFFu;
            v.lastCombined = 0;
        } else if (oldTest) {
            v.phase = 0;
            if (v.lfsr == 0u) v.lfsr = 0x7FFFFFu;
        }
        if (newGate && !oldGate) v.env.gateOn();
        else if (!newGate && oldGate) v.env.gateOff();
    }

    static void clockNoise_(Voice& v, uint32_t previous, uint32_t next) noexcept {
        const bool previousBit = ((previous >> 19u) & 1u) != 0u;
        const bool nextBit = ((next >> 19u) & 1u) != 0u;
        if (previousBit || !nextBit) return;
        const uint32_t feedback = ((v.lfsr >> 22u) ^ (v.lfsr >> 17u)) & 1u;
        v.lfsr = ((v.lfsr << 1u) | feedback) & 0x7FFFFFu;
        if (v.lfsr == 0u) v.lfsr = 0x7FFFFFu;
    }

    static bool hardSyncShouldReset_(uint8_t destination,
                                     const std::array<bool, 3>& syncEnabled,
                                     const std::array<bool, 3>& sourceMsbRose) noexcept {
        if (destination >= 3u || !syncEnabled[destination]) return false;
        static constexpr std::array<uint8_t, 3> source = {2u, 0u, 1u};
        const uint8_t src = source[destination];
        if (!sourceMsbRose[src]) return false;
        // Match SIDChip::sidHardSyncShouldReset: a source oscillator that is
        // itself synchronized on the same edge does not propagate a cascading
        // reset through the three-oscillator sync ring.
        const uint8_t sourceOfSource = source[src];
        return !(syncEnabled[src] && sourceMsbRose[sourceOfSource]);
    }

    void clockOne_() noexcept {
        std::array<bool, 3> msbRose{};
        std::array<bool, 3> syncEnabled{};
        for (std::size_t i = 0; i < voices_.size(); ++i) {
            Voice& v = voices_[i];
            const uint32_t previous = v.phase;
            syncEnabled[i] = (v.control & 0x02u) != 0u;
            if (v.control & 0x08u) {
                v.phase = 0;
                v.lfsr = 0x7FFFFFu;
            } else {
                v.phase = (v.phase + v.freq) & 0xFFFFFFu;
                clockNoise_(v, previous, v.phase);
                msbRose[i] = (previous & 0x800000u) == 0u && (v.phase & 0x800000u) != 0u;
            }
        }
        // SID sync ring: voice 1<-3, voice 2<-1, voice 3<-2. Use the same
        // no-cascade law as the audio core so C64 OSC3 polling sees the phase
        // topology that actually rendered.
        for (std::size_t i = 0; i < voices_.size(); ++i) {
            if (hardSyncShouldReset_(static_cast<uint8_t>(i), syncEnabled, msbRose)) voices_[i].phase = 0;
        }
        for (auto& v : voices_) v.env.tick();
        updateOscillatorByte_(2u);

        if (++potCycle_ >= kPotConversionCycles) {
            potCycle_ = 0;
            potLatchedX_ = potTargetX_;
            potLatchedY_ = potTargetY_;
        }
    }

    static uint16_t noise12_(const Voice& v) noexcept {
        uint16_t out = 0;
        out |= uint16_t(((v.lfsr >> 22u) & 1u) << 11u);
        out |= uint16_t(((v.lfsr >> 20u) & 1u) << 10u);
        out |= uint16_t(((v.lfsr >> 16u) & 1u) << 9u);
        out |= uint16_t(((v.lfsr >> 13u) & 1u) << 8u);
        out |= uint16_t(((v.lfsr >> 11u) & 1u) << 7u);
        out |= uint16_t(((v.lfsr >> 7u) & 1u) << 6u);
        out |= uint16_t(((v.lfsr >> 4u) & 1u) << 5u);
        out |= uint16_t(((v.lfsr >> 2u) & 1u) << 4u);
        return out;
    }

    void updateOscillatorByte_(uint8_t index) noexcept {
        Voice& v = voices_[index];
        if (v.control & 0x08u) {
            // TEST: accumulator/LFSR held in reset — tri/saw/noise read 0, but
            // the pulse comparator is forced HIGH while TEST is set (reSID law,
            // mirrored from SIDVoice::renderFromPhase). Pulse-only reads $FF.
            v.oscReadByte = ((v.control & 0xF0u) == 0x40u) ? 0xFFu : 0u;
            return;
        }
        const uint8_t selected = uint8_t(v.control & 0xF0u);
        if (selected == 0u) {
            v.oscReadByte = 0u;
            return;
        }
        const bool tri = (selected & 0x10u) != 0u;
        const bool saw = (selected & 0x20u) != 0u;
        const bool pulse = (selected & 0x40u) != 0u;
        const bool noise = (selected & 0x80u) != 0u;
        const uint8_t source = index == 0u ? 2u : uint8_t(index - 1u);
        const bool ringMsb = (voices_[source].phase & 0x800000u) != 0u;
        const uint16_t saw12 = uint16_t((v.phase >> 12u) & 0x0FFFu);
        const uint16_t triBase = uint16_t((v.phase >> 11u) & 0x0FFFu);
        const bool triMsb = ((v.phase & 0x800000u) != 0u) ^ ((v.control & 0x04u) && ringMsb);
        const uint16_t tri12 = triMsb ? uint16_t(triBase ^ 0x0FFFu) : triBase;
        // v895 split-brain fix: this readback previously used a raw comparator
        // WITHOUT the 6581 comparator bias the audio engines apply for extreme
        // pulse widths (PW<=$020 → +1, PW>=$F00 → +2), so $D41B OSC3 polling of
        // a 6581 pulse disagreed with the rendered audio by 1-2 accumulator
        // steps per cycle. All engines now share sidPulseComparator12 (which
        // also preserves the v855 PW=$000/$FFF edge-case law).
        const uint16_t pulse12 =
            ArpSID::sidPulseComparator12(saw12, uint16_t(v.pulse & 0x0FFFu), is6581_);
        const uint16_t noiseValue = noise ? noise12_(v) : 0u;
        uint16_t output = 0;
        const uint8_t count = uint8_t(tri) + uint8_t(saw) + uint8_t(pulse) + uint8_t(noise);
        if (count == 1u) {
            output = tri ? tri12 : saw ? saw12 : pulse ? pulse12 : noiseValue;
        } else {
            output = ArpSID::sidAnalogCombined12_Ultra(
                tri12, saw12, pulse12, noiseValue,
                tri, saw, pulse, noise, is6581_, v.lastCombined,
                35.0f, 5.0f, is6581_ ? 3u : 5u, v.combinedSeed);
            v.lastCombined = output;
        }
        v.oscReadByte = uint8_t((output >> 4u) & 0xFFu);
    }

    std::array<Voice, 3> voices_{};
    bool is6581_ = true;
    uint8_t potTargetX_ = 0xFFu;
    uint8_t potTargetY_ = 0xFFu;
    uint8_t potLatchedX_ = 0xFFu;
    uint8_t potLatchedY_ = 0xFFu;
    uint32_t potCycle_ = 0;
    uint64_t clockCursor_ = 0;
    bool clockStarted_ = false;
};

} // namespace ArpSID::C64
