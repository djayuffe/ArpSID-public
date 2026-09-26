// Copyright (C) 2024-2026 Ulf Bertilsson
// v873: PSID extra-SID address bytes are version-gated per the HVSC/libsidplayfp
// PSID v2NG spec (max 3 SIDs):
//   v2: $7A/$7B reserved (mono)
//   v3: $7A = 2nd SID only
//   v4: $7A = 2nd SID, $7B = 3rd SID
// and there is no 4th/5th SID.
#include "arpsid/core/psid_header.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace ArpSID;

namespace {

void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "PsidMultiSidVersionSemanticsV873Tests FAIL: " << msg << "\n";
        std::exit(1);
    }
}

void poke16(std::vector<uint8_t>& v, size_t off, uint16_t val) {
    v[off] = uint8_t(val >> 8);
    v[off + 1] = uint8_t(val & 0xFF);
}

// Build a minimal valid PSID v2NG image with the given version, flags and $7A/$7B.
std::vector<uint8_t> makePsid(uint16_t version, uint8_t sid2Byte, uint8_t sid3Byte,
                              uint16_t flags = 0) {
    std::vector<uint8_t> v(0x7C + 8, 0);          // header + a little payload
    v[0] = 'P'; v[1] = 'S'; v[2] = 'I'; v[3] = 'D';
    poke16(v, 0x04, version);                      // version
    poke16(v, 0x06, 0x7C);                         // dataOffset
    poke16(v, 0x08, 0x1000);                       // loadAddr
    poke16(v, 0x0A, 0x1000);                       // initAddr
    poke16(v, 0x0C, 0x1003);                       // playAddr
    poke16(v, 0x0E, 1);                            // songs
    poke16(v, 0x10, 1);                            // startSong
    poke16(v, 0x76, flags);                        // flags word (clock/model nibbles)
    v[0x7A] = sid2Byte;                            // 2nd SID selector
    v[0x7B] = sid3Byte;                            // 3rd SID selector
    return v;
}

} // namespace

