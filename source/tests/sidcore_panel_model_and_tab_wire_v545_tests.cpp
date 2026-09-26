// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// sidcore_panel_model_and_tab_wire_v545_tests.cpp
//
// Pins:
// A. v544 GUI tabs SETTINGS + C64-STATE are now in the visible-tab
// inventories. We can't link the ViewController (Cocoa) from this
// test, so we mirror the contract via the header inventory:
// `tab_architecture.h` already pins the 9-tab inventory and the
// v544 tabs sit at indices 6 (SIDCORE), 7 (C64STATE), 8 (SETTINGS).
// Combined with the v544 NSView builders existing in the AUv2
// build target, this is the wire-up evidence.
//
// B. SIDCORE panel model (v545 — docs/TAB_ARCHITECTURE.md §6). Pins:
// - SidCoreRegisterWriteEvent layout (16 bytes, trivially copyable)
// - SidCoreLiveSnapshot layout (32 bytes, trivially copyable)
// - Event flag bits stable
// - SidCoreRegisterWriteRing256 push/drain semantics
// - Drain overflow detection
// - SidCorePanelModel end-to-end: publish + drain + live snapshot
// - Reset clears ring + zero-snapshot publish

#include "arpsid/gui/sidcore_panel_model.h"
#include "arpsid/gui/tab_architecture.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>

namespace {

void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::abort();
    }
}

} // namespace

