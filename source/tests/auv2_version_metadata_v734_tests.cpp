// Copyright (C) 2024-2026 Ulf Bertilsson
// Version coherence: VERSION.txt is the single source of truth. CMake
// project(VERSION), include/arpsid/version.h, the README title and the
// CHANGELOG must all carry the same plain MAJOR.MINOR.PATCH version, and the
// AU component version integer must use Apple's major<<16|minor<<8|patch
// encoding so hosts display the real version.
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static std::string readFile(const char* rel) {
    std::ifstream f(std::string(ARPSID_SOURCE_ROOT) + "/" + rel, std::ios::binary);
    if (!f) {
        std::cerr << "missing file: " << rel << "\n";
        std::abort();
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void require(bool cond, const std::string& msg) {
    if (!cond) {
        std::cerr << "FAIL: " << msg << "\n";
        std::abort();
    }
}

int main() {
    const std::string cmake = readFile("CMakeLists.txt");
    const std::string versionHeader = readFile("include/arpsid/version.h");
    const std::string versionTxt = readFile("VERSION.txt");
    const std::string readme = readFile("README.md");
    const std::string changelog = readFile("CHANGELOG.md");
    const std::string drumContext = readFile("include/arpsid/core/drum_context.h");

    std::smatch m;
    require(std::regex_match(versionTxt, m, std::regex(R"((\d+)\.(\d+)\.(\d+)\n?)")),
            "VERSION.txt must hold a plain MAJOR.MINOR.PATCH version");
    const std::string major = m[1], minor = m[2], patch = m[3];
    const std::string version = major + "." + minor + "." + patch;

    require(cmake.find("project(ArpSID VERSION " + version + " LANGUAGES CXX)") != std::string::npos,
            "CMake project(VERSION) must match VERSION.txt");
    require(cmake.find("${_arpsid_ver_major} * 65536 + ${_arpsid_ver_minor} * 256 + ${_arpsid_ver_patch}") !=
                std::string::npos,
            "AU version integer must use Apple's major<<16|minor<<8|patch encoding");

    require(versionHeader.find("#define ARPSID_PLUGIN_VERSION_MAJOR " + major + "\n") != std::string::npos,
            "version.h major must match VERSION.txt");
    require(versionHeader.find("#define ARPSID_PLUGIN_VERSION_MINOR " + minor + "\n") != std::string::npos,
            "version.h minor must match VERSION.txt");
    require(versionHeader.find("#define ARPSID_PLUGIN_VERSION_PATCH " + patch + "\n") != std::string::npos,
            "version.h patch must match VERSION.txt");
    require(versionHeader.find("#define ARPSID_PLUGIN_VERSION \"" + version + "\"") != std::string::npos,
            "version.h string must match VERSION.txt");
    require(versionHeader.find("ARPSID_BUILD_PASS") == std::string::npos,
            "version.h must not carry the retired build-pass identity");

    require(readme.rfind("# ArpSID " + version + "\n", 0) == 0,
            "README title must name the current version");
    require(changelog.find("## [" + version + "]") != std::string::npos,
            "CHANGELOG.md must have an entry for the current version");

    require(drumContext.find("overlap by design//") == std::string::npos,
            "drum_context.h must not contain the overlap-by-design comment merge artifact");
    require(drumContext.find("overlap by design.\n// The legacy DrSID projection block") != std::string::npos,
            "drum_context.h should split the overlap documentation across clean comment lines");

    std::cout << "Auv2VersionMetadataV734Tests PASS\n";
    return 0;
}
