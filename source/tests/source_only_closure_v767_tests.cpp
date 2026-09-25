#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

std::string readText(const char* relative) {
    std::ifstream stream(std::string(ARPSID_SOURCE_ROOT) + "/" + relative,
                         std::ios::binary);
    require(static_cast<bool>(stream), relative);
    return std::string(std::istreambuf_iterator<char>(stream),
                       std::istreambuf_iterator<char>());
}

void requireContains(const std::string& haystack,
                     const char* needle,
                     const char* message) {
    require(haystack.find(needle) != std::string::npos, message);
}

} // namespace


static std::string extractCurrentPass(const std::string& version) {
    const std::string prefix = "0.0.690-pass";
    const std::size_t passPos = version.find(prefix);
    require(passPos != std::string::npos, "VERSION.txt must carry a 0.0.690-passNNN identity");
    std::size_t digits = passPos + prefix.size();
    std::size_t end = digits;
    while (end < version.size() && version[end] >= '0' && version[end] <= '9') ++end;
    require(end > digits, "VERSION.txt pass identity must include digits");
    return version.substr(digits, end - digits);
}

int main() {
    const std::string cmake = readText("CMakeLists.txt");
    const std::string version = readText("VERSION.txt");
    const std::string pass = extractCurrentPass(version);
    const std::string versionHeader = readText("include/arpsid/version.h");
    const std::string readme = readText("README.md");
    const std::string status = readText("arpsid-fix-list-690.md");
    const std::string audit = readText("AUDIT-FIXES-0.0.686.md");
    const std::string closure = readText("RELEASE_SOURCE_ONLY_CLOSURE.md");

    requireContains(version, ("0.0.690-pass" + pass).c_str(),
                    "VERSION.txt must carry current pass identity");
    requireContains(versionHeader, ("#define ARPSID_BUILD_PASS " + pass).c_str(),
                    "version.h build pass must match current pass");
    require(readme.rfind("# ArpSID 0.0.690 pass" + pass, 0) == 0,
            "README must start with the current release entry");
    requireContains(status, ("# ArpSID 0.0.690 pass" + pass).c_str(),
                    "handoff status must be current source-only closure");
    requireContains(audit, "## Fix-order #20 — source-only release closure / macOS validation handoff",
                    "audit doc must include fix-order #20 closure");

    const char* guards[] = {
        "VstCocoaFactoryPresetRangeV759Tests",
        "DrumContextLegacyCanonicalSplitV760Tests",
        "SidcoreSingleAuthoritySurfaceV761Tests",
        "WrapperStateApplyPolicyCoreV762Tests",
        "DigiSplitProtocolSurfaceV763Tests",
        "ParameterDirtyFlushFallbackTelemetryV764Tests",
        "Auv2RampAnchorDropTelemetryV765Tests",
        "Post18ClosureStatusConsistencyV766Tests",
        "SourceOnlyClosureV767Tests",
        "RootBuildScriptReleaseV768Tests",
        "RootBuildScriptReleaseModesV769Tests",
        "MacosInstallStatusHandoffV770Tests",
        "RootBuildScriptAuvalModeV771Tests",
        "RootBuildScriptMacosClosureV772Tests",
        "RootBuildScriptMacosClosureLoggingV773Tests",
        "MacosBuildGreenStatusV774Tests",
    };
    for (const char* guard : guards) {
        requireContains(cmake, guard, "CMake must register every post-handoff guard");
        requireContains(status, guard, "handoff status must name every post-handoff guard");
    }

    requireContains(closure, "No AUv2/Logic runtime validation is claimed",
                    "closure doc must explicitly avoid false AUv2/Logic claims");
    requireContains(closure, "macOS validation", "closure doc must carry macOS handoff");
    requireContains(status, "Do not claim this sandbox ran AUv2/Logic validation",
                    "status must preserve the sandbox limitation");
    require(status.find("AU VALIDATION SUCCEEDED") == std::string::npos,
            "current status must not claim auval success");
    require(status.find("Logic validation succeeded") == std::string::npos,
            "current status must not claim Logic validation success");

    std::cout << "SourceOnlyClosureV767Tests PASS\n";
    return 0;
}