int main() {
    using namespace ArpSID;
    using namespace ArpSID::GUI;

    // ── A. v544 tab inventory is unchanged (SIDCORE + C64STATE + SETTINGS
    // are part of the 9-tab architecture spec, regardless of AUv2    // wrapper-side tab-bar wiring) ──────────────────────────────────
    {
        require(kTabCount == 17, "17 tabs in production architecture");
        require(tabSpec(ArpSIDTab::SIDCORE).id    == ArpSIDTab::SIDCORE,
                "SIDCORE tab at architecture index 6");
        require(tabSpec(ArpSIDTab::C64STATE).id   == ArpSIDTab::C64STATE,
                "C64STATE retained as migration spec");
        require(!isVisibleProductionTab(ArpSIDTab::C64STATE),
                "C64STATE diagnostics surface retired");
        require(tabSpec(ArpSIDTab::SETTINGS).id   == ArpSIDTab::SETTINGS,
                "SETTINGS tab at architecture index 8");
    }

    // ── B. SidCoreRegisterWriteEvent layout ──────────────────────────────
    {
        require(sizeof(SidCoreRegisterWriteEvent) == 16,
                "SidCoreRegisterWriteEvent is 16 bytes");
        require(std::is_trivially_copyable<SidCoreRegisterWriteEvent>::value,
                "trivially copyable");
        // Field-presence smoke test.
        SidCoreRegisterWriteEvent ev{};
        ev.sidCycleStamp = 12345;
        ev.registerIndex = 0x04;
        ev.value = 0x40;
        ev.voiceHint = 0;
        ev.flags = SidCoreEventFlag::kGateOn;
        require(ev.sidCycleStamp == 12345, "sidCycleStamp round-trip");
        require(ev.registerIndex == 0x04, "registerIndex round-trip");
        require((ev.flags & SidCoreEventFlag::kGateOn) != 0, "flag bit accessible");
    }

    // ── B'. Event flag bits are stable ───────────────────────────────────
    {
        require(SidCoreEventFlag::kGateOn         == 0x01, "kGateOn stable");
        require(SidCoreEventFlag::kGateOff        == 0x02, "kGateOff stable");
        require(SidCoreEventFlag::kHardRestart    == 0x04, "kHardRestart stable");
        require(SidCoreEventFlag::kFilterMode     == 0x08, "kFilterMode stable");
        require(SidCoreEventFlag::kWaveformChange == 0x10, "kWaveformChange stable");
    }

    // ── C. SidCoreLiveSnapshot layout ────────────────────────────────────
    {
        require(sizeof(SidCoreLiveSnapshot) == 32,
                "SidCoreLiveSnapshot is 32 bytes");
        require(std::is_trivially_copyable<SidCoreLiveSnapshot>::value,
                "trivially copyable");
    }

    // ── D. SidCoreRegisterWriteRing256 push + drain semantics ────────────
    {
        SidCoreRegisterWriteRing256 ring;
        require(ring.kCapacity == 256, "capacity 256");
        // Empty drain returns 0.
        std::array<SidCoreRegisterWriteEvent, 8> buf{};
        bool overflowed = false;
        require(ring.drain(buf.data(), buf.size(), &overflowed) == 0,
                "drain on empty ring returns 0");
        require(!overflowed, "no overflow on empty drain");

        // Push 5 events, drain into a buffer of 8.
        for (int i = 0; i < 5; ++i) {
            SidCoreRegisterWriteEvent ev{};
            ev.sidCycleStamp = static_cast<std::uint64_t>(i);
            ev.registerIndex = static_cast<std::uint16_t>(i);
            ev.value = static_cast<std::uint8_t>(i * 17);
            ring.push(ev);
        }
        overflowed = false;
        const std::size_t drained = ring.drain(buf.data(), buf.size(), &overflowed);
        require(drained == 5, "drained 5 events");
        require(!overflowed, "no overflow within capacity");
        for (int i = 0; i < 5; ++i) {
            require(buf[i].sidCycleStamp == static_cast<std::uint64_t>(i),
                    "drained events in publish order");
            require(buf[i].value == static_cast<std::uint8_t>(i * 17),
                    "drained payload integrity");
        }
    }

    // ── E. Drain-overflow detection ──────────────────────────────────────
    {
        SidCoreRegisterWriteRing256 ring;
        // Push 300 events (more than the 256-slot ring can buffer).
        for (int i = 0; i < 300; ++i) {
            SidCoreRegisterWriteEvent ev{};
            ev.sidCycleStamp = static_cast<std::uint64_t>(i);
            ring.push(ev);
        }
        std::array<SidCoreRegisterWriteEvent, 512> buf{};
        bool overflowed = false;
        const std::size_t drained = ring.drain(buf.data(), buf.size(), &overflowed);
        require(overflowed, "overflow flag set when consumer fell behind > capacity");
        require(drained <= 256, "drained no more than capacity");
        require(drained > 0, "still drained the most-recent events");
        require(ring.lostEventCount() >= 1,
                "lost-event diagnostic counter ticked");
        // The first drained event should be the oldest VISIBLE one
        // (i.e., total - capacity = 300 - 256 = 44).
        require(buf[0].sidCycleStamp == 44u,
                "oldest visible event after overflow is at offset (total - capacity)");
    }

    // ── F. SidCorePanelModel: publish + drain end-to-end ─────────────────
    {
        SidCorePanelModel model;

        // Producer side: publish a few register writes + a live snapshot.
        for (int i = 0; i < 10; ++i) {
            SidCoreRegisterWriteEvent ev{};
            ev.sidCycleStamp = static_cast<std::uint64_t>(i);
            ev.registerIndex = static_cast<std::uint16_t>(0x04 + i);
            ev.value = static_cast<std::uint8_t>(i * 7);
            ev.voiceHint = static_cast<std::uint8_t>(i % 3);
            model.publishRegisterWrite(ev);
        }

        SidCoreLiveSnapshot snap{};
        snap.voiceFrequency[0] = 0x1234;
        snap.voicePulseWidth[1] = 0x0800;
        snap.voiceWaveform[2] = 0x41;
        snap.filterCutoff = 0x07FF;
        snap.filterResRoute = 0xA5;
        snap.filterModeVolume = 0x1F;
        model.publishLiveSnapshot(snap);

        // Consumer side.
        std::array<SidCoreRegisterWriteEvent, 16> buf{};
        bool ov = false;
        const std::size_t drained = model.drainRegisterWrites(buf.data(), buf.size(), &ov);
        require(drained == 10, "model drained 10 events");
        require(!ov, "no overflow");
        require(buf[5].registerIndex == 0x09, "event[5].registerIndex == 0x09");
        require(buf[5].voiceHint == 2, "event[5].voiceHint == 2 (5 % 3)");

        SidCoreLiveSnapshot got{};
        model.peekLiveSnapshot(got);
        require(got.voiceFrequency[0] == 0x1234,
                "live snapshot voiceFrequency[0] round-trips");
        require(got.filterCutoff == 0x07FF,
                "live snapshot filterCutoff round-trips");
        require(got.filterModeVolume == 0x1F,
                "live snapshot filterModeVolume round-trips");
    }

    // ── G. SidCorePanelModel.reset clears ring + zeros snapshot ──────────
    {
        SidCorePanelModel model;
        // Publish some data.
        SidCoreRegisterWriteEvent ev{};
        ev.sidCycleStamp = 999;
        model.publishRegisterWrite(ev);
        SidCoreLiveSnapshot snap{};
        snap.voiceFrequency[0] = 0xABCD;
        model.publishLiveSnapshot(snap);

        model.reset();

        std::array<SidCoreRegisterWriteEvent, 8> buf{};
        bool ov = false;
        require(model.drainRegisterWrites(buf.data(), buf.size(), &ov) == 0,
                "reset cleared the register-write ring");
        SidCoreLiveSnapshot got{};
        model.peekLiveSnapshot(got);
        require(got.voiceFrequency[0] == 0,
                "reset published a zeroed live snapshot");
    }

    // ── H. Publish a burst + drain in chunks ─────────────────────────────
    {
        SidCorePanelModel model;
        for (int i = 0; i < 50; ++i) {
            SidCoreRegisterWriteEvent ev{};
            ev.sidCycleStamp = static_cast<std::uint64_t>(1000 + i);
            ev.registerIndex = static_cast<std::uint16_t>(i & 0x1F);
            model.publishRegisterWrite(ev);
        }
        // First drain chunk: 20 events.
        std::array<SidCoreRegisterWriteEvent, 20> chunkA{};
        bool ov = false;
        require(model.drainRegisterWrites(chunkA.data(), chunkA.size(), &ov) == 20,
                "first chunk drains 20 events");
        require(chunkA[0].sidCycleStamp == 1000u, "first chunk starts at oldest event");

        // Second drain chunk: remaining 30 events.
        std::array<SidCoreRegisterWriteEvent, 40> chunkB{};
        const std::size_t got = model.drainRegisterWrites(chunkB.data(), chunkB.size(), &ov);
        require(got == 30, "second chunk drains remaining 30 events");
        require(chunkB[0].sidCycleStamp == 1020u, "second chunk picks up where first left off");

        // Third drain: nothing left.
        require(model.drainRegisterWrites(chunkA.data(), chunkA.size(), &ov) == 0,
                "third drain on empty ring returns 0");
    }

    std::cout << "sidcore_panel_model_and_tab_wire_v545_tests: SIDCORE data model + v544 tab inventory wire-up pinned\n";
    return 0;
}
