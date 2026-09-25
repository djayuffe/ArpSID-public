#pragma once

#include "sid_chip.h"
#include "sid_static_params.h"
#include "parameter_ids.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace ArpSID {

static constexpr float kSidForensicTemperatureMin = 20.0f;
static constexpr float kSidForensicTemperatureMax = 60.0f;
static constexpr float kSidForensicSupplyMin = 4.5f;
static constexpr float kSidForensicSupplyMax = 5.5f;

inline float sidForensicTemperatureFromNormalized(float norm) noexcept {
    const float clamped = std::clamp(std::isfinite(norm) ? norm : 0.375f, 0.0f, 1.0f);
    return kSidForensicTemperatureMin + (kSidForensicTemperatureMax - kSidForensicTemperatureMin) * clamped;
}

inline float sidForensicTemperatureToNormalized(float celsius) noexcept {
    const float clamped = std::clamp(std::isfinite(celsius) ? celsius : 35.0f,
                                     kSidForensicTemperatureMin, kSidForensicTemperatureMax);
    return (clamped - kSidForensicTemperatureMin) / (kSidForensicTemperatureMax - kSidForensicTemperatureMin);
}

inline float sidForensicSupplyFromNormalized(float norm) noexcept {
    const float clamped = std::clamp(std::isfinite(norm) ? norm : 0.5f, 0.0f, 1.0f);
    return kSidForensicSupplyMin + (kSidForensicSupplyMax - kSidForensicSupplyMin) * clamped;
}

inline float sidForensicSupplyToNormalized(float voltage) noexcept {
    const float clamped = std::clamp(std::isfinite(voltage) ? voltage : 5.0f,
                                     kSidForensicSupplyMin, kSidForensicSupplyMax);
    return (clamped - kSidForensicSupplyMin) / (kSidForensicSupplyMax - kSidForensicSupplyMin);
}

inline uint8_t sidForensicRevisionFromNormalized(float norm) noexcept {
    const float clamped = std::clamp(std::isfinite(norm) ? norm : (1.0f / 3.0f), 0.0f, 1.0f);
    return static_cast<uint8_t>(2 + std::clamp<int>(static_cast<int>(std::lround(clamped * 3.0f)), 0, 3));
}

inline float sidForensicRevisionToNormalized(uint8_t revision) noexcept {
    const int clamped = std::clamp<int>(static_cast<int>(revision), 2, 5);
    return static_cast<float>(clamped - 2) * (1.0f / 3.0f);
}

inline uint32_t sidForensicChipSeedFromNormalized(float norm) noexcept {
    const double clamped = std::clamp(static_cast<double>(std::isfinite(norm) ? norm : 0.0f), 0.0, 1.0);
    return static_cast<uint32_t>(std::llround(clamped * 4294967295.0));
}

inline float sidForensicChipSeedToNormalized(uint32_t seed) noexcept {
    return static_cast<float>(static_cast<double>(seed) / 4294967295.0);
}

inline uint8_t sidForensicRevisionForVariant(uint8_t revision, SidFamily family) noexcept {
    return sidCombinedRevisionForModel(revision, family == SidFamily::MOS6581);
}

inline const char* sidForensicRevisionLabel(uint8_t revision) noexcept {
    switch (std::clamp<int>(static_cast<int>(revision), 2, 5)) {
        case 2: return "6581 R2";
        case 3: return "6581 R3";
        case 4: return "6581 R4AR";
        default: return "8580 R5";
    }
}


