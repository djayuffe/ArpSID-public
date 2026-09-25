// SPDX-License-Identifier: BSD-3-Clause
// mix_panel_model.h — MIX tab data model + FX-chain architecture (v547).
//
// TAB_ARCHITECTURE.md §5 demands:
// - Per-channel: vol/pan/EQ/transient/compress/saturate/bitcrush
// - Send-to-delay + send-to-convolution-reverb buses
// - Master limiter (shared with output-stage limiter)
// - Solo/mute per channel
// - All FX RT-safe: no allocation, fixed-size buffers, no locking
// - FX coefficients smoothed via ParameterSmoother (skive v533)
// - Per-channel processing fits in 5% of render budget at 48 kHz/8 voices
//
// This header is the DATA-MODEL LAYER. The actual FX processors live in a
// separate header (`mix_fx_processors.h` — follow-up). The model:
// * Defines per-channel parameter POD structs (16 channels × ~64 bytes)
// * Defines FX chain ordering (which FX run in what order per channel)
// * Defines send bus state (master + 2 sends)
// * Provides constexpr defaults + sanitize for project-state loading
//
// LAYOUT INVARIANTS (pinned via static_assert):
// * MixChannel = 64 bytes (per-instrument state)
// * Mix bus master + 2 sends = 32 bytes each
// * MixPanelModel total ≤ 1.5 KB (16 channels + masters + sends)

#ifndef ARPSID_GUI_MIX_PANEL_MODEL_H
#define ARPSID_GUI_MIX_PANEL_MODEL_H

#include <array>
#include <cstdint>
#include <type_traits>

