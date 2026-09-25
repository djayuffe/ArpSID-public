#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}
static std::string readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary); std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}

int main() {
    const std::string root = ARPSID_SOURCE_DIR;
    const std::string d = readFile(root + "/include/arpsid/engines/drsid_engine.h");
    const auto kit = d.find("void triggerKitMidiNote(int midiNote,");
    require(kit != std::string::npos, "triggerKitMidiNote exists");
    const auto end = d.find("std::uint16_t lastKitSelectedFactorySlot", kit);
    const std::string body = d.substr(kit, end - kit);
    require(body.find("makeKitAuthoredVoiceProgram_") != std::string::npos,
            "KIT trigger builds final voice program before applying");
    require(body.find("applyKitAuthoredVoiceProgram_") != std::string::npos,
            "KIT trigger applies authored voice program");
    require(body.find("triggerMidiNote(") == std::string::npos,
            "KIT trigger no longer calls generic triggerMidiNote then post-mutates voice");
    require(d.find("struct KitAuthoredDrSidVoiceProgram_") != std::string::npos,
            "authored KIT voice program type exists");
    std::cout << "DrSidKitPreAuthoredTriggerV664Tests PASS\n";
    return 0;
}
