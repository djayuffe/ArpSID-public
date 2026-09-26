// Copyright (C) 2024-2026 Ulf Bertilsson
// realtime_source_lint_v817_tests.cpp
//
// Pass380 V817 source contract lint. This intentionally overlaps the focused
// V804/V805/V806/V814 guards, but keeps the broad realtime/mutation invariants
// in one place so future refactors have a single always-on tripwire.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

static std::string readText(const char* rel) {
    std::ifstream stream(std::string(ARPSID_SOURCE_ROOT) + "/" + rel, std::ios::binary);
    if (!stream) {
        std::cerr << "FAIL: missing " << rel << "\n";
        std::exit(1);
    }
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

static std::string bodyOf(const std::string& text, const std::string& signature) {
    const std::size_t sig = text.find(signature);
    require(sig != std::string::npos, "function signature found");
    const std::size_t open = text.find('{', sig);
    require(open != std::string::npos, "function body open found");
    int depth = 0;
    for (std::size_t i = open; i < text.size(); ++i) {
        if (text[i] == '{') ++depth;
        else if (text[i] == '}') {
            --depth;
            if (depth == 0) return text.substr(open, i - open + 1);
        }
    }
    require(false, "function body close found");
    return {};
}

static std::string afterFirst(const std::string& text, const std::string& marker) {
    const std::size_t pos = text.find(marker);
    require(pos != std::string::npos, "marker found");
    return text.substr(pos + marker.size());
}

static bool containsAny(const std::string& text, const std::vector<const char*>& needles) {
    for (const char* n : needles) {
        if (text.find(n) != std::string::npos) return true;
    }
    return false;
}

int main() {
    const std::string kernel = readText("source/au3/ArpSIDDSPKernel.hpp");
    const std::string platform = readText("include/arpsid/core/c64_platform.h");
    const std::string c64Runtime = readText("include/arpsid/core/c64_psid_runtime.h");
    const std::string model = readText("include/arpsid/core/sid_runtime_model.h");
    const std::string todo = readText("TODO.md");
    const std::string patching = readText("PATCHING.md");

    const std::string rtApply = bodyOf(kernel, "applyPreparedStateRootRT_(SidStateRootV1& preparedRoot) noexcept");
    require(!containsAny(rtApply, {
        "sidCanonicalizeStateRootForApply",
        "preloadSid808FactorySlotForScheduledRestoreNonRealtime_",
        "sidEnsureSemanticParameterEntries",
        "sidHydrateParameterValuesFromSemanticEntries",
        "ensureParameterCapacity"
    }), "RT prepared-root apply helper must not call non-RT/canonicalize/hydrate/ensure helpers");
    require(rtApply.find("applyStateRootCanonical(preparedRoot") != std::string::npos,
            "RT prepared-root helper delegates only to canonical render apply entry");

    const std::string canonical = bodyOf(kernel, "applyStateRootCanonical(SidStateRootV1& root");
    const std::string afterSwap = afterFirst(canonical, "runtimeModel_.applyStateRootBySwap(root)");
    require(afterSwap.find("root.patch.sid_runtime") == std::string::npos,
            "post-swap code must not restore from swapped-out local root.patch.sid_runtime");
    require(afterSwap.find("sidStateRootParamValue(runtimeModel_.stateRoot()") == std::string::npos,
            "post-swap render apply must not use semantic parameter reads");
    require(afterSwap.find("sidStateRootParamValueFromHydratedValuesRT(appliedRoot") != std::string::npos,
            "post-swap render apply must use hydrated applied-root reads");

    require(kernel.find("c64BridgeRollbackPlatformScratch_") == std::string::npos,
            "DSP kernel must not reintroduce full C64Platform rollback scratch");
    require(kernel.find("C64Platform platformSnapshot") == std::string::npos,
            "DSP kernel transaction snapshots must not embed full C64Platform");
    require(kernel.find("beginRenderTransaction()") != std::string::npos,
            "render bridge transactions must begin the C64Runtime transaction");
    require(c64Runtime.find("beginRenderMutationJournal()") != std::string::npos,
            "C64Runtime transaction must begin the mutation journal");
    require(c64Runtime.find("rollbackRenderMutationJournal()") != std::string::npos,
            "C64Runtime rollback must use mutation journal rollback");
    require(c64Runtime.find("commitRenderMutationJournal()") != std::string::npos,
            "C64Runtime commit must commit the mutation journal");

    require(platform.find("struct RenderMutationJournal final") != std::string::npos,
            "C64Platform owns the render mutation journal");
    require(platform.find("std::array<DirtyByteEntry, kMaxDirtyRam>") != std::string::npos,
            "RAM journal storage remains bounded/fixed-size");
    require(platform.find("std::array<DirtyByteEntry, kMaxDirtyColor>") != std::string::npos,
            "ColorRAM journal storage remains bounded/fixed-size");
    require(platform.find("bool beginRenderMutationJournal() noexcept") != std::string::npos,
            "journal begin API remains noexcept");
    require(platform.find("bool rollbackRenderMutationJournal() noexcept") != std::string::npos,
            "journal rollback API remains noexcept");
    require(platform.find("void commitRenderMutationJournal() noexcept") != std::string::npos,
            "journal commit API remains noexcept");

    const std::string swapBody = bodyOf(model, "void applyStateRootBySwap(SidStateRootV1& inOut) noexcept");
    require(!containsAny(swapBody, {
        "sidCanonicalizeStateRootForApply",
        "sidEnsureSemanticParameterEntries",
        "sidHydrateParameterValuesFromSemanticEntries",
        "ensureParameterCapacity",
        "syncVariantPresentationMirrors_();"
    }), "runtime-model by-swap apply must remain RT-only");

    require(todo.find("P2-05/P2-06/P2-07") != std::string::npos,
            "TODO tracks the V817 source-lint closure bucket");
    require(patching.find("V817") != std::string::npos,
            "PATCHING documents V817 source-lint guard");

    std::cout << "RealtimeSourceLintV817Tests PASS\n";
    return 0;
}
