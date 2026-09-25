#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::cerr << "missing file: " << path << "\n";
        std::exit(2);
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static void requireContains(const std::string& haystack, const std::string& needle, const char* msg) {
    if (haystack.find(needle) == std::string::npos) {
        std::cerr << "FAIL: " << msg << "\nmissing: " << needle << "\n";
        std::exit(1);
    }
}

static void requireAbsent(const std::string& haystack, const std::string& needle, const char* msg) {
    if (haystack.find(needle) != std::string::npos) {
        std::cerr << "FAIL: " << msg << "\nforbidden: " << needle << "\n";
        std::exit(1);
    }
}

int main() {
    const std::string src = readFile(std::string(ARPSID_SOURCE_ROOT) + "/source/au2/ArpSIDAUv2Component.mm");

    requireContains(src,
        "__weak ArpSIDAudioUnit* weakAudioUnit_v795 = audioUnit;",
        "AUv2 view factory must create a weak AU token before queued main-thread view construction");
    requireContains(src,
        "ArpSIDAudioUnit* strongAudioUnit_v795 = weakAudioUnit_v795;\n                if (!strongAudioUnit_v795) return;",
        "buildBlock must weak-load the AU at point of use");
    requireContains(src,
        "[controller connectAudioUnit:strongAudioUnit_v795];",
        "new controller must connect through weak-loaded AU");
    requireContains(src,
        "if (strongAudioUnit_v795) disposeAbandonedAUv2Editor(strongAudioUnit_v795);",
        "timeout/orphan disposal must also weak-load the AU");

    requireAbsent(src,
        "disposeAbandonedAUv2Editor(audioUnit);",
        "queued AUv2 timeout disposal must not capture the AU strongly/raw");
    requireAbsent(src,
        "[controller connectAudioUnit:audioUnit];",
        "queued AUv2 editor construction must not capture the AU strongly/raw");
    requireContains(src,
        "associatedViewControllerForAudioUnit(strongAudioUnit_v795);",
        "associated view/controller lookup inside buildBlock must use weak-loaded AU");

    std::cout << "PASS auv2_view_factory_weak_au_continuation_v795\n";
    return 0;
}
