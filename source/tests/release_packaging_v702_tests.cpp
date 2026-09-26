// Copyright (C) 2024-2026 Ulf Bertilsson
// release_packaging_v702_tests.cpp
//
// Pass127 guard for the final release path. It keeps package-release support
// honest so future source drops do not ship build caches, stale CMake roots or
// undocumented packaging entrypoints.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef ARPSID_SOURCE_DIR
#error "ARPSID_SOURCE_DIR must be defined"
#endif

namespace {
std::string readText(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        std::cerr << "FAIL: could not open " << path << "\n";
        std::abort();
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::abort();
    }
}
void contains(const std::string& haystack, const char* needle, const char* msg) {
    require(haystack.find(needle) != std::string::npos, msg);
}
}

int main() {
    const std::string root = ARPSID_SOURCE_DIR;
    const std::string build = readText(root + "/build.sh");
    const std::string pack = readText(root + "/scripts/package_release.sh");
    const std::string wrapper = readText(root + "/scripts/install_auv2_component.sh");
    const std::string cmake = readText(root + "/CMakeLists.txt");

    contains(build, "--package-release", "build.sh exposes package-release mode");
    contains(build, "PACKAGE_RELEASE=1; RELEASE_CHECK=1", "package-release forces release-check validation");
    contains(build, "scripts/package_release.sh", "build.sh delegates clean zipping to package script");
    contains(pack, "--exclude '/build/'", "package excludes build directory");
    contains(pack, "--exclude '/dist/'", "package excludes generated distribution artifacts");
    contains(pack, "--exclude '/release-logs/'", "package excludes generated closure logs");
    contains(pack, "--exclude '/CMakeCache.txt'", "package excludes stale CMakeCache.txt");
    contains(pack, "--exclude '/.git/'", "package excludes git metadata");
    contains(pack, "--exclude '.claude/'", "rsync path excludes internal Claude metadata at any depth");
    contains(pack, "--exclude='.claude'", "tar fallback excludes internal Claude metadata directory");
    contains(pack, "--exclude='.claude/*'", "tar fallback excludes internal Claude metadata contents");
    contains(pack, "--exclude '.DS_Store'", "rsync path excludes Finder metadata at any depth");
    contains(pack, "--exclude='*/.DS_Store'", "tar fallback excludes nested Finder metadata");
    contains(pack, "--exclude 'CMakeCache.txt'", "rsync path excludes nested stale CMake cache files");
    contains(pack, "--exclude='*/CMakeCache.txt'", "tar fallback excludes nested stale CMake cache files");
    contains(pack, "--exclude '/*.zip'", "package excludes root release zips");
    contains(pack, "--exclude '/*.patch'", "package excludes root patch files");
    contains(pack, "--exclude '/RELEASE_MANIFEST_PASS*.sha256'", "rsync path excludes stale historical release manifests");
    contains(pack, "--exclude '/PASS*.md'", "rsync path excludes historical pass notes from clean release payload");
    contains(pack, "--exclude '/RELEASE_PASS*.md'", "rsync path excludes stale root release-pass notes from clean release payload");
    contains(pack, "--exclude '/RELEASE_NOTES_PASS*.md'", "rsync path excludes historical release-note pass files from clean release payload");
    contains(pack, "--exclude '/ArpSID_pass*_REPORT.md'", "rsync path excludes transient pass reports from clean release payload");
    contains(pack, "--exclude '/docs/PASS*.md'", "rsync path excludes historical docs pass notes from clean release payload");
    contains(pack, "--exclude '/HANDOFF.md'", "rsync path excludes handoff notes from clean release payload");
    contains(pack, "--exclude='./RELEASE_MANIFEST_PASS*.sha256'", "tar fallback excludes stale historical release manifests");
    contains(pack, "--exclude='./dist/*'", "tar fallback excludes generated distribution artifacts");
    contains(pack, "--exclude='./release-logs/*'", "tar fallback excludes generated closure logs");
    contains(pack, "--exclude='./PASS*.md'", "tar fallback excludes historical pass notes from clean release payload");
    contains(pack, "--exclude='./RELEASE_PASS*.md'", "tar fallback excludes stale root release-pass notes from clean release payload");
    contains(pack, "--exclude='./RELEASE_NOTES_PASS*.md'", "tar fallback excludes historical release-note pass files from clean release payload");
    contains(pack, "--exclude='./ArpSID_pass*_REPORT.md'", "tar fallback excludes transient pass reports from clean release payload");
    contains(pack, "--exclude='./docs/PASS*.md'", "tar fallback excludes historical docs pass notes from clean release payload");
    contains(pack, "--exclude='./HANDOFF.md'", "tar fallback excludes handoff notes from clean release payload");
    contains(pack, "RELEASE_NAME", "package script supports explicit release root naming");
    contains(pack, "PACKAGE_OUT", "package script supports explicit output path");
    contains(pack, "PACKAGE_TMP_ROOT", "package staging supports writable temporary roots");
    contains(wrapper, "macos/install_auv2_component.sh", "root install wrapper delegates to canonical installer");
    contains(cmake, "ReleasePackagingV702Tests", "CMake registers packaging release guard");
    contains(cmake, "arpsid_release_packaging_v702_tests", "release suite builds packaging guard target");

    std::cout << "ReleasePackagingV702Tests PASS\n";
    return 0;
}