template <typename ParamsAccessor>
inline ArpSIDForensicConfig buildRawForensicConfigFromParams(ParamsAccessor&& getParam) noexcept {
    auto gp = [&](ParamID pid) noexcept -> float {
        const float v = getParam(pid);
        return std::isfinite(v) ? v : kParamInfos[(size_t)pid].defaultNorm;
    };
    auto enabledAmount = [&](ParamID enablePid, ParamID amountPid) noexcept -> float {
        return gp(enablePid) > 0.5f ? gp(amountPid) : 0.0f;
    };

    ArpSIDForensicConfig fc{};
    fc.enable               = gp(kParamForensicEnable) > 0.5f;
    fc.intensity            = gp(kParamForensicIntensity);
    fc.temperatureCelsius   = sidForensicTemperatureFromNormalized(gp(kParamForensicTemp));
    fc.supplyVoltage        = sidForensicSupplyFromNormalized(gp(kParamForensicSupply));
    fc.revision             = sidForensicRevisionFromNormalized(gp(kParamForensicRevision));
    fc.chipIdSeed           = sidForensicChipSeedFromNormalized(gp(kParamForensicChipSeed));
    fc.startupRandomization = gp(kParamForensicStartupRandom) > 0.5f;
    fc.digifix8580          = gp(kParamForensicDigifix8580) > 0.5f;
    fc.clockJitterEnabled   = gp(kParamForensicClockJitterEnable) > 0.5f;
    fc.supplyRippleEnabled  = gp(kParamForensicSupplyRippleEnable) > 0.5f;
    fc.thermalDriftEnabled  = gp(kParamForensicThermalDriftEnable) > 0.5f;
    fc.voiceCrosstalkEnabled= gp(kParamForensicVoiceCrosstalkEnable) > 0.5f;
    fc.externalBleedEnabled = gp(kParamForensicExternalBleedEnable) > 0.5f;
    fc.clockJitter          = enabledAmount(kParamForensicClockJitterEnable,      kParamForensicClockJitter);
    fc.supplyRipple         = enabledAmount(kParamForensicSupplyRippleEnable,     kParamForensicSupplyRipple);
    fc.thermalDrift         = enabledAmount(kParamForensicThermalDriftEnable,     kParamForensicThermalDrift);
    fc.voiceCrosstalk       = enabledAmount(kParamForensicVoiceCrosstalkEnable,   kParamForensicVoiceCrosstalk);
    fc.externalBleed        = enabledAmount(kParamForensicExternalBleedEnable,    kParamForensicExternalBleed);
    fc.envelopeTDM          = gp(kParamForensicEnvelopeTDM);
    fc.d418Asymmetry        = gp(kParamForensicD418Asymmetry);
    fc.filterOhmic          = gp(kParamForensicFilterOhmic);
    fc.systemNoise          = gp(kParamForensicSystemNoise);
    fc.motherboard          = gp(kParamForensicMotherboard);
    fc.adcBleed             = gp(kParamForensicADCBleed);
    fc.busCollision         = gp(kParamForensicBusCollision);
    fc.potInput             = gp(kParamForensicPOTInput);
    return fc;
}

template <typename ParamsAccessor>
inline ArpSIDForensicConfig buildEffectiveForensicConfigFromParams(ParamsAccessor&& getParam,
                                                                   const SidVariantProfile& variant,
                                                                   const SidStaticParams& staticParams) noexcept {
    return resolveEffectiveForensicConfig(buildRawForensicConfigFromParams(std::forward<ParamsAccessor>(getParam)),
                                          variant,
                                          staticParams);
}

