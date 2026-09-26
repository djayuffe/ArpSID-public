// Copyright (C) 2024-2026 Ulf Bertilsson
// sid_runtime_restore_dual_engine_v815_tests.cpp
//
// P1-13 guard: a prepared state restore must restore the serialized primary SID
// envelope runtime into BOTH SID-backed engines from the post-swap applied root.
// The local argument passed to applyStateRootBySwap() is swapped-out old state
// after the handoff, so any root.patch.sid_runtime use after the swap is wrong.

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
    const std::string bitperfect = readText("include/arpsid/engines/bitperfect_engine.h");
    const std::string drsid = readText("include/arpsid/engines/drsid_engine.h");

    require(bitperfect.find("void restorePrimarySidEnvelopeRuntimeState(const SidSerializedState& in) noexcept") != std::string::npos,
            "BitPerfectEngine exposes SID runtime restore");
    require(drsid.find("void restorePrimarySidEnvelopeRuntimeState(const SidSerializedState& in) noexcept") != std::string::npos,
            "DrSidEngine exposes SID runtime restore");
    require(kernel.find("out.patch.sid_runtime = drs_()->serializePrimarySidEnvelopeRuntimeState();") != std::string::npos,
            "state capture can serialize DrSid SID runtime when DrSid is active");
    require(kernel.find("out.patch.sid_runtime = bpe_()->serializePrimarySidEnvelopeRuntimeState();") != std::string::npos,
            "state capture can serialize BitPerfect SID runtime otherwise");

    const std::string body = functionBody(kernel, "applyStateRootCanonical(SidStateRootV1& root");
    const std::size_t swap = body.find("runtimeModel_.applyStateRootBySwap(root)");
    require(swap != std::string::npos, "state root by-swap handoff exists");
    const std::string afterSwap = body.substr(swap);

    require(afterSwap.find("const SidStateRootV1& appliedRoot = runtimeModel_.stateRoot()") != std::string::npos,
            "post-swap applied root is rebound from runtime model");
    require(afterSwap.find("if (bpe_()) bpe_()->restorePrimarySidEnvelopeRuntimeState(appliedRoot.patch.sid_runtime);") != std::string::npos,
            "BitPerfect restore uses applied root SID runtime");
    require(afterSwap.find("if (drs_()) drs_()->restorePrimarySidEnvelopeRuntimeState(appliedRoot.patch.sid_runtime);") != std::string::npos,
            "DrSid restore uses applied root SID runtime");
    require(afterSwap.find("restorePrimarySidEnvelopeRuntimeState(root.patch.sid_runtime)") == std::string::npos,
            "no engine restore may use swapped-out local root SID runtime");

    std::cout << "SidRuntimeRestoreDualEngineV815Tests PASS\n";
    return 0;
}
