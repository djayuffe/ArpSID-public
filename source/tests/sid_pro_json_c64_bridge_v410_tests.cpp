// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/c64_platform.h"
#include "arpsid/core/sid_pro_json_c64_bridge.h"
#include "arpsid/core/sid_pro_json_trace.h"
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ArpSID;

static void require(bool v, const char* msg) {
    if (!v) throw std::runtime_error(msg);
}

static std::string b64(const std::vector<uint8_t>& in) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        uint32_t n = (uint32_t(in[i]) << 16u) | (uint32_t(in[i + 1]) << 8u) | uint32_t(in[i + 2]);
        out.push_back(t[(n >> 18u) & 63u]); out.push_back(t[(n >> 12u) & 63u]);
        out.push_back(t[(n >> 6u) & 63u]); out.push_back(t[n & 63u]);
    }
    if (i < in.size()) {
        uint32_t n = uint32_t(in[i]) << 16u;
        out.push_back(t[(n >> 18u) & 63u]);
        if (i + 1 < in.size()) {
            n |= uint32_t(in[i + 1]) << 8u;
            out.push_back(t[(n >> 12u) & 63u]); out.push_back(t[(n >> 6u) & 63u]); out.push_back('=');
        } else {
            out.push_back(t[(n >> 12u) & 63u]); out.push_back('='); out.push_back('=');
        }
    }
    return out;
}

static void putLe32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(uint8_t(x & 0xFFu));
    v.push_back(uint8_t((x >> 8u) & 0xFFu));
    v.push_back(uint8_t((x >> 16u) & 0xFFu));
    v.push_back(uint8_t((x >> 24u) & 0xFFu));
}

static std::string makeJson(uint32_t sidCount, const std::vector<uint8_t>& cycles, const std::vector<uint8_t>& data) {
    return std::string("{\n") +
        "\"metadata\":{\"title\":\"Bridge Trace\",\"author\":\"SID-PRO\",\"clockFreq\":985248,\"isNtsc\":false,\"sidCount\":" + std::to_string(sidCount) + "},\n" +
        "\"totalDuration\":1.25,\"frameCount\":50,\"detectedRefreshRate\":50.0,\n" +
        "\"optimizedWrites\":{\"cycles_u32le\":\"" + b64(cycles) + "\",\"data_u8\":\"" + b64(data) + "\"},\n" +
        "\"frames\":[]\n}";
}

int main() {
    std::vector<uint8_t> cycles;
    putLe32(cycles, 0);
    putLe32(cycles, 4);
    putLe32(cycles, 9);
    std::vector<uint8_t> dataSingle = {0x00, 0x12, 0x01, 0x34, 0x04, 0x80};

    SidProJsonTrace trace{};
    auto pr = SidProJsonParser::parse(makeJson(1, cycles, dataSingle), trace);
    require(pr == SidProJsonResult::OK, sidProJsonResultName(pr));
    require(trace.metadata.frameCount == 50, "frameCount metadata parsed");
    require(trace.metadata.detectedRefreshRate == 50.0, "refresh metadata parsed");
    require(trace.metadata.sidAddresses[0] == 0xD400u, "primary SID base default");

    C64::C64Platform platform;
    platform.reset(true);
    SidProJsonC64Bridge bridge;
    auto br = bridge.applyUntilToC64Platform(trace, platform, 4);
    require(br == SidProJsonBridgeResult::OK, sidProJsonBridgeResultName(br));
    require(bridge.telemetry().applied == 2, "two writes accepted into C64 bus");
    require(bridge.telemetry().immediate == 1, "first write immediate");
    require(bridge.telemetry().scheduled == 1, "second write scheduled");
    require(platform.sidRegisterImage()[0] == 0x12, "immediate write visible");
    platform.runCycles(5);
    require(platform.sidRegisterImage()[1] == 0x34, "scheduled write visible after PHI2 advance");
    require(platform.sidRegisterImage()[4] == 0x00, "future write not applied yet");
    br = bridge.applyUntilToC64Platform(trace, platform, 100);
    require(br == SidProJsonBridgeResult::OK, "remaining write accepted");
    platform.runCycles(16);
    require(platform.sidRegisterImage()[4] == 0x80, "remaining write visible");

    // Read-only SID window must be skipped, not mirrored into the SID image.
    std::vector<uint8_t> roCycles;
    putLe32(roCycles, 0);
    std::vector<uint8_t> roData = {0x19, 0xFF}; // POTX read-only window
    pr = SidProJsonParser::parse(makeJson(1, roCycles, roData), trace);
    require(pr == SidProJsonResult::OK, "read-only reg can exist in archive log");
    platform.reset(true);
    bridge.reset();
    br = bridge.applyUntilToC64Platform(trace, platform, 0);
    require(br == SidProJsonBridgeResult::OK, "read-only skipped without hard failure");
    require(bridge.telemetry().rejectedReadOnly == 1, "read-only telemetry counted");
    require(platform.sidRegisterImage()[0x19] == 0x00, "read-only not written");

    // Multi-SID must not be silently folded onto C64Platform chip 0.
    std::vector<uint8_t> multiCycles;
    putLe32(multiCycles, 0);
    putLe32(multiCycles, 1);
    std::vector<uint8_t> multiData = {0x00, 0x11, 0x20, 0x22}; // chip0 reg0, chip1 reg0
    pr = SidProJsonParser::parse(makeJson(2, multiCycles, multiData), trace);
    require(pr == SidProJsonResult::OK, "multi SID trace parses");
    platform.reset(true);
    bridge.reset();
    br = bridge.applyUntilToC64Platform(trace, platform, 100);
    require(br == SidProJsonBridgeResult::MultiSidRequiresExternalSink, "multi SID rejected by single C64Platform bridge");
    require(platform.sidRegisterImage()[0] == 0x11, "chip0 write still applied");
    require(bridge.telemetry().rejectedMultiSid == 1, "multi SID rejection counted");

    SidProJsonMultiSidRegisterBank bank;
    bridge.reset();
    br = bridge.applyUntilToRegisterBank(trace, bank, 100);
    require(br == SidProJsonBridgeResult::OK, "multi SID register bank accepts trace");
    require(bank.chip(0)[0] == 0x11, "bank chip0 write");
    require(bank.chip(1)[0] == 0x22, "bank chip1 write");
    require(bank.lastChip() == 1 && bank.lastReg() == 0 && bank.lastValue() == 0x22, "bank last write metadata");

    std::array<std::array<uint8_t, 32>, 3> external{};
    bridge.reset();
    br = bridge.applyUntilToExternalSink(trace, 100, [&](uint8_t chip, uint8_t reg, uint8_t value, uint64_t) noexcept {
        external[chip][reg] = value;
    });
    require(br == SidProJsonBridgeResult::OK, "external multi SID sink accepts trace");
    require(external[0][0] == 0x11 && external[1][0] == 0x22, "external sink writes both chips");

    std::cout << "SidProJsonC64BridgeV410 tests passed\n";
    return 0;
}
