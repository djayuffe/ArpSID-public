#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

namespace {

std::string readText(const char* relative) {
    std::ifstream stream(std::string(ARPSID_SOURCE_ROOT) + "/" + relative,
                         std::ios::binary);
    if (!stream) {
        std::cerr << "FAIL: missing " << relative << '\n';
        std::exit(1);
    }
    return std::string(std::istreambuf_iterator<char>(stream),
                       std::istreambuf_iterator<char>());
}

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void requireContains(const std::string& haystack,
                     const char* needle,
                     const char* message) {
    require(haystack.find(needle) != std::string::npos, message);
}

} // namespace

int main() {
    const std::string readme = readText("README.md");
    const std::string version = readText("VERSION.txt");
    const std::string versionHeader = readText("include/arpsid/version.h");
    const std::string status = readText("arpsid-fix-list-690.md");

    const std::string prefix = "0.0.690-pass";
    const std::size_t passPos = version.find(prefix);
    require(passPos != std::string::npos,
            "VERSION.txt must carry a 0.0.690-passNNN identity");
    std::size_t digits = passPos + prefix.size();
    std::size_t end = digits;
    while (end < version.size() && version[end] >= '0' && version[end] <= '9') ++end;
    require(end > digits, "VERSION.txt pass identity must include digits");
    const std::string pass = version.substr(digits, end - digits);

    requireContains(versionHeader, ("#define ARPSID_BUILD_PASS " + pass).c_str(),
                    "version.h build pass must match VERSION.txt");
    require(readme.rfind("# ArpSID 0.0.690 pass" + pass, 0) == 0,
            "README must start with current pass entry");

    requireContains(readme, ("currently **0.0.690-pass" + pass + "**").c_str(),
                    "README source-folder note must name the current canonical pass");
    require(readme.find("currently **0.0.690-pass369**") == std::string::npos,
            "README source-folder note must not retain stale pass369 current wording");
    requireContains(status, "ReadmeCurrentVersionNoteV776Tests",
                    "handoff status must name the current-version note guard");
    requireContains(status, "auval: PENDING",
                    "status must keep auval pending until external log proves success");

    std::cout << "ReadmeCurrentVersionNoteV776Tests PASS\n";
    return 0;
}
