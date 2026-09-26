// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// sidcore_feed_v550_tests.cpp — Contract tests for the v550 SIDCORE register-write feed.
//
// Tests:
// Section I — SidCoreRegisterWriteEvent layout pinning (16 bytes, trivially copyable)
// Section II — SidCoreLiveSnapshot layout pinning (32 bytes, trivially copyable)
// Section III — SidCoreEventFlag namespace constants
// Section IV — Ring push/drain round-trip (single-thread)
// Section V — Ring overflow detection (lostEventCount)
// Section VI — SidCorePanelModel publish/peek round-trip
// Section VII — SidCorePanelModel reset clears ring + publishes zeroed snapshot

#include "arpsid/gui/sidcore_panel_model.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <type_traits>

using namespace ArpSID::GUI;

// ─── Section I: SidCoreRegisterWriteEvent layout ─────────────────────────────

static_assert(std::is_trivially_copyable<SidCoreRegisterWriteEvent>::value,
              "SidCoreRegisterWriteEvent must be trivially copyable");
static_assert(sizeof(SidCoreRegisterWriteEvent) == 16,
              "SidCoreRegisterWriteEvent must be 16 bytes");

// ─── Section II: SidCoreLiveSnapshot layout ──────────────────────────────────

static_assert(std::is_trivially_copyable<SidCoreLiveSnapshot>::value,
              "SidCoreLiveSnapshot must be trivially copyable");
static_assert(sizeof(SidCoreLiveSnapshot) == 32,
              "SidCoreLiveSnapshot must be 32 bytes");

// ─── Section III: Event flag constants ───────────────────────────────────────

static_assert(SidCoreEventFlag::kGateOn         == 0x01u, "kGateOn");
static_assert(SidCoreEventFlag::kGateOff        == 0x02u, "kGateOff");
static_assert(SidCoreEventFlag::kHardRestart    == 0x04u, "kHardRestart");
static_assert(SidCoreEventFlag::kFilterMode     == 0x08u, "kFilterMode");
static_assert(SidCoreEventFlag::kWaveformChange == 0x10u, "kWaveformChange");

// ─── Section IV: Ring push/drain round-trip ───────────────────────────────────

static void testRingPushDrain() {
    SidCoreRegisterWriteRing<16> ring;
    assert(ring.headForTesting() == 0u);
    assert(ring.tailForTesting() == 0u);

    // Push 5 events.
    for (uint8_t i = 0; i < 5u; ++i) {
        SidCoreRegisterWriteEvent ev{};
        ev.sidCycleStamp  = (uint64_t)i * 100u;
        ev.registerIndex  = i;
        ev.value          = (uint8_t)(i * 10u);
        ev.voiceHint      = (i <= 6u) ? 0u : 0xFFu;
        ring.push(ev);
    }
    assert(ring.headForTesting() == 5u);

    // Drain all 5.
    SidCoreRegisterWriteEvent out[16]{};
    bool overflowed = true;
    std::size_t n = ring.drain(out, 16, &overflowed);
    if (n != 5u) std::abort();
    assert(!overflowed);
    assert(ring.tailForTesting() == 5u);

    for (std::size_t i = 0; i < 5u; ++i) {
        assert(out[i].sidCycleStamp == (uint64_t)i * 100u);
        assert(out[i].registerIndex == (uint16_t)i);
        assert(out[i].value == (uint8_t)(i * 10u));
    }

    // Second drain returns 0.
    n = ring.drain(out, 16, &overflowed);
    if (n != 0u) std::abort();
    assert(!overflowed);

    std::puts("  IV: ring push/drain round-trip — OK");
}

// ─── Section V: Ring overflow detection ──────────────────────────────────────

static void testRingOverflow() {
    SidCoreRegisterWriteRing<16> ring;

    // Push 17 events into a capacity-16 ring (consumer hasn't drained).
    for (uint8_t i = 0; i < 17u; ++i) {
        SidCoreRegisterWriteEvent ev{};
        ev.registerIndex = i;
        ring.push(ev);
    }

    // Drain: consumer falls more than kCapacity behind → overflow.
    SidCoreRegisterWriteEvent out[32]{};
    bool overflowed = false;
    const std::size_t n = ring.drain(out, 32, &overflowed);
    assert(overflowed);
    // We get at most kCapacity events.
    if (n != 16u) std::abort();
    // lostEventCount_ increments once per overflow detection.
    assert(ring.lostEventCount() == 1u);

    std::puts("  V: ring overflow detection — OK");
}

