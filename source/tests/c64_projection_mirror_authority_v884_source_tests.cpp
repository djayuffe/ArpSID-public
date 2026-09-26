// Copyright (C) 2024-2026 Ulf Bertilsson
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

static std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::string dirnameOf(std::string path) {
    const std::size_t pos = path.find_last_of("/\\");
    return (pos == std::string::npos) ? std::string(".") : path.substr(0, pos);
}

static std::string readKernelSource() {
    const std::string testDir = dirnameOf(__FILE__);
    const std::vector<std::string> candidates = {
        "source/au3/ArpSIDDSPKernel.hpp",
        "../source/au3/ArpSIDDSPKernel.hpp",
        "../../source/au3/ArpSIDDSPKernel.hpp",
        testDir + "/../au3/ArpSIDDSPKernel.hpp",
        testDir + "/../../source/au3/ArpSIDDSPKernel.hpp",
    };
    for (const auto& c : candidates) {
        std::string s = readFile(c);
        if (!s.empty()) return s;
    }
    return {};
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
    const std::string kernel = readKernelSource();
    require(!kernel.empty(), "kernel source must be readable");

    require(contains(kernel, "v884 closure: do not advance the cosmetic mirror clock until this"),
            "v884 clock-consumption closure comment must be present");
    require(contains(kernel, "uint64_t cyclesDueThisBlock = 0u;\n        uint64_t cyclesThisBlock = 0u;\n        float mirrorFidelity = 1.0f;"),
            "cycles/fidelity must be initialized without advancing the clock");
    require(contains(kernel, "if (c64MirrorConsumedThisBlock) {\n            cyclesDueThisBlock = c64PlatformClock_.cyclesForNextHostBlock"),
            "cyclesForNextHostBlock must be called only inside consumed branch");
    require(!contains(kernel, "const uint64_t cyclesDueThisBlock = c64PlatformClock_.cyclesForNextHostBlock"),
            "old eager cyclesForNextHostBlock call before consumption decision must not remain");
    require(contains(kernel, "telemetryC64MirrorFidelity_.store(mirrorFidelity, std::memory_order_relaxed);"),
            "mirror fidelity must still be published after consumed/discard decision");
    require(contains(kernel, "c64RealtimeCycleDebt_ = 0;\n        c64MirrorObserverActive_.store(0u"),
            "cancelC64TelemetryDemandBlock_ must reset realtime cycle debt before closing observer");

    std::cout << "PASS: C64 projection mirror clock-consumption source closure\n";
    return 0;
}
