#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}
static std::string readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}

int main() {
    const std::string root = ARPSID_SOURCE_DIR;
    const std::string s = readFile(root + "/scripts/macos_full_build_install_clear_au_logic_cache.sh");
    const std::string b = readFile(root + "/build.sh");
    require(s.rfind("#!/usr/bin/env bash", 0) == 0, "macOS script has correct shebang");
    require(b.rfind("#!/usr/bin/env bash", 0) == 0, "root build.sh has correct shebang");
    require(s.find("cmake --build") != std::string::npos, "script builds");
    require(s.find("ctest --test-dir") != std::string::npos, "script runs ctest");
    require(s.find("AudioUnitCache") != std::string::npos, "script clears AU cache");
    require(s.find("com.apple.logic10") != std::string::npos, "script clears Logic cache");
    require(s.find("AudioComponentRegistrar") != std::string::npos, "script rebuilds AU registry");
    require(s.find("auval -v aumu") != std::string::npos, "script runs auval validation");
    require(s.find("FOUND_COUNT") != std::string::npos, "script checks duplicate installed components");
    std::cout << "MacOSProductionScriptV653Tests PASS\n";
    return 0;
}
