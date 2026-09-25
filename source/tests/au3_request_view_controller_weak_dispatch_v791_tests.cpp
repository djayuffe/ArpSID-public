#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static std::string readFile(const std::string& rel) {
    std::ifstream in(std::string(ARPSID_SOURCE_ROOT) + "/" + rel, std::ios::binary);
    if (!in) { std::cerr << "missing file: " << rel << "\n"; std::exit(1); }
    std::ostringstream ss; ss << in.rdbuf(); return ss.str();
}

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

static std::string sliceBetween(const std::string& s, const std::string& begin, const std::string& end) {
    const auto b = s.find(begin);
    require(b != std::string::npos, "begin marker missing");
    const auto e = s.find(end, b + begin.size());
    require(e != std::string::npos, "end marker missing");
    return s.substr(b, e - b);
}

int main() {
    const std::string src = readFile("source/au3/ArpSIDAudioUnit.mm");
    const std::string cmake = readFile("CMakeLists.txt");
    const std::string audit = readFile("AUDIT-FIXES-0.0.686.md");

    const std::string method = sliceBetween(src,
        "requestViewControllerWithCompletionHandler", "#endif\n\n@end");

    require(method.find("__weak ArpSIDAudioUnit* weakAudioUnit_v791 = self;") != std::string::npos,
            "requestViewController must capture the AU through a weak token before main-queue bounce");
    require(method.find("ArpSIDAudioUnit* strongAudioUnit_v791 = weakAudioUnit_v791;") != std::string::npos,
            "main-queue controller builder must strong-load the weak AU token");
    require(method.find("if (!strongAudioUnit_v791)") != std::string::npos,
            "controller builder must fail closed if the AU has gone away");
    require(method.find("completionHandler(nil);") != std::string::npos,
            "controller builder must complete with nil when lifetime token is gone");
    require(method.find("[controller setValue:strongAudioUnit_v791 forKey:@\"extensionAudioUnit\"]") != std::string::npos,
            "AUv3 extension controller must receive the weak-loaded AU object");
    require(method.find("[controller connectAudioUnit:strongAudioUnit_v791]") != std::string::npos,
            "AUv2 direct controller must connect to the weak-loaded AU object");
    require(method.find("[controller setValue:self forKey:@\"extensionAudioUnit\"]") == std::string::npos,
            "AUv3 path must not use self directly after queued dispatch");
    require(method.find("[controller connectAudioUnit:self]") == std::string::npos,
            "AUv2 path must not use self directly after queued dispatch");

    require(cmake.find("AU3RequestViewControllerWeakDispatchV791Tests") != std::string::npos,
            "v791 guard registered in CMake");
    require(audit.find("fix-order #45") != std::string::npos || audit.find("Fix-order #45") != std::string::npos,
            "audit records fix-order #45");
    require(audit.find("AU3 requestViewController weak dispatch") != std::string::npos,
            "audit documents requestViewController weak dispatch hardening");

    std::cout << "AU3RequestViewControllerWeakDispatchV791Tests PASS\n";
    return 0;
}
