#pragma once
// PSID / RSID file header parser — deterministic, RT-safe, zero allocation.

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace ArpSID {

// Parse strictness. StrictSpec rejects out-of-range song metadata (validation tools,
// conformance tests). SidTuneCompatible clamps it the way libsidplayfp/sidtune does
// so real-world tunes with sloppy song counts still play. Playback uses Compatible;
// the default stays Strict so validators and pinned conformance tests are unaffected.
enum class PsidParsePolicy : uint8_t {
    StrictSpec = 0,
    SidTuneCompatible = 1,
};

enum class PsidParseResult : uint8_t {
    OK = 0,
    BadMagic = 1,
    TooShort = 2,
    BadVersion = 3,
    BadOffset = 4,
    BadSongCount = 5,
    BadStartSong = 6,
    BadRsidHeader = 7,
    UnsupportedMultiSid = 8,
    BadSidAddress = 9,
    UnsupportedMusSpecific = 10,   // PSID flags bit 0: Compute! Sidplayer MUS data (not 6502 code)
    UnsupportedRsidBasic = 11,     // RSID flags bit 1: requires C64 BASIC warm-start (not implemented)
    DuplicateSidBase = 12,         // two SID chips map to the same base — ambiguous/invalid
    BadRelocationRange = 13,      // startPage/pageLength invalid or overlaps loaded tune payload
};

static inline const char* psidParseResultName(PsidParseResult r) noexcept {
    switch (r) {
        case PsidParseResult::OK: return "OK";
        case PsidParseResult::BadMagic: return "BadMagic";
        case PsidParseResult::TooShort: return "TooShort";
        case PsidParseResult::BadVersion: return "BadVersion";
        case PsidParseResult::BadOffset: return "BadOffset";
        case PsidParseResult::BadSongCount: return "BadSongCount";
        case PsidParseResult::BadStartSong: return "BadStartSong";
        case PsidParseResult::BadRsidHeader: return "BadRsidHeader";
        case PsidParseResult::UnsupportedMultiSid: return "UnsupportedMultiSid";
        case PsidParseResult::BadSidAddress: return "BadSidAddress";
        case PsidParseResult::UnsupportedMusSpecific: return "UnsupportedMusSpecific";
        case PsidParseResult::UnsupportedRsidBasic: return "UnsupportedRsidBasic";
        case PsidParseResult::DuplicateSidBase: return "DuplicateSidBase";
        case PsidParseResult::BadRelocationRange: return "BadRelocationRange";
    }
    return "Unknown";
}

// Normalized video-clock and SID-model projections of the PSID v2NG flags word.
// The enum values match the 2-bit flag encodings directly (00 unknown, 01/10 the
// two concrete choices, 11 "any"), so a flags nibble casts straight to these.
enum class PsidClock : uint8_t {
    Unknown = 0,
    PAL = 1,
    NTSC = 2,
    Any = 3,
};

enum class PsidSidModel : uint8_t {
    Unknown = 0,
    MOS6581 = 1,
    MOS8580 = 2,
    Any = 3,
};

// Normalized file-class classification (v873 audit item 1). Note: "v4E"/version 0x4E
// is intentionally absent — it is not a real PSID/RSID format (versions are 1..4), so
// this runtime rejects version 0x4E as BadVersion rather than inventing a class for it.
enum class PsidCompatibility : uint8_t {
    Unknown = 0,
    PSIDv1,
    PSIDv2,
    PSIDv3,
    PSIDv4,
    RSIDv2,
    RSIDv3,
    RSIDv4,
    RSID_BASIC,
};

static inline PsidCompatibility psidResolveCompatibility(bool isRsid, uint16_t version,
                                                         uint16_t flags) noexcept {
    if (!isRsid) {
        switch (version) {
            case 1: return PsidCompatibility::PSIDv1;
            case 2: return PsidCompatibility::PSIDv2;
            case 3: return PsidCompatibility::PSIDv3;
            case 4: return PsidCompatibility::PSIDv4;
            default: return PsidCompatibility::Unknown;
        }
    }
    if ((flags & 0x0002u) != 0u) return PsidCompatibility::RSID_BASIC;
    switch (version) {
        case 2: return PsidCompatibility::RSIDv2;
        case 3: return PsidCompatibility::RSIDv3;
        case 4: return PsidCompatibility::RSIDv4;  // RSID v4 is valid HVSC (brief omitted it)
        default: return PsidCompatibility::Unknown;
    }
}

struct PsidHeader {
    bool isRsid = false;
    uint16_t version = 0;
    uint16_t dataOffset = 0;
    uint16_t loadAddr = 0;
    uint16_t initAddr = 0;
    uint16_t playAddr = 0;
    uint16_t songs = 0;
    uint16_t startSong = 0;
    // rawSpeed is the file speed word. speed/normalizedSpeed is playback-normalized:
    // for RSID it is 0xFFFFFFFF so every subtune is CIA-timed internally.
    uint32_t rawSpeed = 0;
    uint32_t speed = 0;
    uint32_t normalizedSpeed = 0;
    char name[33]{};
    char author[33]{};
    char released[33]{};
    uint16_t effectiveLoadAddr = 0;
    uint16_t flags = 0;
    uint8_t startPage = 0;
    uint8_t pageLength = 0;
    uint8_t secondSidAddress = 0;
    uint8_t thirdSidAddress = 0;
    uint8_t fourthSidAddress = 0;
    uint8_t fifthSidAddress = 0;
    // Normalized projections of the flags word (v873). clock = declared video
    // standard; sidModel[i] = declared model for SID chip i (max 3 SIDs). Extra-SID
    // models default to SID1's model when the header leaves them Unknown.
    PsidClock clock = PsidClock::Unknown;
    PsidSidModel sidModel[3] = {PsidSidModel::Unknown, PsidSidModel::Unknown, PsidSidModel::Unknown};
    PsidCompatibility compatibility = PsidCompatibility::Unknown;
    const uint8_t* payload = nullptr;
    uint32_t payloadLen = 0;
};

static inline uint16_t psidBE16(const uint8_t* p) noexcept {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8u) | p[1]);
}
static inline uint32_t psidBE32(const uint8_t* p) noexcept {
    return (static_cast<uint32_t>(p[0]) << 24u) |
           (static_cast<uint32_t>(p[1]) << 16u) |
           (static_cast<uint32_t>(p[2]) << 8u) |
           static_cast<uint32_t>(p[3]);
}

