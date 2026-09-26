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

static std::string stripComments(const std::string& s) {
    std::string out; out.reserve(s.size());
    bool line=false, block=false, str=false, chr=false, esc=false;
    for (size_t i=0;i<s.size();++i) {
        const char c=s[i]; const char n=(i+1<s.size())?s[i+1]:'\0';
        if (line) { if (c=='\n') { line=false; out.push_back(c); } continue; }
        if (block) { if (c=='*' && n=='/') { block=false; ++i; } continue; }
        if (!str && !chr && c=='/' && n=='/') { line=true; ++i; continue; }
        if (!str && !chr && c=='/' && n=='*') { block=true; ++i; continue; }
        out.push_back(c);
        if (esc) { esc=false; continue; }
        if (c=='\\' && (str||chr)) { esc=true; continue; }
        if (!chr && c=='"') str=!str; else if (!str && c=='\'') chr=!chr;
    }
    return out;
}

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}
static void requireContains(const std::string& hay, const std::string& needle, const char* msg) {
    require(hay.find(needle) != std::string::npos, msg);
}

int main() {
    const std::string src = readFile("source/au3/ArpSIDHostAppDelegate.mm");
    const std::string code = stripComments(src);
    const std::string cmake = readFile("CMakeLists.txt");
    const std::string audit = readFile("AUDIT-FIXES-0.0.686.md");

    requireContains(code, "@interface ArpSIDHostMIDIContext_v778", "standalone MIDI weak context class exists");
    requireContains(code, "@property (atomic, weak) ArpSIDHostAppDelegate* delegate", "MIDI context holds zeroing weak delegate");
    requireContains(code, "ArpSIDHostDelegateFromMIDIContext_v778", "callbacks resolve delegate through weak context helper");
    requireContains(code, "ArpSIDReleaseRetainedMIDIContext_v778", "retained MIDI context has a release helper");
    requireContains(code, "_midiCallbackContext_v778 = (__bridge_retained void*)midiContext", "MIDI context retained before CoreMIDI registration");
    requireContains(code, "MIDIClientCreate(CFSTR(\"ArpSIDHost\"),\n                                       ArpSIDMIDINotifyProc,\n                                       _midiCallbackContext_v778", "MIDI client receives weak-box context");
    requireContains(code, "MIDIInputPortCreate(_midiClient,\n                                 CFSTR(\"ArpSIDInput\"),\n                                 ArpSIDMIDIReadProc,\n                                 _midiCallbackContext_v778", "MIDI input port receives weak-box context");
    requireContains(code, "MIDIDestinationCreate(_midiClient,\n                                   CFSTR(\"ArpSID\"),\n                                   ArpSIDMIDIReadProc,\n                                   _midiCallbackContext_v778", "virtual destination receives weak-box context");
    requireContains(code, "ArpSIDHostAppDelegate* delegate = ArpSIDHostDelegateFromMIDIContext_v778(readProcRefCon);\n    if (!delegate) return;", "MIDI read proc weak-loads and nil-guards delegate");
    requireContains(code, "ArpSIDReleaseRetainedMIDIContext_v778(_midiCallbackContext_v778);", "MIDI context released on teardown/failure paths");

    require(code.find("MIDIClientCreate(CFSTR(\"ArpSIDHost\"),\n                                       ArpSIDMIDINotifyProc,\n                                       (__bridge void*)self") == std::string::npos,
            "MIDIClientCreate must not receive raw self context");
    require(code.find("MIDIInputPortCreate(_midiClient,\n                                 CFSTR(\"ArpSIDInput\"),\n                                 ArpSIDMIDIReadProc,\n                                 (__bridge void*)self") == std::string::npos,
            "MIDIInputPortCreate must not receive raw self context");
    require(code.find("MIDIDestinationCreate(_midiClient,\n                                   CFSTR(\"ArpSID\"),\n                                   ArpSIDMIDIReadProc,\n                                   (__bridge void*)self") == std::string::npos,
            "MIDIDestinationCreate must not receive raw self context");
    require(code.find("(__bridge ArpSIDHostAppDelegate*)readProcRefCon") == std::string::npos,
            "MIDI read proc must not cast raw refCon directly to delegate");

    requireContains(cmake, "StandaloneCoreMIDIWeakContextV778Tests", "v778 guard registered in CMake");
    requireContains(audit, "Fix-order #32", "audit records fix-order #32");
    requireContains(audit, "CoreMIDI", "audit documents CoreMIDI context lifetime fix");

    std::cout << "StandaloneCoreMIDIWeakContextV778Tests PASS\n";
    return 0;
}
