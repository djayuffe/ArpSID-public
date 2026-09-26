// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once

#include "arpsid/engines/drsid_engine.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace ArpSID::Tests {

inline void dr808Require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::abort();
    }
}

struct DrumEvent {
    int sample = 0;
    int midiNote = 36;
    float velocity = 1.0f;
};

struct RenderStats {
    float peak = 0.0f;
    float energy = 0.0f;
    float tailRms = 0.0f;
    bool finite = true;
};

struct AnalogDr808EngineForTests : public DrSidEngine {
    AnalogDr808EngineForTests() {
        setSampleRate(48000.0);
        setClockFrequency(PAL_CLOCK_FREQ);
        setMasterVolume(0.92f);
        setDrumMachineModelNormalized(1.0f);
        setAccentAmount(0.82f);
        setOutputDrive(0.34f);
        setHatMetal(0.78f);
        setClapSpread(0.66f);
    }
};

inline AnalogDr808EngineForTests makeAnalogDr808Engine() {
    return AnalogDr808EngineForTests{};
}

inline std::vector<float> renderMonoScript(DrSidEngine& e,
                                           const std::vector<DrumEvent>& script,
                                           int totalSamples) {
    std::vector<float> mono(static_cast<size_t>(std::max(totalSamples, 0)), 0.0f);
    size_t eventIndex = 0;
    for (int i = 0; i < totalSamples; ++i) {
        while (eventIndex < script.size() && script[eventIndex].sample == i) {
            e.triggerMidiNote(script[eventIndex].midiNote, script[eventIndex].velocity);
            ++eventIndex;
        }
        float left = 0.0f;
        float right = 0.0f;
        float* outputs[2] = {&left, &right};
        e.processBlock(outputs, 1);
        mono[static_cast<size_t>(i)] = 0.5f * (left + right);
    }
    return mono;
}

inline RenderStats analyzeMono(const std::vector<float>& mono, int tailWindow = 4096) {
    RenderStats stats{};
    if (mono.empty()) return stats;
    const int total = static_cast<int>(mono.size());
    const int tailStart = std::max(0, total - std::max(1, tailWindow));
    double tailSum = 0.0;
    for (int i = 0; i < total; ++i) {
        const float s = mono[static_cast<size_t>(i)];
        if (!std::isfinite(s)) stats.finite = false;
        const float a = std::fabs(std::isfinite(s) ? s : 0.0f);
        stats.peak = std::max(stats.peak, a);
        stats.energy += a;
        if (i >= tailStart) tailSum += static_cast<double>(s) * static_cast<double>(s);
    }
    stats.tailRms = static_cast<float>(std::sqrt(tailSum / static_cast<double>(std::max(1, total - tailStart))));
    return stats;
}

} // namespace ArpSID::Tests
