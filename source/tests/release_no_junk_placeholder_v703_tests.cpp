#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifndef ARPSID_SOURCE_DIR
#define ARPSID_SOURCE_DIR "."
#endif

namespace {

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::cerr << "failed to open: " << path << "\n";
        std::exit(2);
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}


void rejectTokens(const std::string& relative, const std::vector<std::string>& tokens) {
    const std::string path = std::string(ARPSID_SOURCE_DIR) + "/" + relative;
    std::string body = lower(readFile(path));
    // Allow the guard/test identifier itself; reject only unresolved implementation markers.
    const std::string guardName = "releasenojunkplaceholder";
    for (std::size_t pos = body.find(guardName); pos != std::string::npos; pos = body.find(guardName, pos)) {
        body.replace(pos, guardName.size(), "releasenojunkguard");
    }
    for (const auto& token : tokens) {
        if (body.find(token) != std::string::npos) {
            std::cerr << "release junk token '" << token << "' found in " << relative << "\n";
            std::exit(1);
        }
    }
}

} // namespace

int main() {
    const std::vector<std::string> hardJunk = {
        "todo", "fixme", "tbd", "placeholder", "not implemented",
        "unimplemented", "future extension", "fake direct", "stubbed", "scaffolding"
    };

    // Pass129 release guard: keep the shipped realtime/DIGI/GUI entrypoints free
    // from unresolved implementation markers. Tests and historical closure docs
    // may discuss such words, but production paths must not carry them forward.
    rejectTokens("source/au3/ArpSIDDSPKernel.hpp", hardJunk);
    rejectTokens("source/au3/ArpSIDViewController.mm", hardJunk);
    rejectTokens("source/au3/ArpSIDDSPKernelAdapter.h", hardJunk);
    rejectTokens("source/au3/ArpSIDDSPKernelAdapter.mm", hardJunk);
    rejectTokens("include/arpsid/engines/digi_d418_stream_engine.h", hardJunk);
    rejectTokens("include/arpsid/engines/digi_sampler_engine.h", hardJunk);
    rejectTokens("include/arpsid/gui/digi_panel_model.h", hardJunk);
    rejectTokens("include/arpsid/gui/gui_realtime_projection_v588.h", hardJunk);
    rejectTokens("build.sh", hardJunk);
    rejectTokens("scripts/package_release.sh", hardJunk);

    std::cout << "ReleaseNoJunkPlaceholderV703Tests PASS\n";
    return 0;
}
