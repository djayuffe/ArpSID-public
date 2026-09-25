#include "arpsid/patchbank/factory_drsid_kits.h"
#include <cstdlib>
#include <iostream>
#include <set>

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::abort(); }
}
int main() {
    using namespace ArpSID;
    std::set<std::uint32_t> hashes;
    for (int slot = 80; slot <= 119; ++slot) {
        const auto payload = makeFactoryDrSidKitPayloadForSlot(slot);
        require(factoryDrSidPayloadIsWellFormed(payload), "DrSID factory payload is well-formed");
        require(Drsid::programIsWellFormed(payload.program), "DrSID microprogram is well-formed");
        hashes.insert(payload.behaviorHash);
    }
    require(hashes.size() >= 32, "DrSID 80..119 has many behaviorally distinct microprograms");

    require(makeFactoryDrSidKitPayloadForSlot(80).behaviorHash != makeFactoryDrSidKitPayloadForSlot(88).behaviorHash,
            "DrSID 80 and 88 are behaviorally distinct");
    require(makeFactoryDrSidKitPayloadForSlot(88).behaviorHash != makeFactoryDrSidKitPayloadForSlot(96).behaviorHash,
            "DrSID 88 and 96 are behaviorally distinct");
    require(makeFactoryDrSidKitPayloadForSlot(96).behaviorHash != makeFactoryDrSidKitPayloadForSlot(104).behaviorHash,
            "DrSID 96 and 104 are behaviorally distinct");
    require(makeFactoryDrSidKitPayloadForSlot(104).behaviorHash != makeFactoryDrSidKitPayloadForSlot(112).behaviorHash,
            "DrSID 104 and 112 are behaviorally distinct");

    std::cout << "FactoryDrSidMicroprogramPayloadV692Tests PASS\n";
    return 0;
}
