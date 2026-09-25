#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

static void require(bool ok, const std::string& msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}
static std::string readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    require(static_cast<bool>(f), "cannot open " + p);
    std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}

int main() {
    const std::string root = ARPSID_SOURCE_DIR;
    const std::string cmake = readFile(root + "/CMakeLists.txt");
    const std::string script = readFile(root + "/scripts/run_full_closure_validation.sh");

    const std::vector<std::string> requiredTests = {
        "DrumBridgeNoSilenceV613Tests",
        "KitMixedTargetCompileV628Tests",
        "KitHashFieldContractV632Tests",
        "DspKernelMultiTuSmokeV634Tests",
        "KitSid808FactoryVoicePrecedenceV637Tests",
        "DrSidKitPayloadV638Tests",
        "DigiSlotBridgeContractV639Tests",
        "DrSidFactorySlotAudioShapeV640Tests",
        "DigiNonzeroSlotRuntimeV641Tests",
        "Sid808ProjectionSyncContractV642Tests",
        "KitTargetAuthorityV643Tests",
        "DrumBridgeRuntimeAuthorityV644Tests"
    };
    for (const auto& t : requiredTests) {
        require(cmake.find(t) != std::string::npos, "CMake registers " + t);
        require(script.find(t) != std::string::npos, "closure script runs " + t);
    }

    require(script.find("verify_source_tree.py") != std::string::npos,
            "closure script runs source-tree guard");
    require(script.find("ctest --test-dir") != std::string::npos,
            "closure script runs ctest");

    std::cout << "FullClosureManifestV645Tests PASS\n";
    return 0;
}
