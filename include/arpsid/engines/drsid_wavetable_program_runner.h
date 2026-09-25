// SPDX-License-Identifier: BSD-3-Clause
// drsid_wavetable_program_runner.h — Renders DrSidInstrumentProgram
// register microprograms onto a live SIDChip (Audit #43, #44 wire-up).
//
// PROBLEM (audit §3, #43, #44)
// ---------------------------// `DrSidInstrumentProgram` (skive 4 contract) defines the audit-correct
// C64-wavetable drum format: a small ordered sequence of timed register
// writes per drum hit. The audit's #43 and #44 said the legacy
// DrSidEngine renders drums as an "analog oscillator/noise overlay",
// not as register microprograms — and the clap specifically uses a
// "simple half-interval gate" instead of true multi-burst behavior.
//
// What's been done so far:
// * Skive 4 — `DrSidInstrumentProgram` POD format + constexpr
// `programIsWellFormed()` validator + exemplar Kick/ClosedHat/Clap.
// * Skive 4 follow-up — `DrSidKitCompiler` + CRC32 fingerprint generator.
//
// This header provides the runner that takes a program and emits its register
// writes onto a SIDChip at the right cycle offsets so the chip's audio reflects
// the program.
//
// CONTRACT
// -------// * `DrSidWavetableProgramRunner` is stateful per-voice. One runner
// per active SID voice. Owns a const pointer to the active program
// and an internal cursor.
// * `trigger(program, velocity)` arms the runner with a new program.
// * `tickSidCycle(chip, voiceIndex)` advances by ONE SID cycle and
// emits any register writes whose cycleOffset falls within the
// consumed cycle window.
// * The runner DOES NOT touch the chip's voice level — that's the
// caller's responsibility (host velocity-scaling stage).
// * RT-safe: no allocation, no locks, no dynamic dispatch.

#ifndef ARPSID_ENGINES_DRSID_WAVETABLE_PROGRAM_RUNNER_H
#define ARPSID_ENGINES_DRSID_WAVETABLE_PROGRAM_RUNNER_H

#include "arpsid/core/drsid_instrument_program.h"
#include "arpsid/core/sid_chip.h"

#include <algorithm>
#include <cstdint>

namespace ArpSID {

class DrSidWavetableProgramRunner {
public:
    // ── Trigger a new program on this runner ────────────────────────────────
    // Resets the cursor, captures the program, and applies STEP 0's
    // register frame to the chip's voice immediately (since cycleOffset=0
    // is implicit — the first step is "at trigger time").
    void trigger(const Drsid::DrSidInstrumentProgram* program,
                 SIDChip& chip,
                 std::uint8_t voiceIndex,
                 std::uint8_t velocity = 100) noexcept {
        program_     = program;
        cursor_      = 0;
        cycleCounter_ = 0;
        velocity_    = velocity;
        if (!program_ || program_->empty()) return;
        applyStep_(chip, voiceIndex, program_->steps[0]);
        cursor_ = 1;  // step 0 consumed; the next one waits for its cycleOffset
    }

    // ── Advance by N SID cycles and emit any due register writes ────────────
    // Called once per SID cycle from the chip's interval renderer.
    // Returns the number of program steps that fired during this advance.
    int tickSidCycles(SIDChip& chip,
                      std::uint8_t voiceIndex,
                      std::uint32_t cyclesElapsed) noexcept {
        if (!program_ || program_->empty()) return 0;
        cycleCounter_ += cyclesElapsed;
        int fired = 0;
        while (cursor_ < program_->stepCount &&
               cycleCounter_ >= program_->steps[cursor_].cycleOffset) {
            applyStep_(chip, voiceIndex, program_->steps[cursor_]);
            ++cursor_;
            ++fired;
        }
        return fired;
    }

    // Returns true once the runner has emitted the terminal step.
    bool finished() const noexcept {
        return !program_ || cursor_ >= (program_ ? program_->stepCount : 0);
    }

    // Hard stop — clears cursor without applying terminal step. Used by
    // choke groups (closed-hat chokes open-hat).
    void abort() noexcept {
        program_     = nullptr;
        cursor_      = 0;
        cycleCounter_ = 0;
    }

