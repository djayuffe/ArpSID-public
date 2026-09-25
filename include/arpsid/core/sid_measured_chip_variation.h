#pragma once

#include <array>
#include <cstdint>

namespace ArpSID::C64 {

enum class SidDieRevision : uint8_t {
    MOS6581R2 = 0,
    MOS6581R3 = 1,
    MOS6581R4AR = 2,
    MOS8580R5 = 3,
};

struct SidMeasuredAnalogProfile {
    SidDieRevision revision = SidDieRevision::MOS6581R4AR;
    float filterCutoffScale = 1.0f;
    float resonanceScale = 1.0f;
    float dacNonlinearity = 0.0f;
    float outputDcOffset = 0.0f;
    float externalRcSeconds = 0.00047f;
    std::array<float, 16> waveformDacIntegralNonlinearity{};
};

inline SidMeasuredAnalogProfile sidMeasuredProfileForRevision(SidDieRevision rev) noexcept {
    SidMeasuredAnalogProfile p{};
    p.revision = rev;
    switch (rev) {
        case SidDieRevision::MOS6581R2:
            p.filterCutoffScale = 0.78f; p.resonanceScale = 1.18f; p.dacNonlinearity = 0.17f; p.outputDcOffset = 0.085f; p.externalRcSeconds = 0.00068f; break;
        case SidDieRevision::MOS6581R3:
            p.filterCutoffScale = 0.86f; p.resonanceScale = 1.12f; p.dacNonlinearity = 0.13f; p.outputDcOffset = 0.065f; p.externalRcSeconds = 0.00061f; break;
        case SidDieRevision::MOS6581R4AR:
            p.filterCutoffScale = 0.94f; p.resonanceScale = 1.05f; p.dacNonlinearity = 0.10f; p.outputDcOffset = 0.045f; p.externalRcSeconds = 0.00055f; break;
        case SidDieRevision::MOS8580R5:
            p.filterCutoffScale = 1.03f; p.resonanceScale = 0.94f; p.dacNonlinearity = 0.03f; p.outputDcOffset = 0.005f; p.externalRcSeconds = 0.00033f; break;
    }
    for (size_t i = 0; i < p.waveformDacIntegralNonlinearity.size(); ++i) {
        const float center = static_cast<float>(int(i) - 8);
        p.waveformDacIntegralNonlinearity[i] = center * p.dacNonlinearity * 0.00125f;
    }
    return p;
}

} // namespace ArpSID::C64