static inline bool psidSidAddressBytePresent(uint8_t b) noexcept { return b != 0u; }

static inline uint16_t psidSidAddressByteToBase(uint8_t b) noexcept {
    // PSID v2NG encodes extra SID bases as even address selector bytes.
    // 0x42 -> $D420, 0x44 -> $D440, ... 0xFE -> $DFE0.
    return static_cast<uint16_t>(0xD000u | (static_cast<unsigned>(b) << 4u));
}

// Valid extra-SID base regions per the HVSC/libsidplayfp PSID v2NG spec:
//   $D420-$D7E0  and  $DE00-$DFE0
// with $20 alignment (even selector byte). The previous code rejected the entire
// $D800-$DFFF range, which wrongly refused the legal $DE00-$DFE0 window used by real
// stereo/3-SID tunes (v873 audit item 8). $D800-$DDFF (color RAM / unmapped IO) stays
// invalid.
static inline bool psidValidExtraSidBase(uint16_t base) noexcept {
    const bool inSidRange = (base >= 0xD420u && base < 0xD800u) ||
                            (base >= 0xDE00u && base <= 0xDFE0u);
    return inSidRange && ((base & 0x10u) == 0u);
}

// The single source of truth for "is this a legal CONFIGURED SID window", shared
// by psidParse(), the runtime bus router (C64Platform::configurePsidSidBases /
// sidBasesValidUnique_) and their tests. The primary SID is fixed at $D400; extra
// SIDs must fall in the legal $D420-$D7E0 / $DE00-$DFE0 windows. Keeping one
// validator prevents the split-brain where a base is parser-valid but the runtime
// bus mis-routes it (e.g. $DE00) or runtime-valid but parser-illegal (e.g. $DC00)
// (v874 audit P0-2 / P1-9).
static inline bool psidValidConfiguredSidBase(uint16_t base, uint8_t chip) noexcept {
    return chip == 0u ? (base == 0xD400u) : psidValidExtraSidBase(base);
}

static inline bool psidValidExtraSidAddressByte(uint8_t b) noexcept {
    if (!psidSidAddressBytePresent(b)) return true;
    if ((b & 1u) != 0u) return false;
    return psidValidExtraSidBase(psidSidAddressByteToBase(b));
}

static inline bool psidHasExtraSidChips(const PsidHeader& h) noexcept {
    return psidSidAddressBytePresent(h.secondSidAddress) ||
           psidSidAddressBytePresent(h.thirdSidAddress) ||
           psidSidAddressBytePresent(h.fourthSidAddress) ||
           psidSidAddressBytePresent(h.fifthSidAddress);
}

