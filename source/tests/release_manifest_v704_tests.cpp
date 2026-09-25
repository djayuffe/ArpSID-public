// release_manifest_v704_tests.cpp
//
// Pass130 guard for release handoff integrity. The package step must emit a
// per-file SHA-256 manifest inside the release root and print the archive hash
// after zipping, so downstream macOS validation can prove exact source content.

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
    const std::string pack = readText(root + "/scripts/package_release.sh");
    const std::string cmake = readText(root + "/CMakeLists.txt");

    contains(pack, "RELEASE_CONTENTS.sha256", "package emits per-file release content manifest");
    contains(pack, "find . -type f", "manifest scans release payload files");
    contains(pack, "! -name 'RELEASE_CONTENTS.sha256'", "manifest excludes itself");
    contains(pack, "sort -z", "manifest order is stable for reproducible handoff");
    contains(pack, "sha256sum", "package supports GNU sha256sum");
    contains(pack, "shasum -a 256", "package supports macOS shasum fallback");
    contains(pack, "sha256sum \"${PACKAGE_OUT}\"", "package prints final archive hash when sha256sum exists");
    contains(cmake, "ReleaseManifestV704Tests", "CMake registers release manifest guard");
    contains(cmake, "arpsid_release_manifest_v704_tests", "release suite builds manifest guard target");

    std::cout << "ReleaseManifestV704Tests PASS\n";
    return 0;
}
