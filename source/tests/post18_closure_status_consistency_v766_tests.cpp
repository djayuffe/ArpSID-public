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

std::string slice(const std::string& text,
                  const std::string& begin,
                  const std::string& end) {
    const std::size_t first = text.find(begin);
    require(first != std::string::npos, begin.c_str());
    const std::size_t last = text.find(end, first + begin.size());
    require(last != std::string::npos, end.c_str());
    return text.substr(first, last - first);
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
    const std::string audit = readText("AUDIT-FIXES-0.0.686.md");
    const std::string status = readText("arpsid-fix-list-690.md");
    const std::string readme = readText("README.md");
    const std::string version = readText("VERSION.txt");
    const std::string pass = extractCurrentPass(version);

    require(version.find("0.0.690-pass" + pass) != std::string::npos,
            "VERSION.txt carries current pass identity");
    require(readme.rfind("# ArpSID 0.0.690 pass" + pass, 0) == 0,
            "README starts with current pass closure entry");
    require(status.find("# ArpSID 0.0.690 pass" + pass) != std::string::npos,
            "handoff status is current pass");

    const std::string closure = slice(audit,
        "17/18. **Parameter-automation sample-accuracy telemetry**",
        "## Fix-order #12");
    require(closure.find("completed in\n    fix-order #17 and #18") != std::string::npos,
            "17/18 audit closure says source telemetry is complete");
    require(closure.find("Deferred deliberately") == std::string::npos,
            "17/18 audit closure must not claim completed telemetry is deferred");
    require(closure.find("source-level telemetry closure is no longer deferred") !=
                std::string::npos,
            "17/18 audit closure keeps a positive no-longer-deferred statement");
    require(closure.find("Runtime AUv2/Logic validation is still a macOS release step") != std::string::npos,
            "17/18 audit closure preserves the macOS runtime caveat");

    require(audit.find("## Fix-order #17 — Parameter dirty-flush fallback telemetry") !=
                std::string::npos,
            "audit doc contains fix-order #17 details");
    require(audit.find("## Fix-order #18 — AUv2 ramp-anchor/drop telemetry") !=
                std::string::npos,
            "audit doc contains fix-order #18 details");
    require(audit.find("## Fix-order #19 — post-#18 closure/status consistency") !=
                std::string::npos,
            "audit doc contains fix-order #19 details");
    require(status.find("#19: post-#18 closure/status consistency") !=
                std::string::npos,
            "handoff status lists fix-order #19");
    require(status.find("Post18ClosureStatusConsistencyV766Tests") !=
                std::string::npos,
            "handoff status names the v766 guard");

    std::cout << "Post18ClosureStatusConsistencyV766Tests PASS\n";
    return 0;
}
