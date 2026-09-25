#include "parameter_ids.h"
#include <cstdlib>
#include <iostream>
#include <cmath>

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}
int main() {
    using namespace ArpSID;
    require(kCanonicalFactoryPatchSlotCount == 180, "canonical factory slot count is 180");
    require(canonicalNormalizedBankSlotValue(149) != canonicalNormalizedBankSlotValue(127),
            "slot 149 must not alias slot 127");
    require(canonicalNormalizedBankSlotValue(179) != canonicalNormalizedBankSlotValue(127),
            "slot 179 must not alias slot 127");
    for (int slot : {0, 1, 79, 80, 119, 120, 127, 128, 149, 150, 179}) {
        const float n = canonicalNormalizedBankSlotValue(slot);
        require(canonicalFactorySlotFromNormalizedBankSlot(n) == slot, "factory slot encode/decode roundtrip");
        require(std::isfinite(n) && n >= 0.0f && n <= 1.0f, "normalized factory slot finite");
    }
    require(canonicalFactorySlotFromNormalizedBankSlot(-1.0f) == 0, "negative normalized clamps to 0");
    require(canonicalFactorySlotFromNormalizedBankSlot(2.0f) == 179, "overrange normalized clamps to 179");
    std::cout << "FactorySlotEncodingRoundtripV665Tests PASS\n";
    return 0;
}
