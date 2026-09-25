#pragma once

#include "../core/math_utils.h"
#include <cmath>
#include <algorithm>
#include <array>
#include <cstdint>
// FIX Bug#1: Removed header-local ArpSID_xorshift32 / ArpSID_rand_bipolar definitions.
// Canonical implementations now live in math_utils.h (ArpSID namespace).
// Removed #include <functional> — std::function replaced with raw callback pointer (Bug#5).


namespace ArpSID {


/**
 * LFO (Low Frequency Oscillator)
 * Multiple waveform shapes, tempo sync, retrigger
 */
class LFO {
public:
    enum class Shape {
        Sine,
        Triangle,
        Sawtooth,
        ReverseSaw,
        Square,
        SampleAndHold,
        Random
    };

    LFO() {
        rngState = 0x12345678u;
        reset();
    }

    // Set per-instance seed so multiple LFO instances diverge.
    void setInstanceSeed(uint32_t discriminant) {
        instanceSeed_ = ArpSID_mixSeed(0x12345678u, discriminant);
        rngState = instanceSeed_;
    }

    void reset() {
        phase = std::isfinite(phaseOffset) ? phaseOffset : 0.0;
        currentValue = 0.0f;
        // Pre-generate both random targets so S&H doesn't start at 0.
        rngState = (instanceSeed_ != 0u) ? instanceSeed_ : 0x12345678u;
        lastRandomValue = ArpSID::ArpSID_rand_bipolar(rngState);
        nextRandomValue = ArpSID::ArpSID_rand_bipolar(rngState);
        sampleCounter = 0;
    }

    void setSampleRate(double sr) {
        sampleRate = (std::isfinite(sr) && sr >= 1.0) ? sr : 44100.0;
    }

    // Process one sample and return LFO value (-1.0 to 1.0)
    float process() {
        // Update phase
        const double sr = (std::isfinite(sampleRate) && sampleRate >= 1.0) ? sampleRate : 44100.0;
        const double safeRate = std::isfinite(rateHz) ? std::clamp<double>(rateHz, 0.01, 50.0) : 1.0;
        if (!std::isfinite(phase)) phase = std::isfinite(phaseOffset) ? phaseOffset : 0.0;
        const double phaseIncrement = safeRate / sr;
        phase += phaseIncrement;

        // Wrap phase
        if (phase >= 1.0) {
            phase -= std::floor(phase);

            // On phase wrap: S&H captures the next target immediately;
            // interpolated Random advances to a new segment.
            if (shape == Shape::SampleAndHold) {
                // Snap to nextRandomValue then draw a fresh next one
                currentValue = nextRandomValue;
                lastRandomValue = nextRandomValue;
                nextRandomValue = ArpSID::ArpSID_rand_bipolar(rngState);
            } else if (shape == Shape::Random) {
                lastRandomValue = nextRandomValue;
                nextRandomValue = ArpSID::ArpSID_rand_bipolar(rngState);
            }

            // Trigger retrigger callback
            if (retriggerEnabled && onRetriggerFn_) {
                onRetriggerFn_(onRetriggerCtx_);
            }
        }

        // Generate waveform
        currentValue = generateWaveform();

        // Apply depth
        currentValue *= depth;
        currentValue = ArpSID_sanitizeFloat(currentValue);

        sampleCounter++;
        return currentValue;
    }

    // Retrigger (reset phase)
    void retrigger() {
        if (retriggerEnabled) {
            phase = phaseOffset;

            // For S&H/Random, restart interpolation segment
            if (shape == Shape::SampleAndHold || shape == Shape::Random) {
                lastRandomValue = ArpSID::ArpSID_rand_bipolar(rngState);
                nextRandomValue = ArpSID::ArpSID_rand_bipolar(rngState);
            }
        }
    }

    // Parameters
    void setRate(float hz) {
        rateHz = std::clamp(std::isfinite(hz) ? hz : 1.0f, 0.01f, 50.0f);
    }

    void setRateTempo(float bpm, float division) {
        // Sync to host tempo.
        // division: 1.0=quarter note, 2.0=half note, 0.5=eighth note, 0.25=sixteenth note.
        // A quarter note at 120bpm = 0.5s → rateHz = 1/0.5 = 2 Hz.
        // Formula: rateHz = (bpm / 60) / division [cycles per second]
        const float safeBpm = (std::isfinite(bpm) && bpm > 0.0f) ? bpm : 120.0f;
        const float safeDivision = (std::isfinite(division) && std::abs(division) >= 0.001f)
            ? std::abs(division) : 1.0f;
        rateHz = std::clamp((safeBpm / 60.0f) / safeDivision, 0.01f, 50.0f);
    }

    void setDepth(float d) {
        depth = std::clamp(std::isfinite(d) ? d : 0.0f, 0.0f, 1.0f);
    }

    void setShape(Shape s) {
        shape = s;
    }

    void setShapeFromValue(float value) {
        // FIX Bug#17: Replaced magic 6.99f with std::lround + clamp (same fix as arpeggiator).
        const float safeValue = std::isfinite(value) ? value : 0.0f;
        const int shapeIndex = std::clamp(static_cast<int>(std::lround(safeValue * 6.0f)), 0, 6);
        shape = static_cast<Shape>(shapeIndex);
    }

