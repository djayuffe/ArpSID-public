// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once
#include <cstdint>
#include <algorithm>
#include <cmath>

namespace ArpSID {

enum class SidFamily : uint8_t {
    MOS6581 = 0,
    MOS8580 = 1
};

enum class SidVideoStandard : uint8_t {
    PAL = 0,
    NTSC = 1
};

enum class SidBoardRevision : uint16_t {
    Unknown = 0,
    C64_Assy_250407,
    C64_Assy_250425,
    C64_Assy_250466,
    C64C_Assy_250469,
    C128_Generic,
    CleanLabBoard,
    Custom
};

enum class SidOutputStageProfile : uint16_t {
    Unknown = 0,
    StockC64_6581,
    StockC64_8580,
    C64C_Modified,
    DirectLineOut,
    StudioCapture,
    Custom
};

inline constexpr float sidDefaultClockHz(SidVideoStandard video) noexcept {
    return video == SidVideoStandard::NTSC ? 1022727.0f : 985248.0f;
}

inline constexpr SidBoardRevision sidDefaultBoardRevisionForProfile(SidFamily family) noexcept {
    return family == SidFamily::MOS6581 ? SidBoardRevision::C64_Assy_250425
                                        : SidBoardRevision::C64C_Assy_250469;
}

inline constexpr SidOutputStageProfile sidDefaultOutputStageForProfile(SidFamily family,
                                                                       SidBoardRevision board) noexcept {
    if (board == SidBoardRevision::CleanLabBoard) return SidOutputStageProfile::DirectLineOut;
    if (board == SidBoardRevision::Custom) return SidOutputStageProfile::Custom;
    return family == SidFamily::MOS6581 ? SidOutputStageProfile::StockC64_6581
                                        : SidOutputStageProfile::StockC64_8580;
}

struct SidVariantProfile {
    SidFamily family = SidFamily::MOS8580;
    uint32_t chip_revision_code = 0;
    SidVideoStandard video_standard = SidVideoStandard::PAL;
    SidBoardRevision board_revision = SidBoardRevision::Unknown;
    SidOutputStageProfile output_stage = SidOutputStageProfile::Unknown;
    float master_clock_hz = 985248.0f;
    float nominal_sid_clock_hz = 985248.0f;
    bool allow_runtime_variant_switch = true;

    void sanitize() noexcept {
        if (family != SidFamily::MOS6581 && family != SidFamily::MOS8580) {
            family = SidFamily::MOS8580;
        }
        if (video_standard != SidVideoStandard::PAL && video_standard != SidVideoStandard::NTSC) {
            video_standard = SidVideoStandard::PAL;
        }
        if (static_cast<uint16_t>(board_revision) > static_cast<uint16_t>(SidBoardRevision::Custom)) {
            board_revision = SidBoardRevision::Unknown;
        }
        if (static_cast<uint16_t>(output_stage) > static_cast<uint16_t>(SidOutputStageProfile::Custom)) {
            output_stage = SidOutputStageProfile::Unknown;
        }
        if (board_revision == SidBoardRevision::Unknown) {
            board_revision = sidDefaultBoardRevisionForProfile(family);
        }
        if (output_stage == SidOutputStageProfile::Unknown) {
            output_stage = sidDefaultOutputStageForProfile(family, board_revision);
        }
        const float defaultClock = sidDefaultClockHz(video_standard);
        if (!std::isfinite(master_clock_hz) || master_clock_hz < 1.0f) {
            master_clock_hz = defaultClock;
        }
        if (!std::isfinite(nominal_sid_clock_hz) || nominal_sid_clock_hz < 1.0f) {
            nominal_sid_clock_hz = master_clock_hz;
        }
        master_clock_hz = std::clamp(master_clock_hz, defaultClock * 0.90f, defaultClock * 1.10f);
        nominal_sid_clock_hz = std::clamp(nominal_sid_clock_hz, master_clock_hz * 0.90f, master_clock_hz * 1.10f);
    }

    bool is6581() const noexcept { return family == SidFamily::MOS6581; }
    bool is8580() const noexcept { return family == SidFamily::MOS8580; }
};

inline SidVariantProfile canonicalVariantProfileFromPackedBits(uint32_t bits,
                                                              const SidVariantProfile& fallback) noexcept {
    SidVariantProfile profile = fallback;
    profile.family = (bits & 0x1u) ? SidFamily::MOS8580 : SidFamily::MOS6581;
    profile.video_standard = (bits & 0x2u) ? SidVideoStandard::NTSC : SidVideoStandard::PAL;
    profile.master_clock_hz = sidDefaultClockHz(profile.video_standard);
    profile.nominal_sid_clock_hz = profile.master_clock_hz;
    profile.allow_runtime_variant_switch = (bits & 0x4u) != 0u;
    profile.sanitize();
    return profile;
}

inline void sidSetVariantVideoStandard(SidVariantProfile& profile,
                                       SidVideoStandard video) noexcept {
    profile.video_standard = video;
    profile.master_clock_hz = sidDefaultClockHz(video);
    profile.nominal_sid_clock_hz = profile.master_clock_hz;
}

inline uint32_t canonicalPackVariantProfileEventBits(const SidVariantProfile& profile) noexcept {
    uint32_t bits = 0u;
    if (profile.family == SidFamily::MOS8580) bits |= 0x1u;
    if (profile.video_standard == SidVideoStandard::NTSC) bits |= 0x2u;
    if (profile.allow_runtime_variant_switch) bits |= 0x4u;
    return bits;
}

inline SidVariantProfile sidDefaultVariantProfile(SidFamily family = SidFamily::MOS8580,
                                                  SidVideoStandard video = SidVideoStandard::PAL) noexcept {
    SidVariantProfile p{};
    p.family = family;
    p.video_standard = video;
    p.board_revision = sidDefaultBoardRevisionForProfile(family);
    p.output_stage = sidDefaultOutputStageForProfile(family, p.board_revision);
    p.master_clock_hz = sidDefaultClockHz(video);
    p.nominal_sid_clock_hz = p.master_clock_hz;
    p.sanitize();
    return p;
}

} // namespace ArpSID
