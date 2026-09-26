// Copyright (C) 2024-2026 Ulf Bertilsson
// v873 loader-contract validation: extra-SID $DE00-$DFE0 acceptance (item 8),
// RSID speed!=0 rejection (item 3), PSID MUS rejection (item 5-MUS), RSID BASIC
// init!=0 rejection (item 6) and explicit UnsupportedRsidBasic (item 7).
#include "arpsid/core/psid_header.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace ArpSID;

namespace {

int failures = 0;
void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "PsidContractValidationV873Tests FAIL: " << msg << "\n"; ++failures; }
}
void be16(std::vector<uint8_t>& v, size_t off, uint16_t x) { v[off]=uint8_t(x>>8); v[off+1]=uint8_t(x&0xFF); }

// A v3 PSID (loadAddr $1000) with the given 2nd-SID selector byte at $7A.
std::vector<uint8_t> psidV3(uint8_t sid2Byte, uint16_t flags = 0) {
    std::vector<uint8_t> v(0x7C + 4, 0);
    v[0]='P'; v[1]='S'; v[2]='I'; v[3]='D';
    be16(v,0x04,3); be16(v,0x06,0x7C); be16(v,0x08,0x1000); be16(v,0x0A,0x1000);
    be16(v,0x0C,0x1003); be16(v,0x0E,1); be16(v,0x10,1); be16(v,0x76,flags);
    v[0x7A]=sid2Byte;
    return v;
}

// A minimal RSID (loadAddr/playAddr = 0; load address embedded in payload).
std::vector<uint8_t> rsid(uint16_t version, uint16_t flags, uint16_t initAddr, uint32_t speed) {
    std::vector<uint8_t> v(0x7C, 0);
    v[0]='R'; v[1]='S'; v[2]='I'; v[3]='D';
    be16(v,0x04,version); be16(v,0x06,0x7C); be16(v,0x08,0x0000); be16(v,0x0A,initAddr);
    be16(v,0x0C,0x0000); be16(v,0x0E,1); be16(v,0x10,1); be16(v,0x76,flags);
    v[0x12]=uint8_t(speed>>24); v[0x13]=uint8_t(speed>>16); v[0x14]=uint8_t(speed>>8); v[0x15]=uint8_t(speed);
    // payload: embedded load address $1000 + a couple code bytes
    v.push_back(0x00); v.push_back(0x10); v.push_back(0x60); v.push_back(0x60);
    return v;
}

PsidParseResult parse(const std::vector<uint8_t>& v) {
    PsidHeader h{};
    return psidParse(v.data(), (uint32_t)v.size(), h, PsidParsePolicy::SidTuneCompatible);
}

} // namespace

int main() {
    // ---- item 8: extra-SID base range ----
    {
        auto v = psidV3(0xE0);  // 0xE0 -> $DE00 (valid)
        PsidHeader h{};
        require(psidParse(v.data(),(uint32_t)v.size(),h,PsidParsePolicy::SidTuneCompatible)==PsidParseResult::OK,
                "2nd SID at $DE00 (selector $E0) is accepted");
        require(psidSidBaseForChip(h,1)==0xDE00u, "$DE00 base normalized");
    }
    require(parse(psidV3(0xFE))==PsidParseResult::OK,       "2nd SID at $DFE0 (selector $FE) accepted");
    require(parse(psidV3(0x42))==PsidParseResult::OK,       "2nd SID at $D420 (selector $42) accepted");
    require(parse(psidV3(0xD8))==PsidParseResult::BadSidAddress, "2nd SID at $DD80 (selector $D8) rejected");
    require(parse(psidV3(0xDE))==PsidParseResult::BadSidAddress, "2nd SID at $DDE0 (selector $DE) rejected");
    require(parse(psidV3(0x80))==PsidParseResult::BadSidAddress, "2nd SID at $D800 (selector $80) rejected");
    require(parse(psidV3(0x43))==PsidParseResult::BadSidAddress, "odd selector $43 rejected");

    // ---- item 5 (MUS half): PSID with MUS flag (bit 0) rejected ----
    require(parse(psidV3(0x00, 0x0001))==PsidParseResult::UnsupportedMusSpecific,
            "PSID MUS-data flag (bit 0) rejected as UnsupportedMusSpecific");
    // bit 1 (PSID-specific) intentionally NOT rejected — must still parse.
    require(parse(psidV3(0x00, 0x0002))==PsidParseResult::OK,
            "PSID-specific flag (bit 1) does NOT reject (would refuse valid tunes)");

    // ---- item 3: RSID speed must be zero ----
    require(parse(rsid(2, 0, 0x1000, 0))==PsidParseResult::OK,          "RSID speed 0 accepted");
    require(parse(rsid(2, 0, 0x1000, 1))==PsidParseResult::BadRsidHeader,      "RSID speed bit0 rejected");
    require(parse(rsid(2, 0, 0x1000, 0x80000000u))==PsidParseResult::BadRsidHeader, "RSID speed bit31 rejected");

    // ---- items 6/7: RSID BASIC is valid metadata but unsupported until BASIC startup exists ----
    require(parse(rsid(2, 0x0002, 0x1000, 0))==PsidParseResult::BadRsidHeader,
            "RSID_BASIC with non-zero initAddr rejected as malformed");
    require(parse(rsid(2, 0x0002, 0x0000, 0))==PsidParseResult::UnsupportedRsidBasic,
            "valid RSID_BASIC returns UnsupportedRsidBasic until BASIC startup exists");

    // ---- item 2: duplicate SID base rejection ----
    {
        auto v = psidV3(0x42);   // v3, 2nd SID at $D420
        v[0x04]=0; v[0x05]=4;    // -> version 4 (also reads $7B as 3rd SID)
        v[0x7B]=0x42;            // 3rd SID = $D420 too => duplicate
        require(parse(v)==PsidParseResult::DuplicateSidBase, "duplicate SID base ($D420 twice) rejected");
        v[0x7B]=0x44;            // 3rd SID = $D440 => distinct
        require(parse(v)==PsidParseResult::OK, "distinct SID bases ($D420,$D440) accepted");
    }

    std::cout << (failures==0 ? "PsidContractValidationV873Tests PASS\n"
                              : "PsidContractValidationV873Tests FAIL\n");
    return failures==0 ? 0 : 1;
}