    void setPhaseOffset(float offset) {
        phaseOffset = std::clamp(std::isfinite(offset) ? offset : 0.0f, 0.0f, 1.0f);
    }

    void setRetrigger(bool enable) {
        retriggerEnabled = enable;
    }

    void setSyncToTempo(bool sync) {
        tempoSync = sync;
    }
    bool isSyncToTempo() const { return tempoSync; }

    // FIX Bug#5: Replaced std::function<void()> with a plain function-pointer + void*
    // context pair. std::function can heap-allocate its target closure when the
    // callable does not fit into the small-buffer optimisation (~24 bytes). The LFO
    // runs on the audio thread; any heap allocation there is a hard-latency violation.
    // A single void(*)(void*) / void* pair has zero heap overhead and is trivially
    // copyable. Usage:
    // lfo.setRetriggerCallback([](void* ctx){ ... }, myContextPtr);
    void setRetriggerCallback(void (*cb)(void*), void* ctx = nullptr) {
        onRetriggerFn_  = cb;
        onRetriggerCtx_ = ctx;
    }

    // Get current values
    float getValue() const { return currentValue; }
    double getPhase() const { return phase; }

private:
    double sampleRate = 44100.0;
    double phase = 0.0;
    double phaseOffset = 0.0;

    float rateHz = 1.0f;
    float depth = 1.0f;
    float currentValue = 0.0f;

    Shape shape = Shape::Sine;
    bool retriggerEnabled = true;
    bool tempoSync = false;

    // Random generation (realtime-safe)
    uint32_t rngState = 0x12345678u;
    uint32_t instanceSeed_ = 0u;
    float lastRandomValue = 0.0f;
    float nextRandomValue = 0.0f;
    uint64_t sampleCounter = 0;

    // FIX Bug#5: raw function-pointer callback (zero allocation — replaces std::function).
    void (*onRetriggerFn_)(void*) = nullptr;
    void*  onRetriggerCtx_        = nullptr;

    static float lfoSineFast(double ph) noexcept {
        static constexpr int kSize = 512;
        static const std::array<float, kSize + 1> table = []() {
            std::array<float, kSize + 1> t{};
            for (int i = 0; i <= kSize; ++i)
                t[(size_t)i] = static_cast<float>(std::sin((static_cast<double>(i) / kSize) * 2.0 * ArpSID_pi()));
            return t;
        }();
        ph -= std::floor(ph);
        const double x = ph * kSize;
        const int i = std::clamp(static_cast<int>(x), 0, kSize - 1);
        const float f = static_cast<float>(x - static_cast<double>(i));
        return table[(size_t)i] + (table[(size_t)i + 1u] - table[(size_t)i]) * f;
    }

    float generateWaveform() {
        float output = 0.0f;

        switch (shape) {
            case Shape::Sine:
                output = lfoSineFast(phase);
                break;

            case Shape::Triangle:
                if (phase < 0.5) output = static_cast<float>(4.0 * phase - 1.0);
                else             output = static_cast<float>(3.0 - 4.0 * phase);
                break;

            case Shape::Sawtooth:
                output = static_cast<float>(2.0 * phase - 1.0);
                break;

            case Shape::ReverseSaw:
                output = static_cast<float>(1.0 - 2.0 * phase);
                break;

            case Shape::Square:
                output = (phase < 0.5) ? 1.0f : -1.0f;
                break;

            case Shape::SampleAndHold:
                // Update on phase wrap (handled in process())
                output = lastRandomValue;
                break;

            case Shape::Random: {
                // Smooth random: interpolate between lastRandomValue and nextRandomValue.
                const float tt = static_cast<float>(phase);
                output = lastRandomValue + (nextRandomValue - lastRandomValue) * tt;
                break;
            }
        }

        return output;
    }
};

/**
 * LFO Bank - Multiple LFOs for different modulation targets
 */
class LFOBank {
public:
    static constexpr int NUM_LFOS = 4;

    LFOBank() {
        for (auto& lfo : lfos) {
            lfo.setSampleRate(44100.0);
        }
    }

    void setSampleRate(double sr) {
        for (auto& lfo : lfos) lfo.setSampleRate(sr);
    }

    void setInstanceSeed(uint32_t discriminant) {
        for (int i = 0; i < NUM_LFOS; ++i) {
            lfos[(size_t)i].setInstanceSeed(ArpSID_mixSeed(discriminant, static_cast<uint32_t>(i)));
        }
    }

    void reset() {
        for (auto& lfo : lfos) lfo.reset();
    }

    // Alias used by panic handler
    void resetAll() { reset(); }

    // Process one sample for all LFOs (updates their internal currentValue).
    // The processor advances LFOs sample-by-sample for sample-accurate mod.
    inline void process() {
        for (auto& lfo : lfos) {
            (void)lfo.process();
        }
    }

    LFO& getLFO(int index) {
        index = std::clamp(index, 0, NUM_LFOS - 1);
        return lfos[(size_t)index];
    }

    const LFO& getLFO(int index) const {
        index = std::clamp(index, 0, NUM_LFOS - 1);
        return lfos[(size_t)index];
    }

private:
    std::array<LFO, NUM_LFOS> lfos{};
};

} // namespace ArpSID
