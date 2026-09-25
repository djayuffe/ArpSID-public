// sid808_restore_flavor_integration_v816_tests.cpp
//
// P1-14 guard: SID-808 state restore must combine three authorities correctly:
// producer-side non-RT preload, post-swap applied-root slot decode, and component
// flavor policy. DrSID/DrumMachine must not accidentally load SID-808 factory kits.

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

static std::string functionBody(const std::string& src, const std::string& sig) {
    const std::size_t at = src.find(sig);
    require(at != std::string::npos, "signature present");
    const std::size_t open = src.find('{', at);
    require(open != std::string::npos, "body open present");
    int depth = 0;
    for (std::size_t i = open; i < src.size(); ++i) {
        if (src[i] == '{') ++depth;
        else if (src[i] == '}') {
            if (--depth == 0) return src.substr(open, i - open + 1);
        }
    }
    require(false, "body close present");
    return {};
}

int main() {
    const std::string kernel = readText("source/au3/ArpSIDDSPKernel.hpp");
    const std::string todo = readText("TODO.md");
    const std::string patching = readText("PATCHING.md");

    const std::string prepareBody = functionBody(kernel, "void prepareStateRootForApplyNonRealtime_(SidStateRootV1& dst)");
    require(prepareBody.find("sidCanonicalizeStateRootForApply(dst)") != std::string::npos,
            "producer preparation canonicalizes the state root");
    require(prepareBody.find("preloadSid808FactorySlotForScheduledRestoreNonRealtime_(dst)") != std::string::npos,
            "producer preparation preloads SID-808 slot off render thread");

    const std::string applyBody = functionBody(kernel, "applyStateRootCanonical(SidStateRootV1& root");
    const std::size_t swap = applyBody.find("runtimeModel_.applyStateRootBySwap(root)");
    require(swap != std::string::npos, "state root by-swap handoff exists");
    const std::string afterSwap = applyBody.substr(swap);

    require(afterSwap.find("const float appliedBankSlot = ArpSID::sidStateRootParamValueFromHydratedValuesRT(appliedRoot, kParamBankSlot);") != std::string::npos,
            "SID-808 slot is decoded from the post-swap applied hydrated root");
    require(afterSwap.find("const int decodedSlotInt = ArpSID::canonicalFactorySlotFromNormalizedBankSlot(appliedBankSlot);") != std::string::npos,
            "SID-808 slot uses canonical bank-slot normalization");
    require(afterSwap.find("componentFlavor_ == ArpSID::ComponentFlavor::Sid808") != std::string::npos,
            "SID-808 bridge load is gated by Sid808 component flavor");
    require(afterSwap.find("ArpSID::isSid808FactorySlot(slotInt)") != std::string::npos,
            "SID-808 bridge load is gated by SID-808 factory slot range");
    require(afterSwap.find("preloadedSlot == slotInt") != std::string::npos,
            "render apply consumes a matching non-RT preload instead of queueing duplicate work");
    require(afterSwap.find("applyPreparedSid808BridgeSlotRT_(slotInt)") != std::string::npos,
            "render apply activates a prepared SID-808 bridge kit at the block boundary");
    require(afterSwap.find("const int slotInt = (componentFlavor_ == ArpSID::ComponentFlavor::Sid808") != std::string::npos,
            "SID-808 restore migrates non-SID808 drum roots to startup slot 120");
    require(afterSwap.find("DrSID/DrumMachine production audio has one authority only") != std::string::npos,
            "DrSID/DrumMachine factory slots are deliberately not loaded into SID-808 bridge");
    require(afterSwap.find("(void)enforceComponentFlavorPolicy_();") != std::string::npos,
            "component flavor policy is enforced during restore projection");
    require(afterSwap.find("runtimeExecutionOwner_->projectStateToBackends(true);") != std::string::npos,
            "restore projects the applied state to backends after flavor policy");

    require(todo.find("P1-13") != std::string::npos && todo.find("P1-14") != std::string::npos,
            "TODO tracks P1-13/P1-14 completion state");
    require(patching.find("V816") != std::string::npos,
            "PATCHING documents V816 integration guard");

    std::cout << "Sid808RestoreFlavorIntegrationV816Tests PASS\n";
    return 0;
}
