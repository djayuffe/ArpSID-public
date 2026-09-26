// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once

#include <algorithm>
#include <cstddef>

namespace ArpSID {

enum class DrumStemMixPolicy : unsigned char {
    ReplaceWithSid808 = 0,
    AdditiveDrumMachine = 1,
    ExplicitSelectedStem = 2,
};

struct DrumStemFrame {
    float mainL = 0.0f;
    float mainR = 0.0f;
    float drsidL = 0.0f;
    float drsidR = 0.0f;
    float sid808L = 0.0f;
    float sid808R = 0.0f;
    float digiL = 0.0f;
    float digiR = 0.0f;
    bool useDrsid = false;
    bool useSid808 = false;
    bool useDigi = false;
};

inline float arpsidClampAudio_(float x) noexcept {
    return std::clamp(x, -1.0f, 1.0f);
}

inline void mixDrumStemFrame(const DrumStemFrame& in,
                             DrumStemMixPolicy policy,
                             float& outL,
                             float& outR) noexcept {
    switch (policy) {
        case DrumStemMixPolicy::ReplaceWithSid808:
            outL = arpsidClampAudio_(in.useSid808 ? in.sid808L : 0.0f);
            outR = arpsidClampAudio_(in.useSid808 ? in.sid808R : 0.0f);
            return;
        case DrumStemMixPolicy::ExplicitSelectedStem: {
            float l = in.mainL;
            float r = in.mainR;
            if (in.useDrsid) { l += in.drsidL; r += in.drsidR; }
            if (in.useSid808) { l += in.sid808L; r += in.sid808R; }
            if (in.useDigi) { l += in.digiL; r += in.digiR; }
            outL = arpsidClampAudio_(l);
            outR = arpsidClampAudio_(r);
            return;
        }
        case DrumStemMixPolicy::AdditiveDrumMachine:
        default: {
            const float l = in.mainL + in.drsidL + in.sid808L + in.digiL;
            const float r = in.mainR + in.drsidR + in.sid808R + in.digiR;
            outL = arpsidClampAudio_(l);
            outR = arpsidClampAudio_(r);
            return;
        }
    }
}

inline void mixDrumStemBuffers(const float* mainL, const float* mainR,
                               const float* drsidL, const float* drsidR,
                               const float* sid808L, const float* sid808R,
                               const float* digiL, const float* digiR,
                               float* outL, float* outR,
                               int n,
                               DrumStemMixPolicy policy,
                               bool useDrsid,
                               bool useSid808,
                               bool useDigi) noexcept {
    if (!outL || !outR || n <= 0) return;
    for (int i = 0; i < n; ++i) {
        DrumStemFrame f{};
        f.mainL = mainL ? mainL[i] : outL[i];
        f.mainR = mainR ? mainR[i] : outR[i];
        f.drsidL = drsidL ? drsidL[i] : 0.0f;
        f.drsidR = drsidR ? drsidR[i] : 0.0f;
        f.sid808L = sid808L ? sid808L[i] : 0.0f;
        f.sid808R = sid808R ? sid808R[i] : 0.0f;
        f.digiL = digiL ? digiL[i] : 0.0f;
        f.digiR = digiR ? digiR[i] : 0.0f;
        f.useDrsid = useDrsid;
        f.useSid808 = useSid808;
        f.useDigi = useDigi;
        mixDrumStemFrame(f, policy, outL[i], outR[i]);
    }
}

} // namespace ArpSID
