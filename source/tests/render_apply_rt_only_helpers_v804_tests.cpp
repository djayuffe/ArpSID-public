// render_apply_rt_only_helpers_v804_tests.cpp
//
// Guard for pass380 / P0 #5: the render-drained state-root apply path must not
// call helpers whose contract says Non-RT / may allocate. The behavioral
// allocation trap (v750) catches warmed allocations; this source contract guard
// catches the split-brain where applyStateRootBySwap calls wrappers documented
// as Non-RT even if a warmed test happens to pass.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

static std::string readFile(const char* rel) {
    const std::string path = std::string(ARPSID_SOURCE_ROOT) + "/" + rel;
    std::ifstream in(path, std::ios::binary);
    require(in.good(), "could open source file");
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static std::string between(const std::string& s, const std::string& a, const std::string& b) {
    const auto begin = s.find(a);
    require(begin != std::string::npos, "begin marker found");
    const auto end = s.find(b, begin);
    require(end != std::string::npos, "end marker found");
    return s.substr(begin, end - begin);
}

int main() {
    const std::string model = readFile("include/arpsid/core/sid_runtime_model.h");
    const std::string presentation = readFile("include/arpsid/core/sid_runtime_state_root_presentation.h");
    const std::string applyBody = between(model,
        "void applyStateRootBySwap(SidStateRootV1& inOut) noexcept",
        "void exportStateRootTo(SidStateRootV1& out) const");
    const std::string rebuildBody = between(model,
        "void rebuildParameterRegisterImage_(SidRegisterImage& out) const noexcept",
        "SidStateRootV1 state_root_{}");

    require(presentation.find("sidStateRootParamValueFromHydratedValuesRT") != std::string::npos,
            "RT-only hydrated value reader exists");
    require(presentation.find("sidSetHydratedStateRootParamValueRT") != std::string::npos,
            "RT-only hydrated value writer exists");
    require(model.find("syncVariantPresentationMirrorsRT_") != std::string::npos,
            "render apply has RT-only variant mirror sync helper");

    require(applyBody.find("ensureParameterCapacity") == std::string::npos,
            "applyStateRootBySwap does not call ensureParameterCapacity");
    require(applyBody.find("syncVariantPresentationMirrors_();") == std::string::npos,
            "applyStateRootBySwap does not call Non-RT semantic mirror sync");
    require(applyBody.find("sidEnsureSemanticParameterEntries") == std::string::npos,
            "applyStateRootBySwap does not call sidEnsureSemanticParameterEntries");
    require(applyBody.find("sidHydrateParameterValuesFromSemanticEntries") == std::string::npos,
            "applyStateRootBySwap does not call sidHydrateParameterValuesFromSemanticEntries");
    require(applyBody.find("sidStateRootParamValue(state_root_") == std::string::npos,
            "applyStateRootBySwap does not read semantic entries through sidStateRootParamValue");
    require(applyBody.find("syncVariantPresentationMirrorsRT_();") != std::string::npos,
            "applyStateRootBySwap uses RT-only mirror sync");
    require(applyBody.find("sidStateRootParamValueFromHydratedValuesRT(state_root_, kParamSidRegD417)") != std::string::npos,
            "applyStateRootBySwap derives D417 from hydrated values");
    require(rebuildBody.find("sidStateRootParamValueFromHydratedValuesRT(state_root_, pid)") != std::string::npos,
            "register-image rebuild uses RT-only hydrated values");

    std::cout << "RenderApplyRTOnlyHelpersV804Tests PASS\n";
    return 0;
}
