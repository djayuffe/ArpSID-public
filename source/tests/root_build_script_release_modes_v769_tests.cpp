// Copyright (C) 2024-2026 Ulf Bertilsson
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static std::string readFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static bool contains(const std::string& s, const char* needle) {
    return s.find(needle) != std::string::npos;
}

static void requireContains(const std::string& s, const char* needle, const char* why) {
    if (!contains(s, needle)) {
        std::cerr << "build.sh missing: " << why << " [" << needle << "]\n";
        std::exit(1);
    }
}

int main() {
    const std::filesystem::path root = ARPSID_SOURCE_ROOT;
    const std::string s = readFile(root / "build.sh");

    requireContains(s, "--release-check", "release-check command-line mode required by ReleaseClosureV701");
    requireContains(s, "--package-release", "package-release command-line mode required by ReleasePackagingV702");
    requireContains(s, "PACKAGE_RELEASE=1; RELEASE_CHECK=1", "package-release must force release-check validation");
    requireContains(s, "arpsid_release_closure_suite", "release-check must build the CMake release closure suite");
    requireContains(s, "ReleaseClosureV701Tests", "release-check must run the release closure guard");
    requireContains(s, "ReleasePackagingV702Tests", "release-check must run the packaging guard");
    requireContains(s, "TabNoScaffoldV631Tests", "release-check must include no-scaffold guard");
    requireContains(s, "TabTooltipCoverageV586Tests", "release-check must include tooltip guard");
    requireContains(s, "BankUITextAndTooltipV677Tests", "release-check must include bank UI/tooltip guard");
    requireContains(s, "NoDead127BankSlotDecodeV694Tests", "release-check must include stale 127-slot guard");
    requireContains(s, "FactoryPayloadFinalDeepGuardV695Tests", "release-check must include factory payload deep guard");
    requireContains(s, "scripts/package_release.sh", "package-release must delegate clean zipping to canonical script");
    requireContains(s, "-DARPSID_BUILD_AUV2=", "AUv2 install path must configure the AUv2 target");
    requireContains(s, "--target arpsid_auv2", "AUv2 install path must build the bundle target before install");
    requireContains(s, "ArpSID.component not found under ${BUILD_DIR} after building arpsid_auv2", "AUv2 install path must fail explicitly if bundle is missing");

    std::cout << "RootBuildScriptReleaseModesV769Tests PASS\n";
    return 0;
}
