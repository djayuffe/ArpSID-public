// Copyright (C) 2024-2026 Ulf Bertilsson
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
    const std::string src = readFile("source/au3/ArpSIDHostAppDelegate.mm");
    const std::string cmake = readFile("CMakeLists.txt");

    const std::string midiFn = sliceBetween(src,
        "- (void)_handleMIDIPacketList:(const MIDIPacketList*)pktList fromSource:(MIDIEndpointRef)srcEp",
        "// ─── Menu Bar");

    require(midiFn.find("__weak ArpSIDHostAppDelegate* weakSelf = self;") != std::string::npos,
            "CoreMIDI handler must create a weak delegate token for async main continuations");
    require(midiFn.find("ArpSIDHostAppDelegate* strongSelf = weakSelf;") != std::string::npos,
            "CoreMIDI main continuations must strong-load weakSelf inside the block");
    require(midiFn.find("if (!strongSelf) return;") != std::string::npos,
            "MIDI activity continuation must nil-guard the weak-loaded delegate");
    require(midiFn.find("if (!strongSelf || !strongSelf->_audioUnit) return;") != std::string::npos,
            "MIDI parameter continuations must nil-guard delegate and AU");
    require(midiFn.find("[strongSelf _flashMIDIActivity]") != std::string::npos,
            "MIDI activity flash must use the weak-loaded delegate");
    require(midiFn.find("[strongSelf->_audioUnit setParameterValue:enable") != std::string::npos,
            "CC65 continuation must use weak-loaded delegate/AU");
    require(midiFn.find("strongSelf->_softPedalSavedCutoff") != std::string::npos,
            "CC67 continuation must use weak-loaded delegate state");

    require(midiFn.find("dispatch_async(dispatch_get_main_queue(), ^{ [self _flashMIDIActivity]; })") == std::string::npos,
            "MIDI activity continuation must not capture self directly");
    require(midiFn.find("dispatch_async(dispatch_get_main_queue(), ^{\n                            if (!self->_audioUnit) return;") == std::string::npos,
            "MIDI parameter continuation must not capture self directly");
    require(midiFn.find("self->_softPedalSavedCutoff =") == std::string::npos,
            "soft-pedal continuation must not mutate self inside async main block");

    require(cmake.find("StandaloneCoreMIDIMainQueueWeakContinuationsV784Tests") != std::string::npos,
            "v784 guard registered in CMake");

    std::cout << "StandaloneCoreMIDIMainQueueWeakContinuationsV784Tests PASS\n";
    return 0;
}
