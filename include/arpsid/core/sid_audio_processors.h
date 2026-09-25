#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include "arpsid/core/math_utils.h"

namespace ArpSID {

// Canonical simple peak-limiter shared between AU and VST render paths.
// Envelope follower: fast attack (default instantaneous), smoothed release.
struct SimpleLimiter {
    float env      = 1.0f;   // current gain (1.0 = no limiting)
    float attCoeff = 0.0f;   // attack: 0 = instantaneous brick-wall
    float relCoeff = 0.9995f;

    void reset() { env = 1.0f; }

    void setAttackMs(float ms, double sr) {
        if (ms < 0.01f) { attCoeff = 0.0f; return; }
        const float sec = ms * 0.001f;
        attCoeff = std::exp(-1.0f / (sec * (float)std::max(1.0, sr)));
    }

    void setReleaseMs(float ms, double sr) {
        const float sec = std::max(1.0f, ms) * 0.001f;
        relCoeff = std::exp(-1.0f / (sec * (float)std::max(1.0, sr)));
    }

    inline float applyEnvelope(float targetGain) noexcept {
        const float tg = (std::isfinite(targetGain) && targetGain >= 0.0f) ? targetGain : 1.0f;
        if (tg < env)
            env = attCoeff * env + (1.0f - attCoeff) * tg;
        else
            env = 1.0f - (1.0f - env) * relCoeff;
        env = std::clamp(env, 0.0f, 1.0f);
        return env;
    }

    inline float processSample(float x, float threshold = 0.97f) noexcept {
        if (!std::isfinite(x)) return 0.0f;
        const float ax = std::fabs(x);
        const float thr = std::clamp(threshold, 0.01f, 1.0f);
        const float targetGain = (ax > thr) ? thr / ax : 1.0f;
        return x * applyEnvelope(targetGain);
    }

    inline void processStereoSample(float& l, float& r, float threshold = 0.97f) noexcept {
        if (!std::isfinite(l)) l = 0.0f;
        if (!std::isfinite(r)) r = 0.0f;
        const float peak = std::max(std::fabs(l), std::fabs(r));
        const float thr = std::clamp(threshold, 0.01f, 1.0f);
        const float targetGain = (peak > thr) ? thr / peak : 1.0f;
        const float g = applyEnvelope(targetGain);
        l *= g;
        r *= g;
    }
};

// Canonical Schroeder reverb (4 comb + 2 allpass per channel) shared between AU and VST.
struct SchroederReverb {
    struct Comb {
        std::vector<float> buf;
        int idx = 0;
        float fb = 0.75f, damp = 0.2f, z = 0.0f;
        void init(int n) { buf.assign((size_t)std::max(1, n), 0.0f); idx = 0; z = 0.0f; }
        inline float process(float x) noexcept {
            float y = buf[(size_t)idx];
            z += (y - z) * (1.0f - damp);
            // Flush denormals: after quiet passages, very small residuals accumulate
            // in the comb delay line and cause CPU stalls on x86 without FTZ/DAZ.
            // Threshold 1e-15 is well below -300 dB — inaudible, always safe to zero.
            float stored = x + z * fb;
            if (std::fabs(stored) < 1.0e-15f) stored = 0.0f;
            if (std::fabs(z) < 1.0e-15f) z = 0.0f;
            buf[(size_t)idx] = stored;
            if (++idx >= (int)buf.size()) idx = 0;
            return y;
        }
    };
    struct Allpass {
        std::vector<float> buf;
        int idx = 0;
        float fb = 0.5f;
        void init(int n) { buf.assign((size_t)std::max(1, n), 0.0f); idx = 0; }
        inline float process(float x) noexcept {
            float y = buf[(size_t)idx];
            float out = -x + y;
            float stored = x + y * fb;
            if (std::fabs(stored) < 1.0e-15f) stored = 0.0f;
            buf[(size_t)idx] = stored;
            if (++idx >= (int)buf.size()) idx = 0;
            return out;
        }
    };

    std::array<Comb, 4> combL, combR;
    std::array<Allpass, 2> apL, apR;

    void init(double sr) {
        const float scale = (float)(sr / 44100.0);
        auto s = [&](int n) { return std::max(1, (int)std::lround(n * scale)); };
        const int cl[4] = { 1116, 1188, 1277, 1356 };
        const int cr[4] = { 1139, 1211, 1300, 1379 };
        for (int i = 0; i < 4; ++i) {
            combL[(size_t)i].init(s(cl[i])); combL[(size_t)i].fb = 0.78f; combL[(size_t)i].damp = 0.25f;
            combR[(size_t)i].init(s(cr[i])); combR[(size_t)i].fb = 0.78f; combR[(size_t)i].damp = 0.25f;
        }
        apL[0].init(s(556)); apL[1].init(s(441));
        apR[0].init(s(579)); apR[1].init(s(464));
    }

    void reset() {
        for (auto& c : combL) { std::fill(c.buf.begin(), c.buf.end(), 0.0f); c.idx = 0; c.z = 0.0f; }
        for (auto& c : combR) { std::fill(c.buf.begin(), c.buf.end(), 0.0f); c.idx = 0; c.z = 0.0f; }
        for (auto& a : apL)   { std::fill(a.buf.begin(), a.buf.end(), 0.0f); a.idx = 0; }
        for (auto& a : apR)   { std::fill(a.buf.begin(), a.buf.end(), 0.0f); a.idx = 0; }
    }

    inline void process(float inL, float inR, float& outL, float& outR) noexcept {
        float yL = 0.0f, yR = 0.0f;
        for (int i = 0; i < 4; ++i) {
            yL += combL[(size_t)i].process(inL);
            yR += combR[(size_t)i].process(inR);
        }
        yL *= 0.25f; yR *= 0.25f;
        for (int i = 0; i < 2; ++i) {
            yL = apL[(size_t)i].process(yL);
            yR = apR[(size_t)i].process(yR);
        }
        // Sanitize output: NaN or Inf from a corrupted buffer should not propagate
        // into the host audio stream. Clamp rather than zero so tail ringdown is preserved.
        outL = std::isfinite(yL) ? std::clamp(yL, -2.0f, 2.0f) : 0.0f;
        outR = std::isfinite(yR) ? std::clamp(yR, -2.0f, 2.0f) : 0.0f;
    }
};

} // namespace ArpSID