// ─── Section VI: SidCorePanelModel publish/peek round-trip ───────────────────

static void testPanelModelRoundTrip() {
    SidCorePanelModel model;

    // Publish 3 register writes.
    for (uint8_t i = 0; i < 3u; ++i) {
        SidCoreRegisterWriteEvent ev{};
        ev.sidCycleStamp = (uint64_t)i;
        ev.registerIndex = i;
        ev.value         = (uint8_t)(i + 10u);
        model.publishRegisterWrite(ev);
    }

    // Drain them back.
    SidCoreRegisterWriteEvent out[8]{};
    bool overflowed = false;
    std::size_t n = model.drainRegisterWrites(out, 8, &overflowed);
    if (n != 3u) std::abort();
    assert(!overflowed);
    for (std::size_t i = 0; i < 3u; ++i) {
        assert(out[i].registerIndex == (uint16_t)i);
        assert(out[i].value == (uint8_t)(i + 10u));
    }

    // Publish a live snapshot and peek it back.
    SidCoreLiveSnapshot snap{};
    snap.voiceFrequency[0]  = 0x1234u;
    snap.voiceFrequency[1]  = 0x5678u;
    snap.voiceFrequency[2]  = 0x9ABCu;
    snap.voiceWaveform[0]   = 0x21u;  // sawtooth + gate
    snap.filterCutoff       = 0x07FFu;
    snap.filterModeVolume   = 0x1Fu;
    model.publishLiveSnapshot(snap);

    SidCoreLiveSnapshot peeked{};
    model.peekLiveSnapshot(peeked);
    assert(peeked.voiceFrequency[0] == 0x1234u);
    assert(peeked.voiceFrequency[1] == 0x5678u);
    assert(peeked.voiceFrequency[2] == 0x9ABCu);
    assert(peeked.voiceWaveform[0]  == 0x21u);
    assert(peeked.filterCutoff      == 0x07FFu);
    assert(peeked.filterModeVolume  == 0x1Fu);

    std::puts("  VI: SidCorePanelModel publish/peek round-trip — OK");
}

// ─── Section VII: SidCorePanelModel reset ────────────────────────────────────

static void testPanelModelReset() {
    SidCorePanelModel model;

    // Push 4 writes, then reset.
    for (uint8_t i = 0; i < 4u; ++i) {
        SidCoreRegisterWriteEvent ev{};
        ev.registerIndex = i;
        model.publishRegisterWrite(ev);
    }

    // Publish a non-zero snapshot.
    SidCoreLiveSnapshot snap{};
    snap.voiceFrequency[0] = 0xBEEFu;
    model.publishLiveSnapshot(snap);

    model.reset();

    // Ring should be empty after reset.
    SidCoreRegisterWriteEvent out[8]{};
    std::size_t n = model.drainRegisterWrites(out, 8, nullptr);
    if (n != 0u) std::abort();

    // Snapshot should be zeroed after reset.
    SidCoreLiveSnapshot peeked{};
    peeked.voiceFrequency[0] = 0xDEADu;  // sentinel
    model.peekLiveSnapshot(peeked);
    assert(peeked.voiceFrequency[0] == 0u);

    std::puts("  VII: SidCorePanelModel reset — OK");
}

int main() {
    std::puts("sidcore_feed_v550_tests");
    std::puts("  I:   static_assert SidCoreRegisterWriteEvent layout — OK");
    std::puts("  II:  static_assert SidCoreLiveSnapshot layout — OK");
    std::puts("  III: static_assert SidCoreEventFlag constants — OK");
    testRingPushDrain();
    testRingOverflow();
    testPanelModelRoundTrip();
    testPanelModelReset();
    std::puts("ALL PASS");
    return 0;
}