static inline uint8_t psidSidChipCount(const PsidHeader& h) noexcept {
    uint8_t n = 1;
    if (psidSidAddressBytePresent(h.secondSidAddress)) n = 2;
    if (psidSidAddressBytePresent(h.thirdSidAddress)) n = 3;
    if (psidSidAddressBytePresent(h.fourthSidAddress)) n = 4;
    if (psidSidAddressBytePresent(h.fifthSidAddress)) n = 5;
    return n;
}

static inline uint16_t psidSidBaseForChip(const PsidHeader& h, uint8_t chip) noexcept {
    switch (chip) {
        case 0: return 0xD400u;
        case 1: return psidSidAddressBytePresent(h.secondSidAddress) ? psidSidAddressByteToBase(h.secondSidAddress) : 0u;
        case 2: return psidSidAddressBytePresent(h.thirdSidAddress) ? psidSidAddressByteToBase(h.thirdSidAddress) : 0u;
        case 3: return psidSidAddressBytePresent(h.fourthSidAddress) ? psidSidAddressByteToBase(h.fourthSidAddress) : 0u;
        case 4: return psidSidAddressBytePresent(h.fifthSidAddress) ? psidSidAddressByteToBase(h.fifthSidAddress) : 0u;
        default: return 0u;
    }
}

// Two SID chips at the same base is ambiguous/invalid metadata (v873 audit item 2).
static inline bool psidRelocationRangeValid(uint8_t startPage, uint8_t pageLength,
                                                uint16_t loadAddr, uint32_t payloadLen) noexcept {
    if (startPage == 0u && pageLength == 0u) return true;
    if (startPage == 0u || pageLength == 0u) return false;
    if (startPage < 0x04u) return false; // never allow zero page, stack, or system vectors as relocation workspace
    const uint32_t relStart = static_cast<uint32_t>(startPage) << 8u;
    const uint32_t relEnd = relStart + (static_cast<uint32_t>(pageLength) << 8u);
    if (relEnd > 0x10000u || relEnd <= relStart) return false;
    if (payloadLen == 0u) return true;
    const uint32_t tuneStart = static_cast<uint32_t>(loadAddr);
    const uint32_t tuneEnd = std::min<uint32_t>(0x10000u, tuneStart + payloadLen);
    return !(relStart < tuneEnd && tuneStart < relEnd);
}

static inline bool psidSidBasesUnique(const PsidHeader& h) noexcept {
    const uint16_t bases[5] = {
        0xD400u, psidSidBaseForChip(h, 1), psidSidBaseForChip(h, 2),
        psidSidBaseForChip(h, 3), psidSidBaseForChip(h, 4),
    };
    for (uint8_t i = 0u; i < 5u; ++i) {
        if (bases[i] == 0u) continue;
        for (uint8_t j = 0u; j < i; ++j)
            if (bases[j] != 0u && bases[j] == bases[i]) return false;
    }
    return true;
}

static inline bool psidUsesCiaTimingForSong(const PsidHeader& h, uint16_t song) noexcept {
    if (h.isRsid) return true;
    const uint16_t oneBased = song ? song : h.startSong;
    if (oneBased == 0u) return false;
    // The 32-bit speed word carries one VBI/CIA bit per song for songs 1..32.
    // Songs beyond 32 reuse the 32nd song's bit (bit 31), matching libsidplayfp,
    // instead of silently defaulting all high subtunes to VBI.
    const uint16_t bit = oneBased > 32u ? 31u : static_cast<uint16_t>(oneBased - 1u);
    return ((h.speed >> bit) & 1u) != 0u;
}


