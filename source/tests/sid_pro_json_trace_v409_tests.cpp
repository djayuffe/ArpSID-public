// Copyright (C) 2024-2026 Ulf Bertilsson
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
    v.push_back(uint8_t(x & 0xFFu)); v.push_back(uint8_t((x >> 8u) & 0xFFu));
    v.push_back(uint8_t((x >> 16u) & 0xFFu)); v.push_back(uint8_t((x >> 24u) & 0xFFu));
}

static std::string makeJson(uint32_t sidCount, const std::vector<uint8_t>& cycles, const std::vector<uint8_t>& data) {
    return std::string("{\n") +
        "\"metadata\":{\"title\":\"Unit Trace\",\"author\":\"SID-PRO\",\"clockFreq\":985248,\"isNtsc\":false,\"sidCount\":" + std::to_string(sidCount) + "},\n" +
        "\"optimizedWrites\":{\"cycles_u32le\":\"" + b64(cycles) + "\",\"data_u8\":\"" + b64(data) + "\"},\n" +
        "\"frames\":[]\n}";
}

int main() {
    std::vector<uint8_t> cycles;
    putLe32(cycles, 0); putLe32(cycles, 10); putLe32(cycles, 20); putLe32(cycles, 30);
    // Two-byte packed format: [reg + 32*chip, value]
    std::vector<uint8_t> data2 = {0x00, 0x11, 0x01, 0x22, 0x20, 0x33, 0x3F, 0x44};
    SidProJsonTrace trace{};
    SidProJsonResult r = SidProJsonParser::parse(makeJson(2, cycles, data2), trace);
    require(r == SidProJsonResult::OK, sidProJsonResultName(r));
    require(trace.metadata.sidCount == 2, "sidCount parsed");
    require(trace.metadata.clockFreq == 985248u, "clock parsed");
    require(trace.writes.size() == 4, "write count");
    require(trace.writes[2].chip == 1 && trace.writes[2].reg == 0 && trace.writes[2].value == 0x33, "packed multi SID decode");
    require(trace.writes[3].chip == 1 && trace.writes[3].reg == 31 && trace.writes[3].value == 0x44, "packed register 31 decode");

    std::array<std::array<uint8_t,32>,3> regs{};
    SidProJsonTracePlayer player;
    player.applyUntil(trace, 15, [&](uint8_t chip, uint8_t reg, uint8_t value, uint64_t){ regs[chip][reg] = value; });
    require(player.appliedWrites() == 2, "applyUntil first window");
    require(regs[0][0] == 0x11 && regs[0][1] == 0x22, "writes applied first chip");
    require(regs[1][0] == 0x00, "future chip write not applied early");
    player.applyUntil(trace, 100, [&](uint8_t chip, uint8_t reg, uint8_t value, uint64_t){ regs[chip][reg] = value; });
    require(player.appliedWrites() == 4, "applyUntil second window");
    require(regs[1][0] == 0x33 && regs[1][31] == 0x44, "multi chip writes applied");

    // Three-byte data format from TS OptimizedWriteLog: [chip, reg, val]
    std::vector<uint8_t> data3 = {0, 4, 0x80, 1, 4, 0x81, 0, 5, 0xAA, 1, 6, 0xBB};
    r = SidProJsonParser::parse(makeJson(2, cycles, data3), trace);
    require(r == SidProJsonResult::OK, "three-byte format parse");
    require(trace.writes[1].chip == 1 && trace.writes[1].reg == 4 && trace.writes[1].value == 0x81, "three-byte format decode");

    std::vector<uint8_t> badCycles;
    putLe32(badCycles, 100); putLe32(badCycles, 1);
    std::vector<uint8_t> badData = {0, 1, 1, 2};
    r = SidProJsonParser::parse(makeJson(1, badCycles, badData), trace);
    require(r == SidProJsonResult::OutOfOrderCycles, "out-of-order cycles rejected");

    r = SidProJsonParser::parse(makeJson(4, cycles, data2), trace);
    require(r == SidProJsonResult::UnsupportedSidCount, "unsupported sid count rejected");

    std::vector<uint8_t> badReg = {0x40, 0x11, 0, 0x22, 0, 0x33, 0, 0x44};
    r = SidProJsonParser::parse(makeJson(1, cycles, badReg), trace);
    require(r == SidProJsonResult::BadRegisterIndex, "bad packed register/chip rejected");

    std::cout << "SidProJsonTraceV409 tests passed\n";
    return 0;
}
