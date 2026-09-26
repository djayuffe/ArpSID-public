// Copyright (C) 2024-2026 Ulf Bertilsson
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <cctype>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static std::string readText(const char* rel) {
    std::ifstream stream(std::string(ARPSID_SOURCE_ROOT) + "/" + rel,
                         std::ios::binary);
    if (!stream) {
        std::cerr << "FAIL: missing " << rel << '\n';
        std::exit(1);
    }
    return std::string(std::istreambuf_iterator<char>(stream),
                       std::istreambuf_iterator<char>());
}

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << '\n';
        std::exit(1);
    }
}

static void requireContains(const std::string& haystack,
                            const char* needle,
                            const char* msg) {
    require(haystack.find(needle) != std::string::npos, msg);
}

static std::string currentPass(const std::string& version) {
    const std::string prefix = "0.0.690-pass";
    const std::size_t pos = version.find(prefix);
    require(pos != std::string::npos, "VERSION.txt must carry 0.0.690-passNNN");
    std::size_t begin = pos + prefix.size();
    std::size_t end = begin;
    while (end < version.size() && std::isdigit(static_cast<unsigned char>(version[end]))) ++end;
    require(end > begin, "VERSION.txt pass identity must include digits");
    return version.substr(begin, end - begin);
}

static std::string currentVersionStem(const std::string& pass) {
    return std::string("0.0.690-pass") + pass;
}

int main() {
    const std::string version = readText("VERSION.txt");
    const std::string pass = currentPass(version);
    const std::string versionHeader = readText("include/arpsid/version.h");
    const std::string readme = readText("README.md");
    const std::string audit = readText("AUDIT-FIXES-0.0.686.md");
    const std::string status = readText("arpsid-fix-list-690.md");
    const std::string cmake = readText("CMakeLists.txt");
    const std::string finalDoc = readText("RELEASE_FINAL_SOURCE_CLOSURE.md");
    const std::string build = readText("build.sh");

    const std::string buildPassNeedle = std::string("#define ARPSID_BUILD_PASS ") + pass;
    const std::string readmeHeading = std::string("# ArpSID 0.0.690 pass") + pass;
    const std::string sourceFolderNeedle = std::string("currently **") + currentVersionStem(pass) + "**";

    requireContains(versionHeader, buildPassNeedle.c_str(),
                    "version.h must match VERSION.txt pass identity");
    require(readme.rfind(readmeHeading, 0) == 0,
            "README must start with current pass final closure entry");
    requireContains(readme, sourceFolderNeedle.c_str(),
                    "README current source-folder note must match current pass");

    requireContains(cmake, "AbsoluteP0ClosureV797Tests", "P0 closure guard must be registered");
    requireContains(cmake, "AbsoluteP1ClosureV798Tests", "P1 closure guard must be registered");
    requireContains(cmake, "AbsoluteP2ClosureV799Tests", "P2 closure guard must be registered");
    requireContains(cmake, "StaleVersionGuardSweepV800Tests", "stale pass sweep guard must be registered");
    requireContains(cmake, "FinalSourceClosureV801Tests", "final closure guard must be registered");

    requireContains(audit, "Source-level P0/P1/P2 closure is guarded",
                    "audit log must state guarded P0/P1/P2 source closure");
    requireContains(status, "External release validation is still intentionally separate",
                    "status must keep external validation separate");
    requireContains(finalDoc, "source-level P0/P1/P2 closure: PASS",
                    "final closure doc must mark source P0/P1/P2 PASS");
    requireContains(finalDoc, "auval: PENDING until Mac log proves success",
                    "final closure doc must keep auval pending until Mac proof");
    requireContains(finalDoc, "Logic runtime: PENDING until Mac runtime test proves success",
                    "final closure doc must keep Logic pending until Mac proof");
    requireContains(finalDoc, "VST3 SDK/toolchain validation: PENDING until built with the SDK",
                    "final closure doc must keep VST3 SDK validation pending");
    requireContains(finalDoc, "notarization: PENDING until Apple notarization log proves success",
                    "final closure doc must keep notarization pending");
    require(finalDoc.find("AU VALIDATION SUCCEEDED") == std::string::npos,
            "final source doc must not claim auval success");
    require(finalDoc.find("Logic runtime: PASS") == std::string::npos,
            "final source doc must not claim Logic runtime pass");
    requireContains(build, "--macos-closure", "build.sh must expose macOS closure mode");
    requireContains(build, "--closure-log-dir", "build.sh must expose closure log directory option");

    std::cout << "FinalSourceClosureV801Tests PASS\n";
    return 0;
}
