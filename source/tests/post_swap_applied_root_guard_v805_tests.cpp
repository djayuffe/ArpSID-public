// Copyright (C) 2024-2026 Ulf Bertilsson
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static std::string readText(const char* rel) {
    std::ifstream stream(std::string(ARPSID_SOURCE_ROOT) + "/" + rel, std::ios::binary);
    if (!stream) { std::cerr << "FAIL: missing " << rel << '\n'; std::exit(1); }
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << '\n'; std::exit(1); }
}

static std::string functionBody(const std::string& text, const std::string& signature) {
    const std::size_t sig = text.find(signature);
    require(sig != std::string::npos, "applyStateRootCanonical signature not found");
    const std::size_t open = text.find('{', sig);
    require(open != std::string::npos, "applyStateRootCanonical body open not found");
    int depth = 0;
    for (std::size_t i = open; i < text.size(); ++i) {
        if (text[i] == '{') ++depth;
        else if (text[i] == '}') {
            --depth;
            if (depth == 0) return text.substr(open, i - open + 1);
        }
    }
    require(false, "applyStateRootCanonical body close not found");
    return {};
}

int main() {
    const std::string kernel = readText("source/au3/ArpSIDDSPKernel.hpp");
    const std::string body = functionBody(kernel, "applyStateRootCanonical(SidStateRootV1& root");
    const std::string swapCall = "runtimeModel_.applyStateRootBySwap(root)";
    const std::size_t swap = body.find(swapCall);
    require(swap != std::string::npos, "state root apply must use by-swap handoff");
    const std::string afterSwap = body.substr(swap + swapCall.size());

    require(afterSwap.find("const SidStateRootV1& appliedRoot = runtimeModel_.stateRoot()") != std::string::npos,
            "post-swap code must bind appliedRoot to runtimeModel_.stateRoot()");
    require(afterSwap.find("restorePrimarySidEnvelopeRuntimeState(appliedRoot.patch.sid_runtime)") != std::string::npos,
            "SID envelope runtime restore must use appliedRoot after swap");
    require(afterSwap.find("restorePrimarySidEnvelopeRuntimeState(root.patch.sid_runtime)") == std::string::npos,
            "SID envelope runtime restore must not use swapped-out local root");
    require(afterSwap.find("sidStateRootParamValue(runtimeModel_.stateRoot()") == std::string::npos,
            "render-side apply must not use semantic sidStateRootParamValue reads after swap");
    require(afterSwap.find("sidStateRootParamValueFromHydratedValuesRT(appliedRoot") != std::string::npos,
            "render-side apply must use hydrated RT parameter reads from appliedRoot");

    std::cout << "PostSwapAppliedRootGuardV805Tests PASS\n";
    return 0;
}
