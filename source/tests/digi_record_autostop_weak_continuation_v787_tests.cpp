// Copyright (C) 2024-2026 Ulf Bertilsson
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
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

static bool contains(const std::string& s, const std::string& needle) {
    return s.find(needle) != std::string::npos;
}

int main() {
    const std::string root = ARPSID_SOURCE_ROOT;
    const std::string src = readFile(root + "/source/au3/ArpSIDViewController.mm");

    require(contains(src, "stopWeakSelf"),
            "DIGI record auto-stop continuations must capture a weak controller token");
    require(contains(src, "const std::uint32_t stopGeneration = chunk.clientGeneration;"),
            "AudioQueue auto-stop continuation must snapshot the callback generation");
    require(contains(src, "const std::uint32_t stopGeneration = recordGeneration;"),
            "AVAudioEngine tap auto-stop continuation must snapshot the record generation");
    require(contains(src, "ArpSIDViewController* stopSelf = stopWeakSelf;"),
            "auto-stop continuations must strong-load weak controller on main queue");
    require(contains(src, "if (stopSelf->_digiRecordGeneration_v184_.load(std::memory_order_acquire) != stopGeneration) return;"),
            "auto-stop continuations must drop stale queued stops after generation changes");

    require(!contains(src, "dispatch_async(dispatch_get_main_queue(), ^{ [s _digiStopRecord_v158_:nil]; });"),
            "AudioQueue auto-stop must not capture callback-local controller strongly");
    require(!contains(src, "dispatch_async(dispatch_get_main_queue(), ^{ [strongSelf _digiStopRecord_v158_:nil]; });"),
            "AVAudioEngine tap auto-stop must not capture callback-local strongSelf strongly");

    std::cout << "PASS: DIGI record auto-stop main-queue continuations use weak + generation guards\n";
    return 0;
}
