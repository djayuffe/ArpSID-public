#include <cstdlib>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <regex>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

namespace fs = std::filesystem;

static std::string readText(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        std::cerr << "FAIL: missing " << path << '\n';
        std::exit(1);
    }
    return std::string(std::istreambuf_iterator<char>(stream),
                       std::istreambuf_iterator<char>());
}

static void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

static int currentPassFromVersion(const std::string& version) {
    const std::string prefix = "0.0.690-pass";
    const std::size_t pos = version.find(prefix);
    require(pos != std::string::npos, "VERSION.txt must carry 0.0.690-passNNN");
    std::size_t begin = pos + prefix.size();
    std::size_t end = begin;
    while (end < version.size() && std::isdigit(static_cast<unsigned char>(version[end]))) ++end;
    require(end > begin, "VERSION.txt pass identity must include digits");
    return std::stoi(version.substr(begin, end - begin));
}

static bool isAllowedHistoricalReference(const fs::path& path,
                                         const std::string& text,
                                         std::size_t pos) {
    const std::string file = path.filename().string();
    const std::size_t begin = (pos > 120) ? pos - 120 : 0;
    const std::string context = text.substr(begin, 260);
    if (file == "readme_current_version_note_v776_tests.cpp" &&
        context.find("must not retain stale pass369") != std::string::npos) return true;
    if (file == "macos_build_green_status_v774_tests.cpp" &&
        context.find("macOS build after pass348: PASS") != std::string::npos) return true;
    return false;
}

int main() {
    const fs::path root = fs::path(ARPSID_SOURCE_ROOT);
    const int currentPass = currentPassFromVersion(readText(root / "VERSION.txt"));
    require(currentPass >= 377, "final stale-guard sweep must run on pass377 or newer");

    const std::regex passLiteral(R"(pass([0-9]{3,}))");
    const fs::path tests = root / "source" / "tests";
    for (const auto& entry : fs::directory_iterator(tests)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".cpp") continue;
        if (entry.path().filename() == "stale_version_guard_sweep_v800_tests.cpp") continue;
        if (entry.path().filename() == "readme_current_version_note_v776_tests.cpp") continue;
        const std::string text = readText(entry.path());
        for (std::sregex_iterator it(text.begin(), text.end(), passLiteral), end; it != end; ++it) {
            const int referencedPass = std::stoi((*it)[1].str());
            if (referencedPass == currentPass) continue;
            if (referencedPass < 343 || referencedPass > currentPass) continue;
            const std::size_t pos = static_cast<std::size_t>(it->position());
            if (!isAllowedHistoricalReference(entry.path(), text, pos)) {
                std::cerr << "FAIL: stale pass literal pass" << referencedPass
                          << " in " << entry.path() << '\n';
                std::exit(1);
            }
        }
    }

    std::cout << "StaleVersionGuardSweepV800Tests PASS\n";
    return 0;
}