inline PsidParseResult psidParse(const uint8_t* img, uint32_t len, PsidHeader& out,
                                 PsidParsePolicy policy = PsidParsePolicy::StrictSpec) noexcept {
    out = {};
    if (!img || len < 0x76u) return PsidParseResult::TooShort;
    if (img[0] == 'P' && img[1] == 'S' && img[2] == 'I' && img[3] == 'D') out.isRsid = false;
    else if (img[0] == 'R' && img[1] == 'S' && img[2] == 'I' && img[3] == 'D') out.isRsid = true;
    else return PsidParseResult::BadMagic;

    out.version = psidBE16(img + 0x04u);
    out.dataOffset = psidBE16(img + 0x06u);
    out.loadAddr = psidBE16(img + 0x08u);
    out.initAddr = psidBE16(img + 0x0Au);
    out.playAddr = psidBE16(img + 0x0Cu);
    out.songs = psidBE16(img + 0x0Eu);
    out.startSong = psidBE16(img + 0x10u);
    out.rawSpeed = psidBE32(img + 0x12u);
    out.speed = out.rawSpeed;
    out.normalizedSpeed = out.rawSpeed;
    if (out.version < 1u || out.version > 4u) return PsidParseResult::BadVersion;
    const uint16_t minOffset = (out.version >= 2u) ? 0x7Cu : 0x76u;
    if (out.dataOffset < minOffset) return PsidParseResult::BadOffset;
    if (len < static_cast<uint32_t>(out.dataOffset)) return PsidParseResult::TooShort;
    if (policy == PsidParsePolicy::SidTuneCompatible) {
        // sidtune/libsidplayfp normalization: never reject on song metadata, clamp it.
        if (out.songs == 0u) out.songs = 1u;
        if (out.songs > 256u) out.songs = 256u;
        if (out.startSong == 0u || out.startSong > out.songs) out.startSong = 1u;
    } else {
        if (out.songs == 0u) return PsidParseResult::BadSongCount;
        if (out.startSong == 0u || out.startSong > out.songs) return PsidParseResult::BadStartSong;
    }

    if (out.version >= 2u && len >= 0x7Cu) {
        // v2NG header extension at $76: flags, driver relocation pages, and — for
        // v3+ only — the extra-SID address selector bytes. Per the HVSC/libsidplayfp
        // PSID v2NG spec there are at most THREE SIDs, and the extra-SID bytes are
        // strictly version-gated:
        //   v2: $7A/$7B are RESERVED (mono). Reading them would misclassify a plain
        //       PSID v2 tune as stereo.
        //   v3: $7A selects the 2nd SID; $7B is reserved.
        //   v4: $7A selects the 2nd SID; $7B selects the 3rd SID.
        // The previous code read $7A/$7B for every v2+ file and additionally invented
        // 4th/5th SID selector bytes at $7C/$7D — which fall inside the tune payload,
        // not the header — so both v2 and v3/v4 tunes could be reported with more SID
        // chips than the format allows (fake stereo / payload-bytes-as-SID).
        out.flags = psidBE16(img + 0x76u);
        out.startPage = img[0x78u];
        out.pageLength = img[0x79u];
        if (out.version >= 3u) out.secondSidAddress = img[0x7Au];
        if (out.version >= 4u) out.thirdSidAddress = img[0x7Bu];
        // Normalized clock + per-SID model from the flags word (HVSC PSID v2NG:
        // bits 2-3 = video clock, bits 4-5 = SID1 model, bits 6-7 = SID2 model,
        // bits 8-9 = SID3 model). Extra-SID models left Unknown inherit SID1's
        // model, matching libsidplayfp (an Explicit "Any" is preserved).
        out.clock = static_cast<PsidClock>((out.flags >> 2u) & 0x3u);
        out.sidModel[0] = static_cast<PsidSidModel>((out.flags >> 4u) & 0x3u);
        if (psidSidAddressBytePresent(out.secondSidAddress)) {
            out.sidModel[1] = static_cast<PsidSidModel>((out.flags >> 6u) & 0x3u);
            if (out.sidModel[1] == PsidSidModel::Unknown) out.sidModel[1] = out.sidModel[0];
        }
        if (psidSidAddressBytePresent(out.thirdSidAddress)) {
            out.sidModel[2] = static_cast<PsidSidModel>((out.flags >> 8u) & 0x3u);
            if (out.sidModel[2] == PsidSidModel::Unknown) out.sidModel[2] = out.sidModel[0];
        }
    }
    if (!psidValidExtraSidAddressByte(out.secondSidAddress) ||
        !psidValidExtraSidAddressByte(out.thirdSidAddress) ||
        !psidValidExtraSidAddressByte(out.fourthSidAddress) ||
        !psidValidExtraSidAddressByte(out.fifthSidAddress)) {
        return PsidParseResult::BadSidAddress;
    }
    // Reject non-contiguous multi-SID metadata: an extra SID slot may only be
    // present if every lower slot is present. A header with e.g. a third SID but no
    // second produces a zero base for the missing slot; psidSidChipCount() would
    // still report the higher count, so the runtime would build a chip list with a
    // zero base and reject it downstream. Fail early and explicitly instead. This
    // only rejects malformed headers — valid contiguous stereo/3SID/5SID headers
    // are unaffected (v873 P1-5).
    if ((!psidSidAddressBytePresent(out.secondSidAddress) &&
         (psidSidAddressBytePresent(out.thirdSidAddress) ||
          psidSidAddressBytePresent(out.fourthSidAddress) ||
          psidSidAddressBytePresent(out.fifthSidAddress))) ||
        (!psidSidAddressBytePresent(out.thirdSidAddress) &&
         (psidSidAddressBytePresent(out.fourthSidAddress) ||
          psidSidAddressBytePresent(out.fifthSidAddress))) ||
        (!psidSidAddressBytePresent(out.fourthSidAddress) &&
         psidSidAddressBytePresent(out.fifthSidAddress))) {
        return PsidParseResult::BadSidAddress;
    }
    // Two SID chips at the same base is invalid/ambiguous (v873 audit item 2).
    if (!psidSidBasesUnique(out)) return PsidParseResult::DuplicateSidBase;
    // Multi-SID PSID v2NG metadata is accepted here. Runtime support is
    // handled by C64Platform::configurePsidSidBases(), so parser validation
    // must not silently reject valid stereo/3SID/5SID headers.
    // Flags bit 0 is "Compute! Sidplayer MUS data" for PSID — that payload is note
    // data, not 6502 code, so this real-C64 runtime cannot play it. Refuse honestly
    // rather than executing MUS bytes as code (v873 audit item 5, MUS half only; the
    // bit-1 "PSID-specific" reject is intentionally NOT done here — it would refuse
    // many tunes that currently play).
    if (!out.isRsid && (out.flags & 0x0001u) != 0u)
        return PsidParseResult::UnsupportedMusSpecific;

    // Normalized file class (item 1). Unknown here means a version invalid for the file
    // kind (e.g. RSID v1/v4), which the version gate above lets through — reject it.
    out.compatibility = psidResolveCompatibility(out.isRsid, out.version, out.flags);
    if (out.compatibility == PsidCompatibility::Unknown)
        return PsidParseResult::BadVersion;

    if (out.isRsid) {
        // RSID requires a real C64 environment. The PSID speed word is not valid RSID
        // metadata, so loadAddr/playAddr/rawSpeed must all be zero. Internally normalize
        // speed to all-ones so every subtune resolves as CIA-timed through
        // psidUsesCiaTimingForSong().
        if (out.loadAddr != 0u || out.playAddr != 0u || out.rawSpeed != 0u)
            return PsidParseResult::BadRsidHeader;
        out.normalizedSpeed = 0xFFFFFFFFu;
        out.speed = out.normalizedSpeed;
        // RSID_BASIC is a valid file class, but this runtime has no real BASIC startup
        // path yet. Reject valid BASIC tunes honestly at parse/load instead of loading a
        // pseudo-RSID that can never execute its BASIC bootstrap. If an RSID_BASIC header
        // provides a non-zero init address it is malformed before it is merely unsupported.
        if (out.compatibility == PsidCompatibility::RSID_BASIC) {
            if (out.initAddr != 0u) return PsidParseResult::BadRsidHeader;
            return PsidParseResult::UnsupportedRsidBasic;
        }
    }

    std::memcpy(out.name, img + 0x16u, 32u); out.name[32] = '\0';
    std::memcpy(out.author, img + 0x36u, 32u); out.author[32] = '\0';
    std::memcpy(out.released, img + 0x56u, 32u); out.released[32] = '\0';

    const uint8_t* data = img + out.dataOffset;
    uint32_t dlen = len - static_cast<uint32_t>(out.dataOffset);
    if (out.loadAddr == 0u) {
        if (dlen < 2u) return PsidParseResult::TooShort;
        out.effectiveLoadAddr = static_cast<uint16_t>(data[0] | (static_cast<uint16_t>(data[1]) << 8u));
        out.payload = data + 2u;
        out.payloadLen = dlen - 2u;
    } else {
        out.effectiveLoadAddr = out.loadAddr;
        out.payload = data;
        out.payloadLen = dlen;
    }
    // A header-only file (dataOffset == len, or only the embedded load address
    // and no code) is degenerate: there is nothing to load or execute. Reject it
    // cleanly instead of accepting it and letting init run uninitialised RAM.
    if (out.payloadLen == 0u) return PsidParseResult::TooShort;
    if (!psidRelocationRangeValid(out.startPage, out.pageLength, out.effectiveLoadAddr, out.payloadLen))
        return PsidParseResult::BadRelocationRange;
    if (out.initAddr == 0u) out.initAddr = out.effectiveLoadAddr;
    return PsidParseResult::OK;
}

inline void psidLoadIntoRam(const PsidHeader& hdr, uint8_t ram[65536]) noexcept {
    if (!hdr.payload || hdr.payloadLen == 0u) return;
    const uint32_t maxBytes = 65536u - static_cast<uint32_t>(hdr.effectiveLoadAddr);
    const uint32_t copyLen = std::min(hdr.payloadLen, maxBytes);
    std::memcpy(ram + hdr.effectiveLoadAddr, hdr.payload, copyLen);
}

} // namespace ArpSID
