#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "../au3/ArpSIDComponentFlavor.h"

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static std::string readFile(const char* rel) {
    std::ifstream in(std::string(ARPSID_SOURCE_ROOT) + "/" + rel, std::ios::binary);
    if (!in) {
        std::cerr << "unable to open " << rel << "\n";
        std::exit(2);
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

static bool contains(const std::string& s, const std::string& needle) {
    return s.find(needle) != std::string::npos;
}

int main() {
    static_assert(ArpSID::kComponentFlavorCount == 5, "ComponentFlavor must expose all five product flavors");
    static_assert(ArpSID::componentFlavorFromRaw(4) == ArpSID::ComponentFlavor::C64SidPlayer,
                  "raw flavor 4 must decode to C64SidPlayer");
    static_assert(ArpSID::componentFlavorIndex(ArpSID::ComponentFlavor::C64SidPlayer) == 4u,
                  "C64SidPlayer must own cache/index slot 4");
    static_assert(ArpSID::componentFlavorFromRaw(5) == ArpSID::ComponentFlavor::Hybrid,
                  "invalid raw flavor falls back to Hybrid, not Sid808");

    const std::string flavor = readFile("source/au3/ArpSIDComponentFlavor.h");
    const std::string auv2 = readFile("source/au2/ArpSIDAUv2Component.mm");
    const std::string au3 = readFile("source/au3/ArpSIDAudioUnit.mm");
    const std::string vc = readFile("source/au3/ArpSIDViewController.mm");

    require(contains(flavor, "inline constexpr int kComponentFlavorCount = 5"),
            "canonical flavor count must be five");
    require(contains(flavor, "componentFlavorFromRaw") && contains(flavor, "ComponentFlavor::C64SidPlayer"),
            "canonical raw flavor decoder must include C64SidPlayer");
    require(!contains(vc, "std::clamp((int)raw,0,3)"),
            "GUI flavor sync must not clamp flavor 4 down to Sid808");
    require(contains(vc, "flavor=ArpSID::componentFlavorFromRaw((int)raw);"),
            "GUI flavor sync must use canonical five-flavor decoder");

    require(contains(auv2, "std::array<CachedFactoryPresetStorage, ArpSID::kComponentFlavorCount> storageByFlavor"),
            "AUv2 factory preset cache must allocate one slot per flavor");
    require(contains(auv2, "const size_t flavorIndex = ArpSID::componentFlavorIndex(flavor);"),
            "AUv2 factory preset cache must index through canonical flavor helper");
    require(!contains(auv2, "std::array<CachedFactoryPresetStorage, 4> storageByFlavor") &&
            !contains(auv2, "std::clamp((int)flavor, 0, 3)"),
            "AUv2 factory cache must not use legacy four-slot flavor clamp");

    require(contains(auv2, "case ArpSID::ComponentFlavor::C64SidPlayer:\n            return normalized == 0;"),
            "AUv2 factory-slot policy must explicitly isolate C64SidPlayer to startup/default slot");
    require(contains(auv2, "if (flavor == ArpSID::ComponentFlavor::C64SidPlayer) return 0;"),
            "AUv2 C64SidPlayer startup slot must be explicit");
    require(contains(auv2, "case ArpSID::ComponentFlavor::C64SidPlayer:\n            ArpSID::sidSetStateRootParamValue(root, (int)ArpSID::kParamSynthModeEnable, 0.0f);"),
            "AUv2 C64SidPlayer flavor policy must force synth off");
    require(contains(auv2, "ArpSID::sidSetStateRootParamValue(root, (int)ArpSID::kParamDrSidEnable, 0.0f);"),
            "AUv2 C64SidPlayer flavor policy must force DrSID off");
    require(contains(auv2, "DrumMachine flavor must not destructively clear a restored"),
            "AUv2 DrumMachine flavor policy must preserve restored SEQ transport");
    require(contains(auv2, "SID808 flavor preserves SeqEnable as drum-pattern transport"),
            "AUv2 SID808 flavor policy must preserve SEQ transport");

    require(contains(au3, "DrumMachine flavor must not destructively clear a restored"),
            "AUv3 DrumMachine flavor policy must preserve restored SEQ transport");
    require(contains(au3, "SID808 flavor preserves SeqEnable as drum-pattern transport"),
            "AUv3 SID808 flavor policy must preserve SEQ transport");
    require(contains(au3, "case ArpSID::ComponentFlavor::C64SidPlayer:\n            return normalized == 0;"),
            "AUv3 factory-slot policy must explicitly isolate C64SidPlayer to startup/default slot");
    require(contains(au3, "if (flavor == ArpSID::ComponentFlavor::C64SidPlayer) {\n        ArpSIDAppendOrderedFactorySlotIfAllowed(orderedSlots, seenSlots, flavor, 0);\n        return orderedSlots;\n    }"),
            "AUv3 C64SidPlayer preset order must be explicit and single-slot");
    require(contains(vc, "if (flavor == ArpSID::ComponentFlavor::C64SidPlayer) {\n        ArpSIDAppendOrderedFactorySlotIfAllowedUI"),
            "GUI factory picker order must explicitly handle C64SidPlayer");
    require(contains(vc, "case ArpSID::ComponentFlavor::C64SidPlayer: return slot == 0;"),
            "GUI visible-slot policy must explicitly isolate C64SidPlayer");
    require(contains(vc, "ArpSID::componentFlavorIsC64SidPlayer(flavor)") &&
            contains(vc, "Dedicated C64 SID Player flavor"),
            "GUI flavor sync must treat C64SidPlayer as a distinct locked product flavor");

    std::cout << "PASS: five-flavor C64SidPlayer GUI/AUv2/AUv3 factory policy is explicit and indexed safely\n";
    return 0;
}
