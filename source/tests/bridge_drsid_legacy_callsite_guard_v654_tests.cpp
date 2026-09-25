#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

static void require(bool ok, const std::string& msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}
static std::string readFile(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}

int main() {
    const std::filesystem::path root = ARPSID_SOURCE_DIR;
    const std::vector<std::filesystem::path> scanRoots = {
        root / "source",
        root / "include"
    };

    for (const auto& base : scanRoots) {
        for (const auto& e : std::filesystem::recursive_directory_iterator(base)) {
            if (!e.is_regular_file()) continue;
            const auto ext = e.path().extension().string();
            if (ext != ".cpp" && ext != ".h" && ext != ".hpp" && ext != ".mm") continue;
            const std::string rel = std::filesystem::relative(e.path(), root).string();
            const std::string s = readFile(e.path());

            // Allowed:
            // - the legacy alias declaration itself in drum_engine_host_bridge.h, gated by macro
            // - tests that detect absence of drsidEngine via SFINAE/string checks
            // - router's own DrSID accessor, which is not the host bridge diagnostic alias
            if (rel == "include/arpsid/engines/drum_engine_host_bridge.h" ||
                rel == "include/arpsid/engines/drum_engine_router.h" ||
                rel == "source/tests/bridge_drsid_accessor_removed_v649_tests.cpp" ||
                rel == "source/tests/digi_slot_bridge_contract_v639_tests.cpp" ||
                rel == "source/tests/bridge_drsid_legacy_callsite_guard_v654_tests.cpp" ||
                rel == "source/tests/drum_bridge_no_silence_v613_tests.cpp")
                continue;

            require(s.find(".drsidEngine()") == std::string::npos,
                    "stale bridge-local .drsidEngine() call-site in " + rel);
            require(s.find(".drsidEngine().") == std::string::npos,
                    "stale bridge-local .drsidEngine(). call-site in " + rel);
        }
    }

    const std::string fixed = readFile(root / "source/tests/drsid_restore_and_host_bridge_v537_tests.cpp");
    require(fixed.find("bridge.bridgeDiagnosticDrsidEngine()") == std::string::npos,
            "v537 test no longer references a bridge-owned diagnostic DrSID");
    require(fixed.find("bridge.canonicalDrSidEngine() == &canonicalDrSid") != std::string::npos,
            "v537 test pins external canonical DrSID identity");

    std::cout << "BridgeDrsidLegacyCallsiteGuardV654Tests PASS\n";
    return 0;
}
