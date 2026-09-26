// Copyright (C) 2024-2026 Ulf Bertilsson
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

static void require(bool ok, const std::string& msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}
static std::string readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    require(static_cast<bool>(f), "cannot open " + p);
    std::ostringstream ss; ss << f.rdbuf();
    return ss.str();
}

int main() {
    const std::string root = ARPSID_SOURCE_DIR;
    const std::string md = readFile(root + "/docs/audit/AUDIT_FINDINGS_CLOSURE_MATRIX.md");
    const std::string json = readFile(root + "/docs/audit/AUDIT_FINDINGS_CLOSURE_MATRIX.json");
    const std::string kernel = readFile(root + "/source/au3/ArpSIDDSPKernel.hpp");
    const std::string digi = readFile(root + "/include/arpsid/engines/digi_sampler_engine.h");

    for (const auto& id : {"P1-01","P1-02","P1-03","P1-04","P1-05","P1-06","P1-07","P1-08","P2-09","P2-10"}) {
        require(md.find(id) != std::string::npos, std::string("matrix contains ") + id);
        require(json.find(id) != std::string::npos, std::string("json contains ") + id);
    }

    require(md.find("NOT_PROVABLE_IN_CONTAINER") != std::string::npos,
            "matrix honestly marks macOS proof boundary");
    require(md.find("FIXED_WITH_SCOPE") != std::string::npos &&
            md.find("C64CycleExactClosureV741Tests") != std::string::npos,
            "matrix records scoped CIA/VIC/SID-read cycle closure");

    require(kernel.find("target == ArpSID::GUI::KitEngineTarget::DrSID ||\n                       componentFlavor_ == ArpSID::ComponentFlavor::DrumMachine") == std::string::npos,
            "DrumMachine no longer forces DrSID");
    require(kernel.find("constexpr std::uint8_t slotIndex = 0u;") != std::string::npos &&
            kernel.find("oneShot.activeSlotCount = 1u;") != std::string::npos,
            "kernel KIT Digi one-shot uses dense slot0 projection");
    require(digi.find("singleSlotProj.activeSlotCount = static_cast<std::uint8_t>(std::min<int>(") != std::string::npos,
            "sampler direct nonzero Digi trigger keeps active range consistent");

    std::cout << "AuditClosureMatrixV646Tests PASS\n";
    return 0;
}
