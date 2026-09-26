// Copyright (C) 2024-2026 Ulf Bertilsson
// psid_parser_robustness_v849_tests.cpp
//
// Blocker-prevention guard for the low-level PSID/RSID header parser. A SID
// player must never crash, over-read, or over-write RAM on a malformed or
// truncated file — a bad file is a load failure, never an out-of-bounds access.
// This fuzzes psidParse() across truncation and field corruption, pins the exact
// rejection codes, and proves psidLoadIntoRam() clamps to the 64 KiB RAM window.

#include "arpsid/core/psid_header.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace ArpSID;

static int g_failures = 0;
static void require(bool ok, const char* msg) {
    if (!ok) { std::printf("FAIL: %s\n", msg); ++g_failures; }
}

static void poke16(std::vector<uint8_t>& v, size_t off, uint16_t val) { // big-endian
    v[off] = static_cast<uint8_t>(val >> 8); v[off + 1] = static_cast<uint8_t>(val & 0xFF);
}

// Build a minimal but valid v2 PSID with a non-zero load address.
static std::vector<uint8_t> makeValidV2Psid() {
    std::vector<uint8_t> v(0x7C + 4, 0u); // 0x7C header + 4 payload bytes
    v[0]='P'; v[1]='S'; v[2]='I'; v[3]='D';
    poke16(v, 0x04, 2);       // version 2
    poke16(v, 0x06, 0x7C);    // dataOffset
    poke16(v, 0x08, 0x1000);  // loadAddr
    poke16(v, 0x0A, 0x1000);  // initAddr
    poke16(v, 0x0C, 0x1003);  // playAddr
    poke16(v, 0x0E, 2);       // songs
    poke16(v, 0x10, 1);       // startSong
    poke16(v, 0x76, 0);       // flags
    // extra-SID address bytes at 0x7A/0x7B stay 0 (no extra SID) → valid
    return v;
}

