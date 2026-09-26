// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/c64_psid_runtime.h"
#include "arpsid/core/psid_header.h"
#include <cstdint>
#include <iostream>
#include <vector>

#define REQUIRE_TRUE(expr, msg) do { if (!(expr)) { std::cerr << "FAIL: " << msg << "\n"; return 1; } } while (0)
static void be16(std::vector<uint8_t>& v, size_t off, uint16_t x) { v[off]=uint8_t(x>>8); v[off+1]=uint8_t(x); }
static std::vector<uint8_t> psid(uint8_t startPage, uint8_t pageLen, uint16_t load=0x1000) {
    std::vector<uint8_t> v(0x7C,0); v[0]='P';v[1]='S';v[2]='I';v[3]='D';
    be16(v,0x04,2); be16(v,0x06,0x7C); be16(v,0x08,load); be16(v,0x0A,load); be16(v,0x0C,load+3); be16(v,0x0E,1); be16(v,0x10,1);
    v[0x78]=startPage; v[0x79]=pageLen; v.push_back(0x60); v.push_back(0x60); v.push_back(0x60); return v;
}
int main(){
    using namespace ArpSID;
    PsidHeader h{};
    auto good = psid(0x20,1);
    REQUIRE_TRUE(psidParse(good.data(), (uint32_t)good.size(), h, PsidParsePolicy::SidTuneCompatible)==PsidParseResult::OK, "non-overlapping relocation page accepted");
    auto overlap = psid(0x10,1);
    REQUIRE_TRUE(psidParse(overlap.data(), (uint32_t)overlap.size(), h, PsidParsePolicy::SidTuneCompatible)==PsidParseResult::BadRelocationRange, "relocation page overlapping payload rejected");
    auto low = psid(0x02,1);
    REQUIRE_TRUE(psidParse(low.data(), (uint32_t)low.size(), h, PsidParsePolicy::SidTuneCompatible)==PsidParseResult::BadRelocationRange, "relocation page below $0400 rejected");
    ArpSID::C64::C64Runtime rt;
    auto basic = std::vector<uint8_t>(0x7C,0); basic[0]='R'; basic[1]='S'; basic[2]='I'; basic[3]='D';
    be16(basic,0x04,2); be16(basic,0x06,0x7C); be16(basic,0x0E,1); be16(basic,0x10,1); be16(basic,0x76,0x0002); basic.push_back(0); basic.push_back(0x10); basic.push_back(0x60);
    REQUIRE_TRUE(!rt.loadPsid(basic.data(), basic.size()), "RSID_BASIC load rejected");
    REQUIRE_TRUE(rt.lastLoadFailure()==ArpSID::C64::PsidLoadFailure::UnsupportedRsidBasic, "load failure exposes UnsupportedRsidBasic");
    return 0;
}
