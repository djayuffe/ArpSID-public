// sid808_strict_au_slot_policy_v861_tests.cpp
//
// Dedicated SID808 AU instances may only hold/load canonical SID808 factory
// slots (120..149). Accepting any drum-authored slot lets restored DrSID/DIGI
// roots put the bridge identity, loaded kit, GUI, and replacement render path
// out of sync.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static std::string readText(const char* rel) {
    std::ifstream f(std::string(ARPSID_SOURCE_ROOT) + "/" + rel, std::ios::binary);
    if (!f) { std::cerr << "FAIL: missing " << rel << '\n'; std::exit(1); }
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << '\n'; std::exit(1); }
}

int main() {
    const std::string au3 = readText("source/au3/ArpSIDAudioUnit.mm");
    const std::string vc = readText("source/au3/ArpSIDViewController.mm");
    const std::string auv2 = readText("source/au2/ArpSIDAUv2Component.mm");
    const std::string kernel = readText("source/au3/ArpSIDDSPKernel.hpp");
    const std::string bridge = readText("include/arpsid/engines/drum_engine_host_bridge.h");

    require(au3.find("case ArpSID::ComponentFlavor::Sid808:\n            return ArpSID::isSid808FactorySlot((int)normalized);") != std::string::npos,
            "AUv3 audio-unit slot resolver is SID808-range strict");
    require(vc.find("case ArpSID::ComponentFlavor::Sid808:\n            return ArpSIDFactorySlotIsSid808Type(normalized);") != std::string::npos,
            "AUv3 view/controller allowed-state resolver is SID808-range strict");
    require(auv2.find("case ArpSID::ComponentFlavor::Sid808:\n            return ArpSID::isSid808FactorySlot((int)normalized);") != std::string::npos,
            "AUv2 component slot resolver is SID808-range strict");
    require(auv2.find("const NSInteger normalized = auv2FactorySlotAllowedForFlavor(flavor, requested)") != std::string::npos,
            "AUv2 pinned preset cache resolves raw preset numbers through flavor policy");
    require(auv2.find("const NSInteger safeNumber = auv2FactorySlotAllowedForFlavor(flavor, requestedNumber)") != std::string::npos,
            "AUv2 PresentPreset writes resolve through flavor policy before apply");
    require(kernel.find("forceParam(kParamBankSlot, ArpSID::canonicalNormalizedBankSlotValue(120));") != std::string::npos,
            "kernel migrates non-SID808 state roots to SID808 startup slot");
    require(kernel.find("return ArpSID::isSid808FactorySlot(slot) ? slot : 120;") != std::string::npos,
            "kernel restore preload maps non-SID808 roots to slot 120 for SID808 flavor");
    require(bridge.find("activatePreparedSid808SlotRealtime") != std::string::npos &&
            kernel.find("applyPreparedSid808BridgeSlotRT_(slotInt)") != std::string::npos,
            "prepared SID808 kit can be activated at render block boundary");

    std::cout << "Sid808StrictAuSlotPolicyV861Tests PASS\n";
    return 0;
}
