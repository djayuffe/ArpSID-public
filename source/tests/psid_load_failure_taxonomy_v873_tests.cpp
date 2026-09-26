// Copyright (C) 2024-2026 Ulf Bertilsson
// v873 audit items 4 & 7: loadPsidParsed() (share one parse with the AU) and the
// PsidLoadFailure taxonomy / lastParseResult() diagnostics on C64Runtime.
#include "arpsid/core/c64_psid_runtime.h"
#include "arpsid/core/psid_header.h"

#include <cstdint>
#include <iostream>
#include <vector>

using namespace ArpSID;
using namespace ArpSID::C64;

namespace {

int failures = 0;
void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "PsidLoadFailureTaxonomyV873Tests FAIL: " << msg << "\n"; ++failures; }
}
void be16(std::vector<uint8_t>& v, size_t off, uint16_t x) { v[off]=uint8_t(x>>8); v[off+1]=uint8_t(x&0xFF); }

// Minimal, spec-valid PSID v2: load $1000, four payload bytes.
std::vector<uint8_t> minimalPsid() {
    std::vector<uint8_t> v(0x7C + 4, 0);
    v[0]='P'; v[1]='S'; v[2]='I'; v[3]='D';
    be16(v,0x04,2); be16(v,0x06,0x7C); be16(v,0x08,0x1000); be16(v,0x0A,0x1000);
    be16(v,0x0C,0x1003); be16(v,0x0E,1); be16(v,0x10,1);
    v[0x7C+2]=0x60; // an RTS somewhere in the payload
    return v;
}

} // namespace

int main() {
    const auto bytes = minimalPsid();

    // ---- item 4: loadPsid() and loadPsidParsed() agree ----
    C64Runtime viaBytes; viaBytes.reset(true);
    require(viaBytes.loadPsid(bytes.data(), bytes.size()), "loadPsid(bytes) succeeds on a valid PSID");
    require(viaBytes.lastLoadFailure() == PsidLoadFailure::None, "success -> lastLoadFailure None");
    require(viaBytes.lastParseResult() == PsidParseResult::OK, "success -> lastParseResult OK");

    ArpSID::PsidHeader parsed{};  // low-level parser header (distinct from C64::PsidHeader)
    require(psidParse(bytes.data(), (uint32_t)bytes.size(), parsed,
                      PsidParsePolicy::SidTuneCompatible) == PsidParseResult::OK,
            "standalone psidParse OK");
    C64Runtime viaParsed; viaParsed.reset(true);
    require(viaParsed.loadPsidParsed(parsed), "loadPsidParsed(header) succeeds without re-parsing bytes");
    require(viaParsed.lastLoadFailure() == PsidLoadFailure::None, "loadPsidParsed success -> None");

    // The two load paths must publish an identical runtime image header.
    const auto& a = viaBytes.image().header;
    const auto& b = viaParsed.image().header;
    require(a.valid && b.valid, "both images valid");
    require(a.loadAddress == b.loadAddress && a.initAddress == b.initAddress &&
            a.playAddress == b.playAddress && a.songs == b.songs &&
            a.sidChipCount == b.sidChipCount && a.sidBase[0] == b.sidBase[0],
            "loadPsid and loadPsidParsed publish identical header");

    // ---- item 7: unparseable bytes report a parse-classified failure ----
    // v893 refresh: the v891 relocation/taxonomy work made the parse-failure
    // mapping granular. An 8-byte junk buffer is honestly classified as
    // ParseTooShort (it cannot even hold a header), and a full-size buffer
    // with a wrong magic is BadMagic; the coarse ParseFailed bucket remains
    // only for unmapped parse results.
    const uint8_t junk[8] = {'N','O','T','S','I','D','!','!'};
    C64Runtime bad; bad.reset(true);
    require(!bad.loadPsid(junk, sizeof(junk)), "loadPsid rejects a non-PSID buffer");
    require(bad.lastLoadFailure() == PsidLoadFailure::ParseTooShort,
            "reject (8-byte junk) -> lastLoadFailure ParseTooShort");
    require(bad.lastParseResult() != PsidParseResult::OK, "reject -> lastParseResult not OK");
    uint8_t junkFull[0x7C + 4] = {};
    junkFull[0]='N'; junkFull[1]='O'; junkFull[2]='T'; junkFull[3]='!';
    C64Runtime bad2; bad2.reset(true);
    require(!bad2.loadPsid(junkFull, sizeof(junkFull)), "loadPsid rejects a wrong-magic buffer");
    require(bad2.lastLoadFailure() == PsidLoadFailure::BadMagic,
            "reject (wrong magic) -> lastLoadFailure BadMagic");

    std::cout << (failures==0 ? "PsidLoadFailureTaxonomyV873Tests PASS\n"
                              : "PsidLoadFailureTaxonomyV873Tests FAIL\n");
    return failures==0 ? 0 : 1;
}
