#include <cstdlib>
#include <fstream>
#include <iostream>
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

static void require(bool cond, const char* msg) {
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
    const std::string drumContext = readFile("include/arpsid/core/drum_context.h");

    const std::string prefix = "0.0.690-pass";
    const std::size_t passPos = versionTxt.find(prefix);
    require(passPos != std::string::npos, "VERSION.txt must carry a 0.0.690-passNNN package identity");
    std::size_t digits = passPos + prefix.size();
    std::size_t end = digits;
    while (end < versionTxt.size() && versionTxt[end] >= '0' && versionTxt[end] <= '9') ++end;
    require(end > digits, "VERSION.txt pass identity must include digits");
    const std::string pass = versionTxt.substr(digits, end - digits);

    require(cmake.find("project(ArpSID VERSION 0.0.690 LANGUAGES CXX)") != std::string::npos,
            "CMake project version must match the AU bundle version");
    require(cmake.find("project(ArpSID VERSION 0.0.605") == std::string::npos,
            "CMake project version must not remain at stale 0.0.605");

    require(versionHeader.find("#define ARPSID_PLUGIN_VERSION_PATCH 690") != std::string::npos,
            "version.h patch component must match 0.0.690");
    require(versionHeader.find("#define ARPSID_PLUGIN_VERSION \"0.0.690\"") != std::string::npos,
            "version.h string must match 0.0.690");
    require(versionHeader.find("#define ARPSID_BUILD_PASS " + pass) != std::string::npos,
            "version.h build pass must match VERSION.txt");
    require(versionHeader.find("0.0.605") == std::string::npos,
            "version.h must not expose stale 0.0.605 metadata");

    require(readme.rfind("# ArpSID 0.0.690 pass" + pass, 0) == 0,
            "README must start with the current pass release note");
    require(readme.find("# ArpSID v0.0.605") == std::string::npos,
            "README must not carry stale v0.0.605 release notes (archived)");
    require(drumContext.find("overlap by design//") == std::string::npos,
            "drum_context.h must not contain the overlap-by-design comment merge artifact");
    require(drumContext.find("overlap by design.\n// The legacy DrSID projection block") != std::string::npos,
            "drum_context.h should split the overlap documentation across clean comment lines");

    std::cout << "Auv2VersionMetadataV734Tests PASS\n";
    return 0;
}
