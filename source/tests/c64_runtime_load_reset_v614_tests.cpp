// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/c64_psid_runtime.h"
#include <array>
#include <cstdlib>
#include <cstdint>
#include <iostream>

static bool hasDowngrade(ArpSID::C64::RsidExactnessDowngrade value,
                         ArpSID::C64::RsidExactnessDowngrade bit) {
    using U = unsigned;
    return (static_cast<U>(value) & static_cast<U>(bit)) != 0u;
}

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

static std::array<std::uint8_t, 0x7c + 10> makeMinimalRsid() {
    std::array<std::uint8_t, 0x7c + 10> img{};
    img[0] = 'R'; img[1] = 'S'; img[2] = 'I'; img[3] = 'D';
    img[0x04] = 0x00; img[0x05] = 0x02; // version 2
    img[0x06] = 0x00; img[0x07] = 0x7c; // data offset
    img[0x08] = 0x00; img[0x09] = 0x00; // RSID load address must be embedded in payload
    img[0x0a] = 0x00; img[0x0b] = 0x00; // init defaults to effective load
    img[0x0c] = 0x00; img[0x0d] = 0x00; // RSID play address must be 0
    img[0x0e] = 0x00; img[0x0f] = 0x01; // songs 1
    img[0x10] = 0x00; img[0x11] = 0x01; // start song 1
    // payload little-endian load address $1000, then:
    // init/play workspace: RTS, NOP, NOP, RTS
    img[0x7c + 0] = 0x00;
    img[0x7c + 1] = 0x10;
    img[0x7c + 2] = 0x60;
    img[0x7c + 3] = 0xea;
    img[0x7c + 4] = 0xea;
    img[0x7c + 5] = 0x60;
    return img;
}

int main() {
    using ArpSID::C64::C64Runtime;
    using ArpSID::C64::RsidExactnessDowngrade;
    auto img = makeMinimalRsid();

    C64Runtime rt;
    require(rt.loadPsid(img.data(), img.size()), "first loadPsid succeeds");

    // Poison per-load counters and SID sink state as a reused runtime would see
    // after a bad/complex previous tune.
    rt.notifyTimedWriteOverflow(3);
    rt.notifyDroppedMultiSidWrites(2);
    require(hasDowngrade(rt.rsidExactnessDowngradeReasons(), RsidExactnessDowngrade::TimedWriteOverflow),
            "overflow downgrade set before reload");
    require(hasDowngrade(rt.rsidExactnessDowngradeReasons(), RsidExactnessDowngrade::DroppedMultiSidWrites),
            "dropped-write downgrade set before reload");

    // Re-load without calling reset(). This must now be a full per-load reset
    // boundary and clear exactness counters / sink state.
    require(rt.loadPsid(img.data(), img.size()), "second loadPsid succeeds without reset");
    const auto reasons = rt.rsidExactnessDowngradeReasons();
    require(!hasDowngrade(reasons, RsidExactnessDowngrade::TimedWriteOverflow),
            "timed-write overflow counter cleared by loadPsid");
    require(!hasDowngrade(reasons, RsidExactnessDowngrade::DroppedMultiSidWrites),
            "dropped multi-SID counter cleared by loadPsid");
    require(!rt.rsidPhi2PlaybackActive(), "PHI2 path inactive immediately after clean load");
    require(!rt.rsidExactPlaybackActive(), "exact playback inactive before init/play");

    std::cout << "C64Runtime loadPsid reset-boundary regression passed\n";
    return 0;
}