    // Diagnostic / test introspection.
    const Drsid::DrSidInstrumentProgram* program() const noexcept { return program_; }
    std::uint8_t      cursor()         const noexcept { return cursor_; }
    std::uint32_t     cycleCounter()   const noexcept { return cycleCounter_; }
    std::uint8_t      velocity()       const noexcept { return velocity_; }
    std::uint64_t     stepsEmittedSinceLastReset() const noexcept { return stepsEmittedTotal_; }
    void resetDiagnostics() noexcept { stepsEmittedTotal_ = 0; }

private:
    static std::uint8_t controlWaveformToInternal_(std::uint8_t control) noexcept {
        std::uint8_t wf = 0u;
        if (control & Drsid::WaveformBits::kTriangle) wf |= static_cast<std::uint8_t>(Waveform::Triangle);
        if (control & Drsid::WaveformBits::kSawtooth) wf |= static_cast<std::uint8_t>(Waveform::Sawtooth);
        if (control & Drsid::WaveformBits::kPulse)    wf |= static_cast<std::uint8_t>(Waveform::Pulse);
        if (control & Drsid::WaveformBits::kNoise)    wf |= static_cast<std::uint8_t>(Waveform::Noise);
        return static_cast<std::uint8_t>(wf & 0x0Fu);
    }

    void applyStep_(SIDChip& chip,
                    std::uint8_t voiceIndex,
                    const Drsid::DrSidRegisterStep& s) noexcept {
        if (voiceIndex >= 3) return;
        SIDVoice& v = chip.getVoice(voiceIndex);
        v.setFrequency(s.freq);
        v.setPulseWidth(s.pulseWidth);
        // Audit §3: waveform byte carries the SID $D404 control register
        // bits (gate + sync + ring + waveform). We pass it through
        // setWaveform which masks to the waveform/gate bits — the SIDVoice
        // API splits gate/test handling internally.
        const bool wantGate = (s.waveform & Drsid::WaveformBits::kGate) != 0;
        const bool wantTest = (s.waveform & Drsid::WaveformBits::kTest) != 0;
        v.setTestBit(wantTest);
        v.setWaveform(controlWaveformToInternal_(s.waveform));
        chip.setVoiceSyncEnable(voiceIndex, (s.waveform & Drsid::WaveformBits::kSync) != 0);
        chip.setVoiceRingModEnable(voiceIndex, (s.waveform & Drsid::WaveformBits::kRingMod) != 0);
        v.setGate(wantGate);
        v.setAttack((s.attackDecay >> 4) & 0x0Fu);
        v.setDecay (s.attackDecay & 0x0Fu);
        v.setSustain((s.sustainRelease >> 4) & 0x0Fu);
        v.setRelease(s.sustainRelease & 0x0Fu);
        // Audit-correct: kHardRestartTransient on step 0 triggers the C64
        // "hard restart" punch via setTestBit briefly. We model it
        // declaratively here — host engines can refine.
        if ((s.flags & Drsid::StepFlag::kFilterRouteChange) != 0) {
            chip.setFilterCutoff(static_cast<std::uint16_t>(((s.filterCutoffHi & 0xFFu) << 3u) | (s.filterCutoffLo & 0x07u)));
            chip.setFilterResonance(static_cast<std::uint8_t>((s.filterResRoute >> 4u) & 0x0Fu));
            chip.setFilterVoiceRouting((s.filterResRoute & 0x01u) != 0,
                                       (s.filterResRoute & 0x02u) != 0,
                                       (s.filterResRoute & 0x04u) != 0);
            chip.setFilterMode(static_cast<FilterMode>((s.modeVolume >> 4u) & 0x07u));
            chip.setMasterVolume(static_cast<std::uint8_t>(s.modeVolume & 0x0Fu));
        }
        if ((s.flags & Drsid::StepFlag::kHardRestartTransient) != 0) {
            v.setTestBit(true);
            v.setTestBit(false);
        }
        ++stepsEmittedTotal_;
    }

    const Drsid::DrSidInstrumentProgram* program_ = nullptr;
    std::uint8_t  cursor_      = 0;       ///< index of NEXT step to fire
    std::uint32_t cycleCounter_ = 0;      ///< SID cycles since trigger
    std::uint8_t  velocity_    = 100;
    std::uint64_t stepsEmittedTotal_ = 0; ///< diagnostic counter
};

} // namespace ArpSID

#endif // ARPSID_ENGINES_DRSID_WAVETABLE_PROGRAM_RUNNER_H