int main() {
    // ── A. A well-formed v2 PSID parses and reports its fields. ──────────────
    {
        auto v = makeValidV2Psid();
        PsidHeader h{};
        require(psidParse(v.data(), (uint32_t)v.size(), h) == PsidParseResult::OK,
                "valid v2 PSID parses OK");
        require(!h.isRsid && h.version == 2u && h.loadAddr == 0x1000u &&
                h.songs == 2u && h.startSong == 1u && h.playAddr == 0x1003u,
                "parsed fields match the crafted header");
        require(h.effectiveLoadAddr == 0x1000u && h.payloadLen == 4u,
                "effective load address and payload length are correct");
    }

    // ── B. Truncation at every length must never crash or over-read. ────────
    {
        auto full = makeValidV2Psid();
        for (uint32_t len = 0; len <= (uint32_t)full.size(); ++len) {
            PsidHeader h{};
            const auto r = psidParse(full.data(), len, h); // must simply return
            if (len < 0x76u) require(r == PsidParseResult::TooShort,
                                     "sub-header truncation is TooShort");
            if (r == PsidParseResult::OK) require(h.payload != nullptr && h.payloadLen > 0,
                                                  "OK implies a real payload");
        }
        // nullptr / zero length are handled.
        PsidHeader h{};
        require(psidParse(nullptr, 100, h) == PsidParseResult::TooShort, "null data rejected");
        require(psidParse(full.data(), 0, h) == PsidParseResult::TooShort, "zero length rejected");
    }

    // ── C. Field corruption maps to the exact rejection code. ───────────────
    auto expect = [&](void(*mutate)(std::vector<uint8_t>&), PsidParseResult want, const char* msg) {
        auto v = makeValidV2Psid();
        mutate(v);
        PsidHeader h{};
        require(psidParse(v.data(), (uint32_t)v.size(), h) == want, msg);
    };
    expect([](std::vector<uint8_t>& v){ v[0]='X'; }, PsidParseResult::BadMagic, "bad magic rejected");
    expect([](std::vector<uint8_t>& v){ poke16(v,0x04,0); }, PsidParseResult::BadVersion, "version 0 rejected");
    expect([](std::vector<uint8_t>& v){ poke16(v,0x04,5); }, PsidParseResult::BadVersion, "version 5 rejected");
    expect([](std::vector<uint8_t>& v){ poke16(v,0x06,0x10); }, PsidParseResult::BadOffset, "tiny dataOffset rejected");
    expect([](std::vector<uint8_t>& v){ poke16(v,0x0E,0); }, PsidParseResult::BadSongCount, "zero song count rejected");
    expect([](std::vector<uint8_t>& v){ poke16(v,0x10,0); }, PsidParseResult::BadStartSong, "zero start song rejected");
    expect([](std::vector<uint8_t>& v){ poke16(v,0x10,99); }, PsidParseResult::BadStartSong, "start song > songs rejected");
    // RSID must have loadAddr==0 and playAddr==0.
    expect([](std::vector<uint8_t>& v){ v[0]='R'; }, PsidParseResult::BadRsidHeader, "RSID with nonzero load/play rejected");

    // ── D. Garbage buffers of many sizes must never crash. ──────────────────
    {
        uint32_t seed = 0x1234567u;
        for (int trial = 0; trial < 4000; ++trial) {
            seed = seed * 1664525u + 1013904223u;
            const uint32_t len = seed % 0x120u; // 0..287 bytes
            std::vector<uint8_t> junk(len);
            for (uint32_t i = 0; i < len; ++i) { seed = seed * 1664525u + 1013904223u; junk[i] = (uint8_t)(seed >> 16); }
            PsidHeader h{};
            (void)psidParse(junk.empty() ? nullptr : junk.data(), len, h); // must not crash
        }
        require(true, "4000 random buffers parsed without crashing");
    }

    // ── E. psidLoadIntoRam clamps to the 64 KiB window (no over-write). ─────
    {
        // Craft a PSID whose load address is near the top of RAM with a payload
        // far larger than the space that remains below $FFFF.
        std::vector<uint8_t> v(0x7C + 256, 0u);
        v[0]='P'; v[1]='S'; v[2]='I'; v[3]='D';
        poke16(v, 0x04, 2); poke16(v, 0x06, 0x7C);
        poke16(v, 0x08, 0xFFF0); poke16(v, 0x0A, 0xFFF0); poke16(v, 0x0C, 0xFFF3);
        poke16(v, 0x0E, 1); poke16(v, 0x10, 1);
        for (int i = 0; i < 256; ++i) v[0x7C + i] = (uint8_t)(0xA0 + (i & 0x0F));
        PsidHeader h{};
        require(psidParse(v.data(), (uint32_t)v.size(), h) == PsidParseResult::OK, "top-of-RAM PSID parses");
        require(h.effectiveLoadAddr == 0xFFF0u && h.payloadLen == 256u, "payload is 256 bytes at $FFF0");

        std::array<uint8_t, 65536 + 16> ram{};
        ram.fill(0x5Au);
        const uint8_t sentinel = 0x5Au;
        psidLoadIntoRam(h, ram.data());
        // Only the 16 bytes $FFF0..$FFFF may be written.
        for (int i = 0; i < 16; ++i)
            require(ram[0xFFF0 + i] == (uint8_t)(0xA0 + (i & 0x0F)), "clamped payload byte written");
        for (size_t i = 65536; i < ram.size(); ++i)
            require(ram[i] == sentinel, "no write past the 64 KiB RAM window");
        require(ram[0xFFEF] == sentinel, "no write below the load address");
    }

    if (g_failures == 0) { std::printf("psid_parser_robustness_v849_tests: PASS\n"); return 0; }
    std::printf("psid_parser_robustness_v849_tests: %d FAILURE(S)\n", g_failures);
    return 1;
}