inline ArpSIDForensicConfig resolveEffectiveForensicConfig(const ArpSIDForensicConfig& raw,
                                                          const SidVariantProfile& variant,
                                                          const SidStaticParams& staticParams) noexcept {
    ArpSIDForensicConfig fc = raw;
    const bool is6581 = variant.family == SidFamily::MOS6581;
    fc.intensity = std::clamp(std::isfinite(fc.intensity) ? fc.intensity : 0.0f, 0.0f, 1.0f);
    auto clamp01 = [](float v) noexcept { return std::clamp(std::isfinite(v) ? v : 0.0f, 0.0f, 1.0f); };
    fc.temperatureCelsius = std::clamp(std::isfinite(fc.temperatureCelsius) ? fc.temperatureCelsius : 35.0f,
                                       kSidForensicTemperatureMin, kSidForensicTemperatureMax);
    fc.supplyVoltage = std::clamp(std::isfinite(fc.supplyVoltage) ? fc.supplyVoltage : 5.0f,
                                  kSidForensicSupplyMin, kSidForensicSupplyMax);
    fc.revision = sidForensicRevisionForVariant(fc.revision, variant.family);
    if (fc.chipIdSeed == 0u) fc.chipIdSeed = static_cast<uint32_t>(variant.chip_revision_code ^ 0xDEADBEEFu);
    fc.clockJitter = clamp01(fc.clockJitter) * std::clamp(staticParams.video_clock_scale, 0.5f, 1.5f);
    fc.supplyRipple = clamp01(fc.supplyRipple) * (1.0f + std::clamp(staticParams.supply_sag_sensitivity, 0.0f, 1.0f));
    fc.thermalDrift = clamp01(fc.thermalDrift) * (1.0f + std::clamp(staticParams.thermal_tracking, 0.0f, 1.0f));
    fc.voiceCrosstalk = clamp01(fc.voiceCrosstalk) * (1.0f + std::clamp(staticParams.board_crosstalk, 0.0f, 1.0f));
    fc.externalBleed = clamp01(fc.externalBleed) * (1.0f + std::clamp(staticParams.board_crosstalk * 0.5f, 0.0f, 0.5f));
    fc.envelopeTDM = clamp01(fc.envelopeTDM) * (is6581 ? 1.0f : 0.85f);
    fc.d418Asymmetry = clamp01(fc.d418Asymmetry) * (is6581 ? 1.0f : 0.35f);
    fc.filterOhmic = clamp01(fc.filterOhmic) * (1.0f + std::clamp(staticParams.supply_sag_sensitivity * 0.5f, 0.0f, 0.5f));
    fc.systemNoise = clamp01(fc.systemNoise) * (1.0f + std::clamp(staticParams.dc_bias_millivolts / 100.0f, 0.0f, 0.5f));
    fc.motherboard = clamp01(fc.motherboard) * (1.0f + std::clamp(staticParams.board_crosstalk, 0.0f, 0.75f));
    fc.adcBleed = clamp01(fc.adcBleed) * (is6581 ? 1.0f : 0.75f);
    fc.busCollision = clamp01(fc.busCollision) * (1.0f + std::clamp(staticParams.waveform_memory, 0.0f, 0.5f));
    fc.potInput = clamp01(fc.potInput) * (1.0f + std::clamp(staticParams.dc_bias_millivolts / 120.0f, 0.0f, 0.5f));
    fc.digifix8580 = (!is6581) && fc.digifix8580;
    fc.clockJitter = clamp01(fc.clockJitter);
    fc.supplyRipple = clamp01(fc.supplyRipple);
    fc.thermalDrift = clamp01(fc.thermalDrift);
    fc.voiceCrosstalk = clamp01(fc.voiceCrosstalk);
    fc.externalBleed = clamp01(fc.externalBleed);
    fc.envelopeTDM = clamp01(fc.envelopeTDM);
    fc.d418Asymmetry = clamp01(fc.d418Asymmetry);
    fc.filterOhmic = clamp01(fc.filterOhmic);
    fc.systemNoise = clamp01(fc.systemNoise);
    fc.motherboard = clamp01(fc.motherboard);
    fc.adcBleed = clamp01(fc.adcBleed);
    fc.busCollision = clamp01(fc.busCollision);
    fc.potInput = clamp01(fc.potInput);
    fc.thermalTimeConstantSeconds = std::clamp(std::isfinite(fc.thermalTimeConstantSeconds) ? fc.thermalTimeConstantSeconds : 18.0f, 1.0f, 180.0f);

    fc.supplyRippleMv = std::clamp(std::isfinite(fc.supplyRippleMv) ? fc.supplyRippleMv : 0.0f, 0.0f, 250.0f);
    fc.junctionTempC = std::clamp(std::isfinite(fc.junctionTempC) ? fc.junctionTempC : fc.temperatureCelsius, 15.0f, 92.0f);
    fc.caseTempC = std::clamp(std::isfinite(fc.caseTempC) ? fc.caseTempC : fc.temperatureCelsius, 15.0f, 68.0f);
    fc.ambientTempC = std::clamp(std::isfinite(fc.ambientTempC) ? fc.ambientTempC : fc.temperatureCelsius, 0.0f, 55.0f);
    fc.thermalResistanceJC = std::clamp(std::isfinite(fc.thermalResistanceJC) ? fc.thermalResistanceJC : 8.5f, 1.0f, 80.0f);
    fc.thermalResistanceCA = std::clamp(std::isfinite(fc.thermalResistanceCA) ? fc.thermalResistanceCA : 35.0f, 1.0f, 160.0f);
    fc.thermalCapacitanceJ = std::clamp(std::isfinite(fc.thermalCapacitanceJ) ? fc.thermalCapacitanceJ : 0.012f, 0.001f, 1.0f);
    fc.thermalCapacitanceC = std::clamp(std::isfinite(fc.thermalCapacitanceC) ? fc.thermalCapacitanceC : 0.085f, 0.001f, 4.0f);
    if (fc.frozenNoiseSeed == 0u) fc.frozenNoiseSeed = 0xA5A5A5A5u;
    if (fc.frozen()) {
        fc.clockJitter = 0.0f;
        fc.supplyRipple = 0.0f;
        fc.thermalDrift = 0.0f;
        fc.voiceCrosstalk = 0.0f;
        fc.externalBleed = 0.0f;
        fc.envelopeTDM = 0.0f;
        fc.d418Asymmetry = 0.0f;
        fc.filterOhmic = 0.0f;
        fc.systemNoise = 0.0f;
        fc.motherboard = 0.0f;
        fc.adcBleed = 0.0f;
        fc.busCollision = 0.0f;
        fc.potInput = 0.0f;
        fc.supplyRippleMv = 0.0f;
        fc.junctionTempC = fc.caseTempC = fc.ambientTempC = fc.temperatureCelsius;
    }
    return fc;
}

} // namespace ArpSID
