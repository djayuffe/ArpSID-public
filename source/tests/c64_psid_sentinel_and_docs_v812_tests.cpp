// Copyright (C) 2024-2026 Ulf Bertilsson
// c64_psid_sentinel_and_docs_v812_tests.cpp
//
// P1/P2 guard: C64Psid remains a telemetry sentinel, not a render resolver mode,
// and the ownership/exactness docs for the pass380 follow-up exist.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << '\n'; std::exit(1); }
}

static std::string readText(const char* rel) {
    std::ifstream in(std::string(ARPSID_SOURCE_ROOT) + "/" + rel, std::ios::binary);
    require(in.good(), "could open file");
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

static std::string between(const std::string& s, const std::string& a, const std::string& b) {
    const auto begin = s.find(a);
    require(begin != std::string::npos, "begin marker found");
    const auto end = s.find(b, begin);
    require(end != std::string::npos, "end marker found");
    return s.substr(begin, end - begin);
}

int main() {
    const std::string model = readText("include/arpsid/core/sid_runtime_model.h");
    const std::string kernel = readText("source/au3/ArpSIDDSPKernel.hpp");
    const std::string ownership = readText("OWNERSHIP_MAP.md");
    const std::string journal = readText("REALTIME_ROLLBACK_JOURNAL.md");
    const std::string exactness = readText("C64_EXACTNESS_BOUNDARIES.md");

    require(model.find("C64Psid = 3 is a TELEMETRY SENTINEL ONLY") != std::string::npos,
            "C64Psid enum is documented as telemetry sentinel only");
    const std::string resolver = between(model,
        "inline SidRuntimeRenderMode sidResolveRenderModeFromLiveParams",
        "// Returns true when exactly one render mode flag is active");
    require(resolver.find("C64Psid") == std::string::npos,
            "render-mode resolver must not return C64Psid");
    require(kernel.find("mode = SidRuntimeRenderMode::C64Psid") != std::string::npos,
            "telemetry path may set C64Psid sentinel explicitly");
    require(kernel.find("component flavor, not a render mode value") != std::string::npos,
            "render/DIGI gate documents C64SidPlayer as flavor, not mode");

    require(ownership.find("Render / audio thread owned") != std::string::npos &&
            ownership.find("Non-RT producer side owned") != std::string::npos,
            "ownership map documents render vs producer ownership");
    require(journal.find("bounded mutation journal") != std::string::npos &&
            journal.find("no heap allocation") != std::string::npos,
            "rollback journal design documents bounded/no-heap plan");
    require(exactness.find("Strict RSID") != std::string::npos &&
            exactness.find("PSID `playAddress == 0`") != std::string::npos &&
            exactness.find("SID `$D41D`") != std::string::npos,
            "C64 exactness boundary doc covers strict RSID, PSID play-zero and D41D");

    std::cout << "C64PsidSentinelAndDocsV812Tests PASS\n";
    return 0;
}
