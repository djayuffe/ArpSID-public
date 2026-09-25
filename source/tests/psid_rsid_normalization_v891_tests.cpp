#include "arpsid/core/psid_header.h"
#include <cstdint>
#include <iostream>
#include <vector>

#define REQUIRE_TRUE(expr, msg) do { if (!(expr)) { std::cerr << "FAIL: " << msg << "\n"; return 1; } } while (0)
static void be16(std::vector<uint8_t>& v, size_t off, uint16_t x) { v[off]=uint8_t(x>>8); v[off+1]=uint8_t(x); }
static void be32(std::vector<uint8_t>& v, size_t off, uint32_t x) { v[off]=uint8_t(x>>24); v[off+1]=uint8_t(x>>16); v[off+2]=uint8_t(x>>8); v[off+3]=uint8_t(x); }
static std::vector<uint8_t> rsid(uint16_t flags, uint16_t init, uint32_t speed) {
    std::vector<uint8_t> v(0x7C,0); v[0]='R';v[1]='S';v[2]='I';v[3]='D';
    be16(v,0x04,2); be16(v,0x06,0x7C); be16(v,0x08,0); be16(v,0x0A,init); be16(v,0x0C,0); be16(v,0x0E,1); be16(v,0x10,1); be32(v,0x12,speed); be16(v,0x76,flags);
    v.push_back(0x00); v.push_back(0x10); v.push_back(0x60);
    return v;
}
int main(){
    using namespace ArpSID;
    PsidHeader h{};
    auto ok = rsid(0,0x1000,0);
    REQUIRE_TRUE(psidParse(ok.data(), (uint32_t)ok.size(), h, PsidParsePolicy::SidTuneCompatible)==PsidParseResult::OK, "ordinary RSID parses");
    REQUIRE_TRUE(h.rawSpeed == 0u, "RSID rawSpeed preserves file zero");
    REQUIRE_TRUE(h.normalizedSpeed == 0xFFFFFFFFu && h.speed == 0xFFFFFFFFu, "RSID speed normalized to all CIA bits");
    REQUIRE_TRUE(psidUsesCiaTimingForSong(h,1) && psidUsesCiaTimingForSong(h,32) && psidUsesCiaTimingForSong(h,99), "RSID uses CIA timing for every subtune");
    auto badSpeed = rsid(0,0x1000,1);
    REQUIRE_TRUE(psidParse(badSpeed.data(), (uint32_t)badSpeed.size(), h, PsidParsePolicy::SidTuneCompatible)==PsidParseResult::BadRsidHeader, "RSID non-zero raw speed rejected");
    auto basicBadInit = rsid(0x0002,0x1000,0);
    REQUIRE_TRUE(psidParse(basicBadInit.data(), (uint32_t)basicBadInit.size(), h, PsidParsePolicy::SidTuneCompatible)==PsidParseResult::BadRsidHeader, "RSID_BASIC initAddr != 0 rejected");
    auto basicUnsupported = rsid(0x0002,0,0);
    REQUIRE_TRUE(psidParse(basicUnsupported.data(), (uint32_t)basicUnsupported.size(), h, PsidParsePolicy::SidTuneCompatible)==PsidParseResult::UnsupportedRsidBasic, "valid RSID_BASIC returns UnsupportedRsidBasic until BASIC startup exists");
    return 0;
}
