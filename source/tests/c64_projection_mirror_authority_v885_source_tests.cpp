#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

static std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::string dirnameOf(std::string path) {
    const std::size_t pos = path.find_last_of("/\\");
    return (pos == std::string::npos) ? std::string(".") : path.substr(0, pos);
}

static std::string readKernelSource() {
    const std::string testDir = dirnameOf(__FILE__);
    const std::vector<std::string> candidates = {
        "source/au3/ArpSIDDSPKernel.hpp",
        "../source/au3/ArpSIDDSPKernel.hpp",
        "../../source/au3/ArpSIDDSPKernel.hpp",
        testDir + "/../au3/ArpSIDDSPKernel.hpp",
        testDir + "/../../source/au3/ArpSIDDSPKernel.hpp",
    };
    for (const auto& c : candidates) {
        std::string s = readFile(c);
        if (!s.empty()) return s;
    }
    return {};
}

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

static bool contains(const std::string& s, const std::string& needle) {
    return s.find(needle) != std::string::npos;
}

int main() {
    const std::string kernel = readKernelSource();
    require(!kernel.empty(), "kernel source must be readable");
    require(contains(kernel, "v885 closure: projection observer writes are block-local even when"),
            "v885 block-local projection closure comment must be present");
    require(contains(kernel, "c64Platform_.flushScheduledProjectionWrites();\n        } else {"),
            "consumed mirror branch must flush leftover projection writes before else branch");
    require(contains(kernel, "Those writes describe audio events that already happened in this\n            // host block and must not replay late in a future block"),
            "v885/v899 source must document late replay prevention");
    require(contains(kernel, "Ordinary CPU/CIA/PSID scheduled events are still\n            // preserved"),
            "v899 source must preserve ordinary scheduled events when flushing projection leftovers");
    require(contains(kernel, "dropped, so a cockpit opened later shows current register state"),
            "unconsumed cosmetic block path must flush values instead of dropping them");
    std::cout << "C64ProjectionMirrorAuthorityV885SourceTests PASS\n";
    return 0;
}