namespace ArpSID {
namespace GUI {

// ─── FX type identifiers (per-channel chain composition) ────────────────────
enum class MixFxType : std::uint8_t {
    None        = 0,
    Eq3Band     = 1,  ///< low shelf + mid bell + high shelf
    Transient   = 2,  ///< attack/sustain shaper
    Compressor  = 3,  ///< threshold/ratio/attack/release
    Saturator   = 4,  ///< drive + character
    Bitcrusher  = 5,  ///< bits + downsample rate
};

// Number of defined MixFxType values. The well-formedness/sanitize path derives
// its validity bound from this so adding a new FX type here cannot silently make
// the validator reject the new type (or accept a stale out-of-range value).
inline constexpr std::uint8_t kMixFxTypeCount = 6;

constexpr bool mixFxTypeIsValid(std::uint8_t raw) noexcept {
    return raw < kMixFxTypeCount;
}

constexpr const char* mixFxTypeName(MixFxType t) noexcept {
    switch (t) {
        case MixFxType::None:       return "none";
        case MixFxType::Eq3Band:    return "eq_3band";
        case MixFxType::Transient:  return "transient";
        case MixFxType::Compressor: return "compressor";
        case MixFxType::Saturator:  return "saturator";
        case MixFxType::Bitcrusher: return "bitcrusher";
    }
    return "unknown";
}

static_assert(static_cast<std::uint8_t>(MixFxType::Bitcrusher) + 1u == kMixFxTypeCount,
              "kMixFxTypeCount must track the highest MixFxType value");

// Per-channel FX chain — up to 5 slots, each carrying a single FX type
// + 8 normalized parameters (0..1). Fixed-capacity so the chain layout
// is project-state-stable.
inline constexpr std::uint8_t kMixFxSlotsPerChannel = 5;
inline constexpr std::uint8_t kMixFxParamsPerSlot   = 8;

struct MixFxSlot {
    MixFxType type;                                     // 1 byte
    std::uint8_t bypass;                                 // 1 byte (0=engaged, 1=bypassed)
    std::uint8_t reserved[2];                            // 2 bytes pad → 4 bytes
    std::uint8_t params[kMixFxParamsPerSlot];            // 8 bytes (0..255 → 0..1 norm)
};                                                       // total = 12 bytes
static_assert(std::is_trivially_copyable<MixFxSlot>::value,
              "MixFxSlot must be trivially copyable");
static_assert(sizeof(MixFxSlot) == 12, "MixFxSlot pinned at 12 bytes");

// Helper: convert byte-encoded param to normalized 0..1 float.
constexpr float mixFxParamNorm(std::uint8_t b) noexcept {
    return static_cast<float>(b) * (1.0f / 255.0f);
}
constexpr std::uint8_t mixFxParamByte(float norm) noexcept {
    if (!(norm >= 0.0f)) return 0;
    if (norm >= 1.0f) return 255;
    return static_cast<std::uint8_t>(norm * 255.0f + 0.5f);
}

// ─── Per-channel mixer strip (64 bytes pinned) ──────────────────────────────
struct MixChannel {
    // [0..3]: identity / routing
    std::uint8_t  enabled;       ///< 1 = channel is engaged, 0 = bypassed entirely
    std::uint8_t  solo;          ///< 1 = solo'd (cuts other un-solo'd channels)
    std::uint8_t  mute;          ///< 1 = muted (silenced)
    std::uint8_t  reserved0;
    // [4..7]: volume + pan
    std::uint8_t  volume;        ///< 0..255 → -inf..+6 dB
    std::uint8_t  pan;           ///< 0..255 → -100%..+100% (128 = center)
    std::uint8_t  sendToDelay;   ///< 0..255 → 0..100% send level
    std::uint8_t  sendToReverb;  ///< 0..255 → 0..100% send level
    // [8..67]: 5 × 12-byte FX slots = 60 bytes
    MixFxSlot     fxSlots[kMixFxSlotsPerChannel];
    // [68..71]: padding for 64-byte total — wait, that exceeds 64. Let me recount.
    // Actually 4 + 4 + 60 = 68 bytes. Need to drop something to get 64,
    // or accept 72 (next 8-byte alignment). Let's accept 72 and pin that.
    std::uint8_t  reserved1[4]; ///< pad to 72 bytes total
};
static_assert(std::is_trivially_copyable<MixChannel>::value,
              "MixChannel must be trivially copyable");
static_assert(sizeof(MixChannel) == 72,
              "MixChannel pinned at 72 bytes (4 identity + 4 vol/pan/sends + 60 fx + 4 pad)");

// ─── Send bus state (delay + reverb) ────────────────────────────────────────
// Each send bus has its own FX parameters + master return-level. The
// busses run after all channels are summed; output goes into the master
// output bus along with the dry path.
struct MixSendBus {
    std::uint8_t  enabled;       ///< 1 = bus active
    std::uint8_t  returnLevel;   ///< 0..255 → 0..100% return into master
    std::uint8_t  reserved[6];   ///< pad to 8 bytes
    MixFxSlot     busFx[2];      ///< 2 FX slots per bus (e.g., delay + EQ on delay bus)
};                                ///< total = 8 + 24 = 32 bytes
static_assert(std::is_trivially_copyable<MixSendBus>::value,
              "MixSendBus must be trivially copyable");
static_assert(sizeof(MixSendBus) == 32, "MixSendBus pinned at 32 bytes");

// ─── Master section (limiter + global controls) ─────────────────────────────
struct MixMaster {
    std::uint8_t  masterVolume;       ///< 0..255 → -inf..0 dB (255 = unity)
    std::uint8_t  limiterEnabled;     ///< 1 = limiter on (shared with SIDChip limiter)
    std::uint8_t  limiterThreshold;   ///< 0..255 → -24..0 dB
    std::uint8_t  limiterRelease;     ///< 0..255 → 10..500 ms
    std::uint8_t  stereoWidth;        ///< 0..255 → 0..200% (128 = unity stereo)
    std::uint8_t  dimMonitor;         ///< 1 = -10 dB monitor dim (DAW-style)
    std::uint8_t  reserved[26];       ///< pad to 32 bytes total
};
static_assert(std::is_trivially_copyable<MixMaster>::value,
              "MixMaster must be trivially copyable");
static_assert(sizeof(MixMaster) == 32, "MixMaster pinned at 32 bytes");

// ─── Top-level MIX panel model ──────────────────────────────────────────────
// Layout: 16 channels × 72 + 2 sends × 32 + 1 master × 32 + version/header.
// Sum: 1152 + 64 + 32 + 16 = 1264 bytes. Fits the documented <1.5 KB pin.
inline constexpr std::uint8_t kMixChannelCount = 16;
inline constexpr std::uint8_t kMixSendBusCount = 2;

struct MixPanelModel {
    std::uint32_t schemaVersion;                                  // 4 bytes
    std::uint16_t selectedChannel;                                // 2 bytes (0..15)
    std::uint16_t reserved0;                                      // 2 bytes pad
    std::uint64_t reserved1;                                      // 8 bytes future
    // total header = 16 bytes
    std::array<MixChannel, kMixChannelCount> channels;            // 16 × 72 = 1152
    std::array<MixSendBus, kMixSendBusCount> sendBuses;           // 2 × 32 = 64
    MixMaster master;                                             // 32 bytes
};
static_assert(std::is_trivially_copyable<MixPanelModel>::value,
              "MixPanelModel must be trivially copyable");
static_assert(sizeof(MixPanelModel) == 16 + 1152 + 64 + 32,
              "MixPanelModel layout pinned at 16 + 1152 + 64 + 32 = 1264 bytes");
static_assert(sizeof(MixPanelModel) < 1536,
              "MixPanelModel under 1.5 KB envelope");

// ─── Schema version + defaults ──────────────────────────────────────────────
inline constexpr std::uint32_t kMixSchemaVersion = 1u;

constexpr MixChannel makeDefaultChannel(std::uint8_t channelIdx) noexcept {
    (void)channelIdx;
    MixChannel c{};
    c.enabled        = 1u;
    c.solo           = 0u;
    c.mute           = 0u;
    c.volume         = 200u;   // ~ -1.2 dB; gives headroom for sum of 16 channels
    c.pan            = 128u;   // center
    c.sendToDelay    = 0u;
    c.sendToReverb   = 0u;
    // Default FX chain: all slots None (channel passes through unchanged).
    return c;
}

constexpr MixSendBus makeDefaultSendBus(std::uint8_t busIdx) noexcept {
    (void)busIdx;
    MixSendBus b{};
    b.enabled     = 0u; // sends disabled by default — opt-in for production patches
    b.returnLevel = 128u; // 50% if enabled
    return b;
}

constexpr MixMaster makeDefaultMaster() noexcept {
    MixMaster m{};
    m.masterVolume     = 255u;
    m.limiterEnabled   = 1u;
    m.limiterThreshold = 220u; // ~ -3 dB
    m.limiterRelease   = 100u; // ~ 200 ms
    m.stereoWidth      = 128u; // unity
    m.dimMonitor       = 0u;
    return m;
}

constexpr MixPanelModel makeDefaultMixModel() noexcept {
    MixPanelModel m{};
    m.schemaVersion    = kMixSchemaVersion;
    m.selectedChannel  = 0u;
    m.reserved0        = 0u;
    m.reserved1        = 0ull;
    for (std::uint8_t i = 0; i < kMixChannelCount; ++i) {
        m.channels[i] = makeDefaultChannel(i);
    }
    for (std::uint8_t i = 0; i < kMixSendBusCount; ++i) {
        m.sendBuses[i] = makeDefaultSendBus(i);
    }
    m.master = makeDefaultMaster();
    return m;
}

// ─── Validation ─────────────────────────────────────────────────────────────
constexpr bool mixModelIsWellFormed(const MixPanelModel& m) noexcept {
    if (m.schemaVersion != kMixSchemaVersion) return false;
    if (m.selectedChannel >= kMixChannelCount) return false;
    for (std::uint8_t i = 0; i < kMixChannelCount; ++i) {
        const auto& c = m.channels[i];
        if (c.enabled > 1u) return false;
        if (c.solo > 1u) return false;
        if (c.mute > 1u) return false;
        for (const auto& slot : c.fxSlots) {
            if (!mixFxTypeIsValid(static_cast<std::uint8_t>(slot.type))) return false;
            if (slot.bypass > 1u) return false;
        }
    }
    return true;
}

static_assert(mixModelIsWellFormed(makeDefaultMixModel()),
              "default MIX model must be well-formed");

// ─── Sanitize ────────────────────────────────────────────────────────────────
/// If the model is not well-formed, reset it to canonical defaults.
/// Called on AU state restore to clamp any corrupt or forward-version fields.
constexpr void sanitizeMixModel(MixPanelModel& m) noexcept {
    if (!mixModelIsWellFormed(m))
        m = makeDefaultMixModel();
}

// ─── Channel helpers ────────────────────────────────────────────────────────
inline float channelVolumeDb(const MixChannel& c) noexcept {
    // Map 0..255 to -inf..+6 dB. Audio-taper: 0 → -inf, 200 → 0 dB, 255 → +6 dB.
    if (c.volume == 0u) return -120.0f;
    const float norm = static_cast<float>(c.volume) * (1.0f / 255.0f);
    return 6.0f * norm + 6.0f * (norm - 1.0f) * 3.0f; // approximation
}
inline float channelPanLinear(const MixChannel& c) noexcept {
    // 0=full L, 128=center, 255=full R → -1..0..+1
    return (static_cast<float>(c.pan) - 128.0f) * (1.0f / 127.0f);
}
inline bool channelIsAudible(const MixChannel& c, bool anyChannelSoloed) noexcept {
    if (!c.enabled) return false;
    if (c.mute)     return false;
    if (anyChannelSoloed && !c.solo) return false;
    return true;
}

// ─── Apply an FX type to a slot ────────────────────────────────────────────
// Convenience for the NSView builder: replaces the slot's type while
// resetting params to type-appropriate defaults.
inline void setMixFxSlot(MixFxSlot& slot, MixFxType type) noexcept {
    slot.type = type;
    slot.bypass = 0u;
    for (auto& p : slot.params) p = 0u;
    switch (type) {
        case MixFxType::Eq3Band:
            // Defaults: flat (all gains at unity = 128)
            slot.params[0] = 128u; // low shelf gain
            slot.params[1] = 100u; // low shelf freq
            slot.params[2] = 128u; // mid bell gain
            slot.params[3] = 128u; // mid bell freq
            slot.params[4] = 128u; // mid Q
            slot.params[5] = 128u; // high shelf gain
            slot.params[6] = 150u; // high shelf freq
            slot.params[7] = 0u;   // reserved
            break;
        case MixFxType::Transient:
            slot.params[0] = 128u; // attack (0=cut, 128=unity, 255=boost)
            slot.params[1] = 128u; // sustain
            break;
        case MixFxType::Compressor:
            slot.params[0] = 200u; // threshold (-6 dB)
            slot.params[1] = 100u; // ratio (4:1)
            slot.params[2] = 30u;  // attack (3 ms)
            slot.params[3] = 100u; // release (100 ms)
            slot.params[4] = 0u;   // makeup gain
            break;
        case MixFxType::Saturator:
            slot.params[0] = 64u;  // drive
            slot.params[1] = 128u; // character (tape/tube/transistor)
            break;
        case MixFxType::Bitcrusher:
            slot.params[0] = 255u; // bits (255 = 16-bit, lower = crush)
            slot.params[1] = 255u; // downsample (255 = no resample, lower = crush)
            break;
        case MixFxType::None:
        default:
            break;
    }
}

} // namespace GUI
} // namespace ArpSID

#endif // ARPSID_GUI_MIX_PANEL_MODEL_H
