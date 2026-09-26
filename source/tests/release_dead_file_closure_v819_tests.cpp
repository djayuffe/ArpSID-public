// Copyright (C) 2024-2026 Ulf Bertilsson
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

namespace fs = std::filesystem;

static std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

static void requireContains(const std::string& body, const std::string& needle, const char* msg) {
    if (body.find(needle) == std::string::npos) {
        std::cerr << "FAIL: " << msg << "\nmissing: " << needle << "\n";
        std::exit(1);
    }
}

int main() {
    const fs::path root = fs::path(ARPSID_SOURCE_ROOT);

    const std::vector<std::string> removedRootArtifacts = {
        "eurodance.asm",
        "eurodance.prg",
        "eurodance.sid",
        "techno.asm",
        "pulsegrid.prg",
        "pulsegrid.sid",
        "tune.bin",
        "make_sid.py"
    };
    for (const auto& rel : removedRootArtifacts) {
        require(!fs::exists(root / rel), "dead root C64 scratch/demo artifact must not ship in source closure");
    }

    const std::string manifest = readFile(root / "RELEASE_CONTENTS.sha256");
    require(!manifest.empty(), "release manifest exists");
    for (const auto& rel : removedRootArtifacts) {
        require(manifest.find("./" + rel) == std::string::npos,
                "release manifest must not list removed dead root artifact");
    }

    const std::string patching = readFile(root / "PATCHING.md");
    const std::string todo = readFile(root / "TODO.md");
    const std::string finalClosure = readFile(root / "RELEASE_FINAL_SOURCE_CLOSURE.md");

    requireContains(patching, "V819", "PATCHING.md documents V819 dead-file/factory-audit closure");
    requireContains(todo, "Source-side closure: COMPLETE", "TODO.md marks source-side closure complete");
    requireContains(finalClosure, "FactoryBankAudioAuditV687Tests", "final closure documents factory-bank audit status");
    requireContains(finalClosure, "bounded closure", "final closure documents bounded default closure mode");
    requireContains(finalClosure, "ARPSID_FACTORY_BANK_FULL_AUDIO_AUDIT=1", "final closure documents full factory-bank soak command");

    std::cout << "ReleaseDeadFileClosureV819Tests PASS\n";
    return 0;
}