int main() {
    // 0x42 -> $D420 (valid 2nd SID), 0x44 -> $D440 (valid 3rd SID).
    // v2: both bytes present in the buffer but must be ignored (mono).
    {
        auto v = makePsid(2, 0x42, 0x44);
        PsidHeader h{};
        require(psidParse(v.data(), (uint32_t)v.size(), h) == PsidParseResult::OK, "v2 parses");
        require(h.secondSidAddress == 0 && h.thirdSidAddress == 0,
                "v2 ignores $7A/$7B (reserved) — not stereo");
        require(psidSidChipCount(h) == 1, "v2 is mono (1 SID)");
    }

    // v3: only $7A is read.
    {
        auto v = makePsid(3, 0x42, 0x44);
        PsidHeader h{};
        require(psidParse(v.data(), (uint32_t)v.size(), h) == PsidParseResult::OK, "v3 parses");
        require(h.secondSidAddress == 0x42, "v3 reads $7A as 2nd SID");
        require(h.thirdSidAddress == 0, "v3 ignores $7B (reserved)");
        require(psidSidChipCount(h) == 2, "v3 is stereo (2 SIDs)");
        require(psidSidBaseForChip(h, 1) == 0xD420u, "v3 2nd SID base = $D420");
    }

    // v4: both $7A and $7B are read.
    {
        auto v = makePsid(4, 0x42, 0x44);
        PsidHeader h{};
        require(psidParse(v.data(), (uint32_t)v.size(), h) == PsidParseResult::OK, "v4 parses");
        require(h.secondSidAddress == 0x42 && h.thirdSidAddress == 0x44, "v4 reads $7A+$7B");
        require(psidSidChipCount(h) == 3, "v4 is 3-SID");
        require(psidSidBaseForChip(h, 1) == 0xD420u && psidSidBaseForChip(h, 2) == 0xD440u,
                "v4 2nd/3rd SID bases = $D420/$D440");
        // The format has no 4th/5th SID; those bytes must never be populated.
        require(h.fourthSidAddress == 0 && h.fifthSidAddress == 0,
                "no 4th/5th SID (payload bytes are not SID selectors)");
    }

    // Songs > 32 reuse speed bit 31 (CIA), not a silent VBI default.
    {
        auto v = makePsid(2, 0, 0);
        poke16(v, 0x0E, 40);                       // 40 songs
        poke16(v, 0x10, 33);                       // startSong 33
        v[0x12] = 0x80;                            // speed bit 31 set (BE32 top byte)
        PsidHeader h{};
        require(psidParse(v.data(), (uint32_t)v.size(), h) == PsidParseResult::OK, "40-song v2 parses");
        require(psidUsesCiaTimingForSong(h, 33) == true, "song 33 follows speed bit 31 (CIA)");
        require(psidUsesCiaTimingForSong(h, 40) == true, "song 40 follows speed bit 31 (CIA)");
        v[0x12] = 0x00;                            // clear bit 31 -> VBI
        require(psidParse(v.data(), (uint32_t)v.size(), h) == PsidParseResult::OK, "reparse");
        require(psidUsesCiaTimingForSong(h, 40) == false, "song 40 with bit31 clear = VBI");
    }

    // Clock + SID model projection from the flags word.
    // flags bits: 2-3 clock, 4-5 SID1 model, 6-7 SID2 model, 8-9 SID3 model.
    {
        // clock = PAL (01<<2 = 0x04), SID1 = 6581 (01<<4 = 0x10).
        auto v = makePsid(2, 0, 0, 0x04 | 0x10);
        PsidHeader h{};
        require(psidParse(v.data(), (uint32_t)v.size(), h) == PsidParseResult::OK, "clock/model v2 parses");
        require(h.clock == PsidClock::PAL, "flags project PAL clock");
        require(h.sidModel[0] == PsidSidModel::MOS6581, "flags project SID1 = 6581");
    }
    {
        // clock = NTSC (10<<2 = 0x08), SID1 = 8580 (10<<4 = 0x20).
        auto v = makePsid(2, 0, 0, 0x08 | 0x20);
        PsidHeader h{};
        require(psidParse(v.data(), (uint32_t)v.size(), h) == PsidParseResult::OK, "clock/model v2 parses (2)");
        require(h.clock == PsidClock::NTSC, "flags project NTSC clock");
        require(h.sidModel[0] == PsidSidModel::MOS8580, "flags project SID1 = 8580");
    }
    {
        // v4, SID1 = 8580 (0x20), SID2 model unknown (bits 6-7 = 00) -> inherits 8580,
        // SID3 = 6581 (01<<8 = 0x100). $7A/$7B present.
        auto v = makePsid(4, 0x42, 0x44, 0x20 | 0x100);
        PsidHeader h{};
        require(psidParse(v.data(), (uint32_t)v.size(), h) == PsidParseResult::OK, "3-SID model header parses");
        require(h.sidModel[0] == PsidSidModel::MOS8580, "SID1 = 8580");
        require(h.sidModel[1] == PsidSidModel::MOS8580, "SID2 unknown model inherits SID1 (8580)");
        require(h.sidModel[2] == PsidSidModel::MOS6581, "SID3 = 6581 (explicit)");
    }

    // Parse policy: Strict rejects sloppy song metadata; Compatible clamps it.
    {
        auto v = makePsid(2, 0, 0);
        poke16(v, 0x0E, 0);   // songs = 0
        poke16(v, 0x10, 0);   // startSong = 0
        PsidHeader hs{};
        require(psidParse(v.data(), (uint32_t)v.size(), hs, PsidParsePolicy::StrictSpec)
                    == PsidParseResult::BadSongCount, "strict rejects songs==0");
        PsidHeader hc{};
        require(psidParse(v.data(), (uint32_t)v.size(), hc, PsidParsePolicy::SidTuneCompatible)
                    == PsidParseResult::OK, "compatible accepts songs==0");
        require(hc.songs == 1 && hc.startSong == 1, "compatible clamps songs/startSong to 1");
    }
    {
        auto v = makePsid(2, 0, 0);
        poke16(v, 0x0E, 5);    // songs = 5
        poke16(v, 0x10, 99);   // startSong = 99 (> songs)
        PsidHeader hs{};
        require(psidParse(v.data(), (uint32_t)v.size(), hs, PsidParsePolicy::StrictSpec)
                    == PsidParseResult::BadStartSong, "strict rejects startSong>songs");
        PsidHeader hc{};
        require(psidParse(v.data(), (uint32_t)v.size(), hc, PsidParsePolicy::SidTuneCompatible)
                    == PsidParseResult::OK, "compatible accepts startSong>songs");
        require(hc.startSong == 1 && hc.songs == 5, "compatible clamps out-of-range startSong to 1");
    }
    // Default policy is Strict (validators/pinned tests unaffected).
    {
        auto v = makePsid(2, 0, 0);
        poke16(v, 0x0E, 0);
        PsidHeader h{};
        require(psidParse(v.data(), (uint32_t)v.size(), h) == PsidParseResult::BadSongCount,
                "default policy is StrictSpec");
    }

    std::cout << "PsidMultiSidVersionSemanticsV873Tests PASS\n";
    return 0;
}
