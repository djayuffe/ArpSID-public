#pragma once

#include "arpsid/core/c64_bus.h"
#include "arpsid/core/c64_vic.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ArpSID::C64 {

struct C64TimingMath final {
    static constexpr uint32_t kPalVideoHz = 50u;
    static constexpr uint32_t kNtscVideoHz = 60u;

    static constexpr uint32_t roundedDiv(uint32_t numerator, uint32_t denominator) noexcept {
        return denominator == 0u ? 0u : static_cast<uint32_t>((uint64_t{numerator} + denominator / 2u) / denominator);
    }

    static constexpr uint32_t palPhi2Hz() noexcept { return kPalPhi2Hz; }
    static constexpr uint32_t ntscPhi2Hz() noexcept { return kNtscPhi2Hz; }
    static constexpr uint32_t phi2Hz(bool pal) noexcept { return pal ? kPalPhi2Hz : kNtscPhi2Hz; }

    static constexpr uint32_t palPsidCiaTimerALatch() noexcept { return roundedDiv(kPalPhi2Hz, kPalVideoHz); }
    static constexpr uint32_t ntscPsidCiaTimerALatch() noexcept { return roundedDiv(kNtscPhi2Hz, kNtscVideoHz); }
    static constexpr uint32_t psidCiaTimerALatch(bool pal) noexcept {
        return pal ? palPsidCiaTimerALatch() : ntscPsidCiaTimerALatch();
    }

    static constexpr uint64_t palVicFrameCycles() noexcept {
        return uint64_t{VicII::kPalCyclesPerLine} * uint64_t{VicII::kPalRasterLines};
    }

    static constexpr uint64_t ntscVicFrameCycles() noexcept {
        return uint64_t{VicII::kNtscCyclesPerLine} * uint64_t{VicII::kNtscRasterLines};
    }

    // v855 P1.1 cadence policy — EXPLICIT and deliberate:
    // PSID VBI playback uses the PHYSICAL VIC frame length (PAL 63*312 = 19656
    // cycles ≈ 50.1245 Hz; NTSC 65*263 = 17095 ≈ 59.83 Hz), i.e. the cadence a
    // real C64's raster interrupt delivers. This is NOT the "compatibility"
    // 50.000/60.000 Hz latch cadence (PAL round(985248/50) = 19705 cycles) that
    // the PSID CIA path uses via psidCiaTimerALatch(). The two differ by ~0.25%
    // on PAL; they are close but NOT the same number and must never be mixed:
    // VBI-speed tunes get real VIC frames, CIA-speed tunes get the 50/60 Hz CIA
    // latch. If a strict-50.000 Hz VBI compatibility mode is ever wanted, add it
    // as a separate explicit mode rather than changing this constant.
    static constexpr uint64_t psidVbiFrameCycles(bool pal) noexcept {
        return pal ? palVicFrameCycles() : ntscVicFrameCycles();
    }

    static double psidPlayPeriodSamplesFromCycles(uint64_t phi2Cycles, double hostSampleRate, uint32_t phi2Hz) noexcept {
        const double sr = std::isfinite(hostSampleRate) ? std::max(1.0, hostSampleRate) : 44100.0;
        const double hz = static_cast<double>(std::max<uint32_t>(phi2Hz, 1u));
        return std::max(1.0, (static_cast<double>(phi2Cycles) * sr) / hz);
    }

    static double psidCiaPlayPeriodSamples(bool pal, double hostSampleRate) noexcept {
        return psidPlayPeriodSamplesFromCycles(psidCiaTimerALatch(pal), hostSampleRate, phi2Hz(pal));
    }

    static double psidVbiPlayPeriodSamples(bool pal, double hostSampleRate) noexcept {
        return psidPlayPeriodSamplesFromCycles(psidVbiFrameCycles(pal), hostSampleRate, phi2Hz(pal));
    }
};

static_assert(C64TimingMath::palPsidCiaTimerALatch() == 19705u, "PAL PSID CIA Timer A latch must be round(985248/50)");
static_assert(C64TimingMath::ntscPsidCiaTimerALatch() == 17045u, "NTSC PSID CIA Timer A latch must be round(1022727/60)");
static_assert(C64TimingMath::palVicFrameCycles() == 19656ull, "PAL VIC frame must be 63*312 PHI2 cycles");
static_assert(C64TimingMath::ntscVicFrameCycles() == 17095ull, "NTSC VIC frame must be 65*263 PHI2 cycles");

} // namespace ArpSID::C64
