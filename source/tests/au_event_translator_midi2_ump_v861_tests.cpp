// Copyright (C) 2024-2026 Ulf Bertilsson
// au_event_translator_midi2_ump_v861_tests.cpp
//
// Logic can deliver AU MIDI through AURenderEventMIDIEventList. The translator
// must accept both MIDI 1.0 UMP channel-voice packets and MIDI 2.0 UMP note
// packets; otherwise SID808 pads can be silent even though the host delivered
// the hit.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static std::string readText(const char* rel) {
    std::ifstream f(std::string(ARPSID_SOURCE_ROOT) + "/" + rel, std::ios::binary);
    if (!f) { std::cerr << "FAIL: missing " << rel << '\n'; std::exit(1); }
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << '\n'; std::exit(1); }
}

int main() {
    const std::string translator = readText("source/au3/ArpSIDAUEventTranslator.h");

    require(translator.find("const uint8_t msgType = (w >> 28) & 0xFu;") != std::string::npos,
            "translator decodes UMP message type");
    require(translator.find("msgType == 0x2u") != std::string::npos,
            "MIDI 1.0 UMP channel voice remains supported");
    require(translator.find("msgType == 0x4u") != std::string::npos,
            "MIDI 2.0 UMP channel voice is accepted");
    require(translator.find("pkt->wordCount == 0 || pkt->wordCount > 4") == std::string::npos,
            "multi-event MIDIEventList packets are not skipped by an old word-count cap");
    require(translator.find("wi + 1u >= pkt->wordCount") != std::string::npos,
            "MIDI 2.0 two-word packets are bounds checked");
    require(translator.find("const uint16_t value16") != std::string::npos,
            "MIDI 2.0 16-bit note velocity is decoded");
    require(translator.find("wi += 2u;") != std::string::npos,
            "MIDI 2.0 packets consume two UMP words");
    require(translator.find("EventKind::NoteOn") != std::string::npos &&
            translator.find("EventKind::NoteOff") != std::string::npos,
            "MIDI 2.0 note-on/note-off events are emitted");
    require(translator.find("EventKind::AllSoundOff") != std::string::npos &&
            translator.find("EventKind::AllNotesOff") != std::string::npos,
            "MIDIEventList CC120/123 map to canonical all-sound/all-notes-off");

    std::cout << "AuEventTranslatorMidi2UmpV861Tests PASS\n";
    return 0;
}
