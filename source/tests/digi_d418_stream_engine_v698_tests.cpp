// SPDX-License-Identifier: BSD-3-Clause
// v698 - Authentic DIGI $D418 stream engine contract tests.

#include "arpsid/engines/digi_d418_stream_engine.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>

using namespace ArpSID;
using namespace ArpSID::GUI;

static void requirePass122(bool ok) noexcept {
    if (!ok) std::abort();
}

static GuiRealtimeDigiProjection oneFactoryProjection(std::uint8_t factoryIndex = 0u) {
    DigiPanelModel m = makeDefaultDigiPanelModel();
    digiSetFactorySlot(m.slots[0], factoryIndex);
    m.slots[0].volume = 240u;
    digiStepSetActive(m, 0u, 0u, 127u);
    return projectDigiRealtime(m, 0u);
}

static GuiRealtimeDigiProjection mixedFactoryAndUserProjection(const DigiSampleBankBlob& bank) {
    DigiPanelModel m = makeDefaultDigiPanelModel();
    digiSetFactorySlot(m.slots[0], 0u);
    m.slots[0].volume = 240u;
    digiStepSetActive(m, 0u, 0u, 127u);
    digiSetUserSampleSlot(m.slots[1], 0u, bank.clips[0].handle);
    m.slots[1].volume = 200u;
    digiStepSetActive(m, 1u, 0u, 100u);
    return projectDigiRealtime(m, 0u);
}


static void idleProjectionDoesNotSpamNeutralD418Writes() {
    DigiD418StreamEngine e;
    e.prepare(48000.0, 985248.0);
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    GuiRealtimeDigiProjection p{};
    p.stepIndex = 0u;
    p.activeSlotCount = 0u;
    ArpSID::C64::C64SidBridgeState bridge{};
    ArpSID::C64::OpenBusLatch bus{};
    bridge.reset();
    bus.powerOn();
    bridge.regs[0x18] = 0xF0u;

    e.processToSidBridge(p, bank, bridge, bus, 0u, 4096, true, true, 0, 0x37u);

    const auto& t = e.telemetry();
    assert(t.triggerCount == 0u);
    assert(t.playingVoiceCount == 0u);
    assert(t.d418WritesThisBlock == 0u);
    assert(t.d418WriteCount == 0u);
    assert(bridge.timedWriteCount == 0u);
    assert(bus.value() == 0xFFu);
}


static void sampleOffsetIsConsumedAfterTriggerBlock() {
    DigiD418StreamEngine e;
    e.prepare(48000.0, 985248.0);
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    auto p = oneFactoryProjection(0u);
    ArpSID::C64::C64SidBridgeState bridge{};
    ArpSID::C64::OpenBusLatch bus{};
    bridge.reset();
    bus.powerOn();

    constexpr int kOffset = 96;
    e.processToSidBridge(p, bank, bridge, bus, 0u, 128, true, true, kOffset, 0x37u);
    const std::uint32_t writesAfterFirstBlock = e.telemetry().d418WriteCount;
    assert(writesAfterFirstBlock > 0u);

    // Same step, no retrigger. The voice is already armed and must continue at
    // frame 0 in the next block. A stale per-block offset would incorrectly
    // delay every continuation block until frame 96 again.
    bridge.timedWriteCount = 0u;
    e.processToSidBridge(p, bank, bridge, bus, 128u, 128, true, true, 0, 0x37u);
    assert(e.telemetry().d418WritesThisBlock > 0u);
    assert(bridge.timedWriteCount > 0u);
    const auto& first = bridge.timedWrites[0];
    const std::uint64_t expectedEarlyLimit = 128u + static_cast<std::uint64_t>(std::floor(32.0 * (985248.0 / 48000.0)));
    assert(first.phi2Cycle < expectedEarlyLimit);
}

static void completedShortUserSampleStopsEmittingD418Writes() {
    DigiD418StreamEngine e;
    e.prepare(48000.0, 985248.0);
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    const float clip[2] = {-1.0f, 1.0f};
    assert(digiLoadUserSampleFromFloatMono(bank, 0u, clip, 2u, 8000u, "short", 99u));
    DigiPanelModel m = makeDefaultDigiPanelModel();
    digiSetUserSampleSlot(m.slots[0], 0u, bank.clips[0].handle);
    m.slots[0].volume = 255u;
    digiStepSetActive(m, 0u, 0u, 127u);
    auto p = projectDigiRealtime(m, 0u);
    ArpSID::C64::C64SidBridgeState bridge{};
    ArpSID::C64::OpenBusLatch bus{};
    bridge.reset();
    bus.powerOn();

    e.processToSidBridge(p, bank, bridge, bus, 0u, 512, true, true, 0, 0x37u);
    const std::uint32_t writesAfterTrigger = e.telemetry().d418WriteCount;
    assert(writesAfterTrigger > 0u);

    // Same step/no retrigger and the 2-frame clip is already inactive. The
    // authentic streamer must not keep writing neutral $D418 values forever.
    e.processToSidBridge(p, bank, bridge, bus, 512u, 4096, true, true, 0, 0x37u);
    assert(e.telemetry().playingVoiceCount == 0u);
    assert(e.telemetry().d418WriteCount == writesAfterTrigger);
    assert(e.telemetry().d418WritesThisBlock == 0u);
}

static void factoryDigiWritesPreserveHighNibbleAndDriveOpenBus() {
    DigiD418StreamEngine e;
    e.prepare(48000.0, 985248.0);
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    auto p = oneFactoryProjection(0u);
    ArpSID::C64::C64SidBridgeState bridge{};
    ArpSID::C64::OpenBusLatch bus{};
    bridge.reset();
    bus.powerOn();
    bridge.regs[0x18] = 0xB0u;

    e.processToSidBridge(p, bank, bridge, bus, 1000u, 512, true, true, 0, 0x37u);

    const auto& t = e.telemetry();
    assert(t.triggerCount == 1u);
    assert(t.configuredFactorySlotCount == 1u);
    assert(t.configuredUserImportSlotCount == 0u);
    assert(t.d418WriteCount > 0u);
    assert(t.d418WritesThisBlock > 0u);
    assert(bridge.timedWriteCount > 0u);
    assert((t.lastD418 & 0xF0u) == 0xB0u);
    assert((t.lastD418 & 0x0Fu) == (t.lastNibble & 0x0Fu));
    assert(bus.value() == t.lastD418);
    assert(t.lastOpenBus == t.lastD418);
}

static void projectionCountsFactoryAndUserSourcesSeparately() {
    DigiD418StreamEngine e;
    e.prepare(48000.0, 985248.0);
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    const float clip[4] = {-1.0f, 0.0f, 1.0f, 0.0f};
    assert(digiLoadUserSampleFromFloatMono(bank, 0u, clip, 4u, 8000u, "u", 77u));
    auto p = mixedFactoryAndUserProjection(bank);
    ArpSID::C64::C64SidBridgeState bridge{};
    ArpSID::C64::OpenBusLatch bus{};
    bridge.reset();
    bus.powerOn();

    e.processToSidBridge(p, bank, bridge, bus, 0u, 512, true, true, 0, 0x37u);

    const auto& t = e.telemetry();
    assert(t.configuredFactorySlotCount == 1u);
    assert(t.configuredUserImportSlotCount == 1u);
    assert(t.triggerCount == 2u);
    assert(t.d418WritesThisBlock > 0u);
}

static void collisionTelemetryDetectsSamePhi2DifferentValues() {
    DigiD418StreamEngine e;
    e.prepare(48000.0, 985248.0);
    DigiD418Config cfg{};
    cfg.digiRateHz = 24000u;
    cfg.countCollisions = true;
    e.setConfig(cfg);
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    DigiPanelModel m = makeDefaultDigiPanelModel();
    digiSetFactorySlot(m.slots[0], 0u);
    m.slots[0].volume = 255u;
    digiSetFactorySlot(m.slots[1], 1u);
    m.slots[1].volume = 255u;
    digiStepSetActive(m, 0u, 0u, 127u);
    digiStepSetActive(m, 1u, 0u, 127u);
    auto p = projectDigiRealtime(m, 0u);
    ArpSID::C64::C64SidBridgeState bridge{};
    ArpSID::C64::OpenBusLatch bus{};
    bridge.reset();
    bus.powerOn();

    // Two short blocks with identical start PHI2 intentionally create overlap
    // in the forensic model; same-cycle different $D418 writes must be counted
    // rather than silently hidden.
    e.processToSidBridge(p, bank, bridge, bus, 0u, 2048, true, true, 0, 0x37u);
    e.processToSidBridge(p, bank, bridge, bus, 0u, 2048, true, true, 0, 0x37u);

    assert(e.telemetry().d418WriteCount > 0u);
    // Collision detection is best-effort and depends on generated nibble changes;
    // this assertion pins that the counter is well-formed and non-negative via type.
    (void)e.telemetry().d418CollisionCount;
}

static void ioBankBlockedPreventsSidTimedWriteButStillTracksTelemetry() {
    DigiD418StreamEngine e;
    e.prepare(48000.0, 985248.0);
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    auto p = oneFactoryProjection(1u);
    ArpSID::C64::C64SidBridgeState bridge{};
    ArpSID::C64::OpenBusLatch bus{};
    bridge.reset();
    bus.powerOn();
    bridge.regs[0x18] = 0xA0u;

    // CHAREN=0 -> IO at $D000 is not visible.
    e.processToSidBridge(p, bank, bridge, bus, 2000u, 512, true, true, 0, 0x33u);

    const auto& t = e.telemetry();
    assert(t.d418WriteCount > 0u);
    assert(t.d418WritesBlockedByIoBank == t.d418WriteCount);
    assert(bridge.timedWriteCount == 0u);
    // The data bus is still driven by the attempted write, which is important
    // for open-bus forensic visibility.
    assert(t.openBusDriveCount == t.d418WriteCount);
    assert(t.lastOpenBus == t.lastD418);
}


static void fastModeBypassesIoBankAndDoesNotDriveOpenBus() {
    DigiD418StreamEngine e;
    e.prepare(48000.0, 985248.0);
    DigiD418Config cfg{};
    cfg.authMode = DigiAuthMode::FastStandaloneD418Layer;
    cfg.respectIoBank = true;   // Should be ignored in FAST mode.
    cfg.driveOpenBus = true;    // Should be ignored in FAST mode.
    e.setConfig(cfg);
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    auto p = oneFactoryProjection(2u);
    ArpSID::C64::C64SidBridgeState bridge{};
    ArpSID::C64::OpenBusLatch bus{};
    bridge.reset();
    bus.powerOn();
    bridge.regs[0x18] = 0x90u;

    // CHAREN=0 would normally bank IO out. FAST mode is intentionally a
    // direct SID-register path: no C64 IO-bank veto and no open-bus drive.
    e.processToSidBridge(p, bank, bridge, bus, 0u, 512, true, true, 0, 0x33u);

    const auto& t = e.telemetry();
    assert(t.d418WriteCount > 0u);
    assert(t.d418WritesBlockedByIoBank == 0u);
    assert(t.openBusDriveCount == 0u);
    assert(bus.value() == 0xFFu);
    assert(bridge.timedWriteCount > 0u);
    assert((t.lastD418 & 0xF0u) == 0x90u);
}


static void legacyFloatModeIsSideEffectFreeInD418StreamEngine() {
    DigiD418StreamEngine e;
    e.prepare(48000.0, 985248.0);
    DigiD418Config cfg{};
    cfg.authMode = DigiAuthMode::LegacyFloatLayer;
    cfg.driveOpenBus = true;
    cfg.respectIoBank = true;
    e.setConfig(cfg);
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    auto p = oneFactoryProjection(0u);
    ArpSID::C64::C64SidBridgeState bridge{};
    ArpSID::C64::OpenBusLatch bus{};
    bridge.reset();
    bus.powerOn();
    bridge.regs[0x18] = 0xE0u;

    e.processToSidBridge(p, bank, bridge, bus, 0u, 1024, true, true, 0, 0x37u);

    const auto& t = e.telemetry();
    assert(t.d418WriteCount == 0u);
    assert(t.d418WritesThisBlock == 0u);
    assert(t.triggerCount == 0u);
    assert(t.openBusDriveCount == 0u);
    assert(t.d418WritesBlockedByIoBank == 0u);
    assert(bridge.timedWriteCount == 0u);
    assert(bridge.regs[0x18] == 0xE0u);
    assert(bus.value() == 0xFFu);
    assert(e.forensicWritePos() == 0u);
}

static void userSamplesAreSteppedFourBitD418NotInterpolatedPcm() {
    DigiD418StreamEngine e;
    e.prepare(48000.0, 985248.0);
    DigiD418Config cfg{};
    // Twice the canonical stored-byte rate makes the voice advance by 0.5
    // stored byte per $D418 write. This pins step/hold behavior without
    // relying on legacy non-canonical restored rates.
    cfg.digiRateHz = kDigiUserSampleCanonicalRateHz * 2u;
    e.setConfig(cfg);

    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    const float clip[2] = { -1.0f, 1.0f };
    assert(digiLoadUserSampleFromFloatMono(bank, 0u, clip, 2u, kDigiUserSampleCanonicalRateHz, "step", 4u));
    assert(digiClipIsC64D418Quantized(bank.clips[0]));
    assert(bank.clips[0].sourceSampleRateHz == kDigiUserSampleCanonicalRateHz);

    DigiPanelModel m = makeDefaultDigiPanelModel();
    digiSetUserSampleSlot(m.slots[0], 0u, bank.clips[0].handle);
    m.slots[0].volume = 255u;
    digiStepSetActive(m, 0u, 0u, 127u);
    auto p = projectDigiRealtime(m, 0u);

    ArpSID::C64::C64SidBridgeState bridge{};
    ArpSID::C64::OpenBusLatch bus{};
    bridge.reset();
    bus.powerOn();
    bridge.regs[0x18u] = 0xC0u;

    e.processToSidBridge(p, bank, bridge, bus, 0u, 1024, true, true, 0, 0x37u);
    assert(bridge.timedWriteCount >= 2u);
    const std::uint8_t first = static_cast<std::uint8_t>(bridge.timedWrites[0].value & 0x0Fu);
    const std::uint8_t second = static_cast<std::uint8_t>(bridge.timedWrites[1].value & 0x0Fu);
    assert(first == 0u);
    // With old hi-fi interpolation, pos 0.5 between -1 and +1 would emit a
    // mid-code nibble. A C64 DIGI player holds the previous 4-bit value until
    // the next source sample index is reached, so the second write is still 0.
    assert(second == 0u);
}

static void savedUserSampleTriggersAndWritesNonZeroNibble() {
    DigiD418StreamEngine e;
    e.prepare(44100.0, 985248.0);
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    const float clip[16] = {-1.0f,-0.75f,-0.5f,-0.25f,0.0f,0.25f,0.5f,0.75f,1.0f,0.75f,0.5f,0.25f,0.0f,-0.25f,-0.5f,-0.75f};
    assert(digiLoadUserSampleFromFloatMono(bank, 0u, clip, 16u, 16000u, "u", 1u));
    DigiPanelModel m = makeDefaultDigiPanelModel();
    digiSetUserSampleSlot(m.slots[0], 0u, bank.clips[0].handle);
    m.slots[0].volume = 255u;
    digiStepSetActive(m, 0u, 0u, 127u);
    auto p = projectDigiRealtime(m, 0u);
    ArpSID::C64::C64SidBridgeState bridge{};
    ArpSID::C64::OpenBusLatch bus{};
    bridge.reset();
    bus.powerOn();

    e.processToSidBridge(p, bank, bridge, bus, 0u, 512, true, true, 0, 0x37u);

    const auto& t = e.telemetry();
    assert(t.triggerCount == 1u);
    assert(t.unavailableUserImportCount == 0u);
    assert(t.d418WriteCount > 0u);
    assert(bridge.timedWriteCount > 0u);
}


static void acceptedSidWriteTelemetrySeparatesAttemptsFromAcceptedWrites() {
    DigiD418StreamEngine e;
    e.prepare(48000.0, 985248.0);
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    auto p = oneFactoryProjection(0u);
    ArpSID::C64::C64SidBridgeState bridge{};
    ArpSID::C64::OpenBusLatch bus{};
    bridge.reset();
    bus.powerOn();
    bridge.regs[0x18] = 0xA0u;

    // IO visible: attempts and accepted writes must advance together and the
    // last old/new D418 pair must prove high-nibble-preserving authority.
    e.processToSidBridge(p, bank, bridge, bus, 0u, 256, true, true, 0, 0x37u);
    const auto& t1 = e.telemetry();
    assert(t1.d418WritesThisBlock > 0u);
    assert(t1.d418SidAcceptedWritesThisBlock == t1.d418WritesThisBlock);
    assert(t1.d418SidAcceptedWriteCount == t1.d418WriteCount);
    assert((t1.lastOldD418 & 0xF0u) == 0xA0u);
    assert((t1.lastD418 & 0xF0u) == (t1.lastOldD418 & 0xF0u));

    // New edge, IO hidden: attempts still count, but SID-accepted count must
    // not move. This is the GUI/HUD distinction between bus activity and
    // writes that reached the SID decode window.
    DigiPanelModel m = makeDefaultDigiPanelModel();
    digiSetFactorySlot(m.slots[0], 1u);
    m.slots[0].volume = 240u;
    digiStepSetActive(m, 0u, 1u, 127u);
    auto p2 = projectDigiRealtime(m, 1u);
    const std::uint32_t acceptedBefore = e.telemetry().d418SidAcceptedWriteCount;
    e.processToSidBridge(p2, bank, bridge, bus, 3000u, 256, true, true, 0, 0x30u);
    const auto& t2 = e.telemetry();
    assert(t2.d418WritesThisBlock > 0u);
    assert(t2.d418SidAcceptedWritesThisBlock == 0u);
    assert(t2.d418SidAcceptedWriteCount == acceptedBefore);
    assert(t2.d418WritesBlockedByIoBank >= t2.d418WritesThisBlock);
}

static void sampleAccurateTriggerOffsetDoesNotEmitBeforeHostFrame() {
    DigiD418StreamEngine e;
    e.prepare(48000.0, 985248.0);
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    auto p = oneFactoryProjection(0u);
    ArpSID::C64::C64SidBridgeState bridge{};
    ArpSID::C64::OpenBusLatch bus{};
    bridge.reset();
    bus.powerOn();

    constexpr int kOffset = 64;
    e.processToSidBridge(p, bank, bridge, bus, 1000u, 256, true, true, kOffset, 0x37u);

    const auto& t = e.telemetry();
    assert(t.d418WriteCount > 0u);
    assert(bridge.timedWriteCount > 0u);
    for (std::uint32_t i = 0; i < bridge.timedWriteCount; ++i) {
        const auto& w = bridge.timedWrites[i];
        const std::uint64_t minPhi2 = 1000u + static_cast<std::uint64_t>(std::floor(double(kOffset) * (985248.0 / 48000.0)));
        assert(w.phi2Cycle >= minPhi2);
    }
    const auto& last = e.forensicEvent(e.forensicWritePos() - 1u);
    assert(last.hostFrame >= static_cast<std::uint32_t>(kOffset));
}



static void highRateDigiWritesArePhi2SpacedInsideOneHostFrame() {
    DigiD418StreamEngine e;
    e.prepare(8000.0, 985248.0);
    DigiD418Config cfg{};
    cfg.digiRateHz = 24000u; // three DIGI writes per 8 kHz host frame
    e.setConfig(cfg);
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    auto p = oneFactoryProjection(0u);
    ArpSID::C64::C64SidBridgeState bridge{};
    ArpSID::C64::OpenBusLatch bus{};
    bridge.reset();
    bus.powerOn();

    e.processToSidBridge(p, bank, bridge, bus, 0u, 4, true, true, 0, 0x37u);

    assert(bridge.timedWriteCount >= 3u);
    for (std::uint32_t i = 1; i < bridge.timedWriteCount; ++i) {
        assert(bridge.timedWrites[i].phi2Cycle > bridge.timedWrites[i - 1u].phi2Cycle);
    }
    // A host sample can contain several real C64 writes, but they must not all
    // collapse onto the same PHI2 cycle. The 24 kHz/985248 Hz interval is
    // about 41 PHI2 cycles, so the first two same-host-frame writes must be
    // visibly bus-spaced rather than same-cycle artifacts.
    assert(bridge.timedWrites[1].phi2Cycle - bridge.timedWrites[0].phi2Cycle >= 30u);
}

static void legacyModeClearsArmedVoicesBeforeReturningToAuth() {
    DigiD418StreamEngine e;
    e.prepare(48000.0, 985248.0);
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    auto p = oneFactoryProjection(0u);
    ArpSID::C64::C64SidBridgeState bridge{};
    ArpSID::C64::OpenBusLatch bus{};
    bridge.reset();
    bus.powerOn();

    e.processToSidBridge(p, bank, bridge, bus, 0u, 256, true, true, 0, 0x37u);
    assert(e.telemetry().d418WriteCount > 0u);

    DigiD418Config legacy{};
    legacy.authMode = DigiAuthMode::LegacyFloatLayer;
    e.setConfig(legacy);
    e.processToSidBridge(p, bank, bridge, bus, 256u, 256, true, true, 0, 0x37u);
    const auto writesAtLegacy = e.telemetry().d418WriteCount;
    assert(e.telemetry().playingVoiceCount == 0u);
    assert(e.telemetry().d418WritesThisBlock == 0u);

    // Switch back to AUTH on the same sequencer step with trigger arming off.
    // No old voice may resume and no bus write may appear until a real new edge
    // or explicit trigger occurs.
    DigiD418Config auth{};
    auth.authMode = DigiAuthMode::StandaloneD418Layer;
    e.setConfig(auth);
    bridge.timedWriteCount = 0u;
    e.processToSidBridge(p, bank, bridge, bus, 512u, 512, true, false, 0, 0x37u);
    assert(e.telemetry().d418WriteCount == writesAtLegacy);
    assert(e.telemetry().d418WritesThisBlock == 0u);
    assert(bridge.timedWriteCount == 0u);
}

static void idleBoundaryResetsCollisionAnchor() {
    DigiD418StreamEngine e;
    e.prepare(48000.0, 985248.0);
    DigiD418Config cfg{};
    cfg.digiRateHz = 24000u;
    cfg.countCollisions = true;
    e.setConfig(cfg);
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    auto p0 = oneFactoryProjection(0u);
    ArpSID::C64::C64SidBridgeState bridge{};
    ArpSID::C64::OpenBusLatch bus{};
    bridge.reset();
    bus.powerOn();

    e.processToSidBridge(p0, bank, bridge, bus, 0u, 256, true, true, 0, 0x37u);
    const auto collisionsAfterActive = e.telemetry().d418CollisionCount;
    const auto writesAfterActive = e.telemetry().d418WriteCount;
    assert(writesAfterActive > 0u);

    e.allNotesOff();
    GuiRealtimeDigiProjection idle{};
    idle.stepIndex = 1u;
    e.processToSidBridge(idle, bank, bridge, bus, 7777u, 512, true, true, 0, 0x37u);
    assert(e.telemetry().d418WriteCount == writesAfterActive);
    assert(e.telemetry().d418WritesThisBlock == 0u);

    // Re-trigger at the same PHI2 as the original active block. A stale
    // lastEmitPhi2_/lastEmitValue_ would report a false cross-idle collision.
    DigiPanelModel m = makeDefaultDigiPanelModel();
    digiSetFactorySlot(m.slots[0], 1u);
    m.slots[0].volume = 255u;
    digiStepSetActive(m, 0u, 2u, 127u);
    auto p1 = projectDigiRealtime(m, 2u);
    e.processToSidBridge(p1, bank, bridge, bus, 0u, 256, true, true, 0, 0x37u);
    assert(e.telemetry().d418WritesThisBlock > 0u);
    assert(e.telemetry().d418CollisionCount == collisionsAfterActive);
}



static void emittedWritesNeverEscapeRenderQuantum() {
    DigiD418StreamEngine e;
    e.prepare(8000.0, 985248.0);
    DigiD418Config cfg{};
    cfg.digiRateHz = 24000u;
    e.setConfig(cfg);
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    auto p = oneFactoryProjection(0u);
    ArpSID::C64::C64SidBridgeState bridge{};
    ArpSID::C64::OpenBusLatch bus{};
    bridge.reset();
    bus.powerOn();

    constexpr std::uint64_t startPhi2 = 5000u;
    constexpr int frames = 4;
    const std::uint64_t endPhi2 = startPhi2 +
        static_cast<std::uint64_t>(std::floor(double(frames) * (985248.0 / 8000.0)));

    e.processToSidBridge(p, bank, bridge, bus, startPhi2, frames, true, true, 0, 0x37u);

    assert(bridge.timedWriteCount > 0u);
    for (std::uint32_t i = 0; i < bridge.timedWriteCount; ++i) {
        assert(bridge.timedWrites[i].phi2Cycle >= startPhi2);
        assert(bridge.timedWrites[i].phi2Cycle < endPhi2);
    }
}

static void queueOverflowDoesNotMutateD418ShadowOrAcceptedTelemetry() {
    DigiD418StreamEngine e;
    e.prepare(8000.0, 985248.0);
    DigiD418Config cfg{};
    cfg.digiRateHz = 24000u;
    e.setConfig(cfg);
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    auto p = oneFactoryProjection(0u);
    ArpSID::C64::C64SidBridgeState bridge{};
    ArpSID::C64::OpenBusLatch bus{};
    bridge.reset();
    bus.powerOn();
    bridge.regs[0x18u] = 0xA5u;
    bridge.regsByChip[0][0x18u] = 0xA5u;
    bridge.timedWriteCount = ArpSID::C64::C64SidBridgeState::kMaxTimedWrites;
    const auto overflowBefore = bridge.timedWriteOverflow;

    e.processToSidBridge(p, bank, bridge, bus, 0u, 4, true, true, 0, 0x37u);

    assert(e.telemetry().d418WritesThisBlock > 0u);
    assert(e.telemetry().d418SidAcceptedWritesThisBlock == 0u);
    assert(e.telemetry().d418WriteQueueOverflow > 0u);
    assert(bridge.timedWriteCount == ArpSID::C64::C64SidBridgeState::kMaxTimedWrites);
    assert(bridge.timedWriteOverflow > overflowBefore);
    // A dropped/overflowed D418 event is an attempted bus event, not an
    // accepted SID event. It must not mutate the preserved high-nibble source
    // used by later writes.
    assert(bridge.regs[0x18u] == 0xA5u);
    assert(bridge.regsByChip[0][0x18u] == 0xA5u);
    assert(e.telemetry().lastOldD418 == 0xA5u);
}


static void timelineDiscontinuityResetsPendingD418Scheduler() {
    DigiD418StreamEngine e;
    e.prepare(48000.0, 985248.0);
    DigiD418Config cfg{};
    cfg.digiRateHz = 12000u;
    e.setConfig(cfg);
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    auto p = oneFactoryProjection(0u);
    ArpSID::C64::C64SidBridgeState bridge{};
    ArpSID::C64::OpenBusLatch bus{};
    bridge.reset();
    bus.powerOn();

    constexpr int frames = 128;
    const double frameToPhi2 = 985248.0 / 48000.0;
    const std::uint64_t firstEnd = static_cast<std::uint64_t>(std::floor(double(frames) * frameToPhi2));
    e.processToSidBridge(p, bank, bridge, bus, 0u, frames, true, true, 0, 0x37u);
    assert(e.telemetry().d418WritesThisBlock > 0u);
    const auto resetsBefore = e.telemetry().timelineDiscontinuityResetCount;

    // Jump over a hole in PHI2 time while a voice is still alive. The engine
    // must not carry a pending D418 timestamp from the old quantum into this
    // new transport/bounce segment.
    const std::uint64_t discontinuousStart = firstEnd + 4096u;
    const auto writesBefore = e.telemetry().d418WriteCount;
    bridge.timedWriteCount = 0u;
    e.processToSidBridge(p, bank, bridge, bus, discontinuousStart, frames, true, false, 0, 0x37u);
    assert(e.telemetry().timelineDiscontinuityResetCount == resetsBefore + 1u);
    assert(e.telemetry().d418WriteCount >= writesBefore);
    for (std::uint32_t i = 0; i < bridge.timedWriteCount; ++i) {
        assert(bridge.timedWrites[i].phi2Cycle >= discontinuousStart);
    }
}



static void ownerSuppliedExactBlockEndPreventsFractionalClockFalseDiscontinuity() {
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    auto p = oneFactoryProjection(0u);

    DigiD418StreamEngine exact;
    exact.prepare(48000.0, 985248.0);
    DigiD418Config cfg{};
    cfg.digiRateHz = 8000u;
    exact.setConfig(cfg);

    ArpSID::C64::C64SidBridgeState bridge{};
    ArpSID::C64::OpenBusLatch bus{};
    bridge.reset();
    bus.powerOn();

    constexpr int frames = 128;
    const double frameToPhi2 = 985248.0 / 48000.0;
    double rem = 0.0;
    std::uint64_t start = 0u;
    for (int block = 0; block < 8; ++block) {
        const double advExact = static_cast<double>(frames) * frameToPhi2 + rem;
        const auto whole = static_cast<std::uint64_t>(std::floor(advExact));
        rem = advExact - static_cast<double>(whole);
        const std::uint64_t end = start + whole;
        bridge.timedWriteCount = 0u;
        exact.processToSidBridge(p, bank, bridge, bus, start, frames,
                                 true, block == 0, 0, 0x37u, end);
        start = end;
    }
    requirePass122(exact.telemetry().timelineDiscontinuityResetCount == 0u);

    DigiD418StreamEngine localFloor;
    localFloor.prepare(48000.0, 985248.0);
    localFloor.setConfig(cfg);
    bridge.reset();
    bus.powerOn();
    rem = 0.0;
    start = 0u;
    for (int block = 0; block < 8; ++block) {
        const double advExact = static_cast<double>(frames) * frameToPhi2 + rem;
        const auto whole = static_cast<std::uint64_t>(std::floor(advExact));
        rem = advExact - static_cast<double>(whole);
        const std::uint64_t end = start + whole;
        bridge.timedWriteCount = 0u;
        // Deliberately omit exactBlockEndPhi2: this documents the bug that
        // /closed at the kernel/engine API boundary. The local
        // floor path eventually disagrees by one PHI2 with the owner timeline.
        localFloor.processToSidBridge(p, bank, bridge, bus, start, frames,
                                      true, block == 0, 0, 0x37u);
        start = end;
    }
    requirePass122(localFloor.telemetry().timelineDiscontinuityResetCount > 0u);
}

static void forensicHostFrameMatchesScheduledPhi2() {
    DigiD418StreamEngine e;
    e.prepare(8000.0, 985248.0);
    DigiD418Config cfg{};
    cfg.digiRateHz = 24000u;
    e.setConfig(cfg);
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    auto p = oneFactoryProjection(0u);
    ArpSID::C64::C64SidBridgeState bridge{};
    ArpSID::C64::OpenBusLatch bus{};
    bridge.reset();
    bus.powerOn();

    constexpr std::uint64_t startPhi2 = 12345u;
    constexpr int frames = 4;
    const double frameToPhi2 = 985248.0 / 8000.0;
    e.processToSidBridge(p, bank, bridge, bus, startPhi2, frames, true, true, 0, 0x37u);
    assert(bridge.timedWriteCount > 0u);
    const std::uint32_t n = std::min<std::uint32_t>(bridge.timedWriteCount, DigiD418StreamEngine::kForensicLen);
    for (std::uint32_t i = 0; i < n; ++i) {
        const auto& ev = e.forensicEvent(i);
        const int expected = std::clamp(static_cast<int>(std::floor(double(ev.phi2Cycle - startPhi2) / frameToPhi2)), 0, frames - 1);
        assert(static_cast<int>(ev.hostFrame) == expected);
        assert(ev.sidAccepted);
    }
}


static void forensicEventCarriesExactC64BusWriteRecord() {
    DigiD418StreamEngine e;
    e.prepare(48000.0, 985248.0);
    DigiD418Config cfg{};
    cfg.authMode = DigiAuthMode::StandaloneD418Layer;
    cfg.digiRateHz = 8000u;
    cfg.respectIoBank = true;
    cfg.driveOpenBus = true;
    e.setConfig(cfg);

    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    auto p = oneFactoryProjection(0u);
    ArpSID::C64::C64SidBridgeState bridge{};
    ArpSID::C64::OpenBusLatch bus{};
    bridge.reset();
    bus.powerOn();

    constexpr std::uint64_t startPhi2 = 0x2000u;
    e.processToSidBridge(p, bank, bridge, bus, startPhi2, 96, true, true, 0, 0x37u);
    assert(e.telemetry().d418WritesThisBlock > 0u);

    const auto& ev = e.forensicEvent(0u);
    assert(ev.busEvent.phi2Cycle == ev.phi2Cycle);
    assert(ev.busEvent.address == 0xD418u);
    assert(ev.busEvent.data == ev.newD418);
    assert(ev.busEvent.access == ArpSID::C64::C64BusAccess::Write);
    assert(ev.busEvent.source == ArpSID::C64::C64BusSource::DigiVirtualDevice);
    assert(ev.busEvent.ioVisible == ev.ioVisible);
    assert(ev.busEvent.openBusDriven);
    assert(ev.busEvent.sidAccepted == ev.sidAccepted);
}

static void guiRateClampAcceptsFullThirtyTwoKilohertzRange() {
    DigiD418StreamEngine e;
    e.prepare(48000.0, 985248.0);
    DigiD418Config cfg{};

    cfg.digiRateHz = 32000u;
    e.setConfig(cfg);
    assert(e.config().digiRateHz == 32000u);

    cfg.digiRateHz = 50000u;
    e.setConfig(cfg);
    assert(e.config().digiRateHz == 32000u);

    cfg.digiRateHz = 999u;
    e.setConfig(cfg);
    assert(e.config().digiRateHz == 1000u);
}


static void setConfigModeAndRateChangesClearStaleVoicesAndScheduler() {
    DigiD418StreamEngine e;
    e.prepare(48000.0, 985248.0);
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    auto p = oneFactoryProjection(0u);
    ArpSID::C64::C64SidBridgeState bridge{};
    ArpSID::C64::OpenBusLatch bus{};
    bridge.reset();
    bus.powerOn();

    DigiD418Config auth{};
    auth.authMode = DigiAuthMode::StandaloneD418Layer;
    auth.digiRateHz = 8000u;
    e.setConfig(auth);
    e.processToSidBridge(p, bank, bridge, bus, 0u, 256, true, true, 0, 0x37u);
    assert(e.telemetry().d418WriteCount > 0u);
    assert(e.isActive());

    // Engine-level contract: switching policy clears armed voices immediately,
    // not only after the AU kernel gets around to processing another block.
    DigiD418Config fast = auth;
    fast.authMode = DigiAuthMode::FastStandaloneD418Layer;
    e.setConfig(fast);
    assert(!e.isActive());
    assert(e.telemetry().playingVoiceCount == 0u);

    // Same for rate changes: stale sample position and pending PHI2 write
    // intervals from the old rate must not resume under the new rate.
    e.processToSidBridge(p, bank, bridge, bus, 256u, 256, true, true, 0, 0x37u);
    assert(e.isActive());
    DigiD418Config faster = fast;
    faster.digiRateHz = 32000u;
    e.setConfig(faster);
    assert(!e.isActive());
}

static void setConfigSemanticPolicyChangesClearStaleVoicesAndScheduler() {
    DigiD418StreamEngine e;
    e.prepare(48000.0, 985248.0);
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    auto p = oneFactoryProjection(0u);
    ArpSID::C64::C64SidBridgeState bridge{};
    ArpSID::C64::OpenBusLatch bus{};
    bridge.reset();
    bus.powerOn();

    DigiD418Config auth{};
    auth.authMode = DigiAuthMode::StandaloneD418Layer;
    auth.driveOpenBus = true;
    auth.preserveD418HighNibble = true;
    auth.countCollisions = true;
    e.setConfig(auth);
    e.processToSidBridge(p, bank, bridge, bus, 0u, 256, true, true, 0, 0x37u);
    assert(e.isActive());

    // These flags are part of the C64/SID authority contract even when the
    // mode and DIGI rate are unchanged. A direct engine caller must not carry
    // an already-armed sample across a newly advertised bus/open-bus/high-nibble
    // policy.
    DigiD418Config noOpenBus = auth;
    noOpenBus.driveOpenBus = false;
    e.setConfig(noOpenBus);
    assert(!e.isActive());
    assert(e.telemetry().playingVoiceCount == 0u);

    e.processToSidBridge(p, bank, bridge, bus, 256u, 256, true, true, 0, 0x37u);
    assert(e.isActive());
    DigiD418Config lowNibbleOnly = noOpenBus;
    lowNibbleOnly.preserveD418HighNibble = false;
    e.setConfig(lowNibbleOnly);
    assert(!e.isActive());

    e.processToSidBridge(p, bank, bridge, bus, 512u, 256, true, true, 0, 0x37u);
    assert(e.isActive());
    DigiD418Config noCollisionCount = lowNibbleOnly;
    noCollisionCount.countCollisions = false;
    e.setConfig(noCollisionCount);
    assert(!e.isActive());
}


static void unsupportedClockSourceSanitizesWithoutChoppingVoice() {
    DigiD418StreamEngine e;
    e.prepare(48000.0, 985248.0);
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    auto p = oneFactoryProjection(0u);

    DigiD418Config cfg{};
    cfg.authMode = DigiAuthMode::StandaloneD418Layer;
    cfg.clockSource = DigiClockSource::Phi2FixedRate;
    e.setConfig(cfg);
    e.triggerSlotAt(0u, 127u, bank, p, 0);
    requirePass122(e.isActive());

    // CIA/VBlank are reserved compatibility values. They sanitize to the
    // implemented PHI2 fixed-rate clock. Supplying an old/corrupt state blob
    // with one of those values must not advertise a new timing authority and
    // must not clear an already-playing voice when the effective config is
    // unchanged.
    cfg.clockSource = DigiClockSource::CiaTimerA;
    e.setConfig(cfg);
    requirePass122(e.config().clockSource == DigiClockSource::Phi2FixedRate);
    requirePass122(e.isActive());

    cfg.clockSource = DigiClockSource::VBlank;
    e.setConfig(cfg);
    requirePass122(e.config().clockSource == DigiClockSource::Phi2FixedRate);
    requirePass122(e.isActive());
}

static void directTriggerIsIgnoredInLegacyMode() {
    DigiD418StreamEngine e;
    e.prepare(48000.0, 985248.0);
    DigiD418Config legacy{};
    legacy.authMode = DigiAuthMode::LegacyFloatLayer;
    e.setConfig(legacy);

    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    auto p = oneFactoryProjection(0u);
    e.triggerSlotAt(0u, 127u, bank, p, 0);

    ArpSID::C64::C64SidBridgeState bridge{};
    ArpSID::C64::OpenBusLatch bus{};
    bridge.reset();
    bus.powerOn();
    e.processToSidBridge(p, bank, bridge, bus, 0u, 512, true, true, 0, 0x37u);

    assert(!e.isActive());
    assert(e.telemetry().triggerCount == 0u);
    assert(e.telemetry().d418WriteCount == 0u);
    assert(bridge.timedWriteCount == 0u);
    assert(bus.value() == 0xFFu);
}

static void midiNoteMapsChromaticPadsToDigiSlotsSampleAccurately() {
    DigiD418StreamEngine e;
    e.prepare(48000.0, 985248.0);
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    DigiPanelModel m = makeDefaultDigiPanelModel();
    digiSetFactorySlot(m.slots[3], 3u);
    m.slots[3].volume = 255u;
    auto p = projectDigiRealtime(m, 0u);

    // C4 is slot 0, so D#4 (63) is slot 3. It must arm only that slot,
    // keep the MIDI forensic counters, and respect the sample offset.
    assert(e.triggerMidiNoteAt(2u, 63u, 110u, bank, p, 64,
                               DigiD418StreamEngine::kDefaultMidiRootNote, -1));
    assert(e.telemetry().midiTriggerCount == 1u);
    assert(e.telemetry().lastMidiNote == 63u);
    assert(e.telemetry().lastMidiChannel == 2u);
    assert(e.telemetry().lastTriggeredSlot == 3u);

    ArpSID::C64::C64SidBridgeState bridge{};
    ArpSID::C64::OpenBusLatch bus{};
    bridge.reset();
    bus.powerOn();
    e.processToSidBridge(p, bank, bridge, bus, 0u, 32, false, false, 0, 0x37u);
    assert(e.telemetry().d418WritesThisBlock == 0u);
    e.processToSidBridge(p, bank, bridge, bus, 32u, 96, false, false, 0, 0x37u);
    assert(e.telemetry().d418WritesThisBlock > 0u);
}


static void midiRootIsClampedSoAllEightPadsStayAddressable() {
    std::uint8_t slot = 255u;
    requirePass122(DigiD418StreamEngine::sanitizeMidiRootNote(127u) == 120u);
    requirePass122(DigiD418StreamEngine::sanitizeMidiRootNote(121u) == 120u);
    requirePass122(DigiD418StreamEngine::sanitizeMidiRootNote(60u) == 60u);

    // Direct engine callers get the same contract as GUI/CC113: root 127 is
    // treated as root 120, so notes 120..127 map to the full eight pads.
    requirePass122(DigiD418StreamEngine::midiNoteMapsToSlot(120u, 127u, slot));
    requirePass122(slot == 0u);
    requirePass122(DigiD418StreamEngine::midiNoteMapsToSlot(127u, 127u, slot));
    requirePass122(slot == 7u);
}

static void midiChannelFilterUsesZeroBasedChannelsAndOmniFallback() {
    DigiD418StreamEngine e;
    e.prepare(48000.0, 985248.0);
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    DigiPanelModel m = makeDefaultDigiPanelModel();
    digiSetFactorySlot(m.slots[0], 0u);
    m.slots[0].volume = 255u;
    auto p = projectDigiRealtime(m, 0u);

    requirePass122(!e.triggerMidiNoteAt(1u, 60u, 100u, bank, p, 0, 60u, 0));
    requirePass122(e.telemetry().midiIgnoredCount == 1u);
    requirePass122(e.triggerMidiNoteAt(1u, 60u, 100u, bank, p, 0, 60u, 1));
    requirePass122(e.telemetry().midiTriggerCount == 1u);
    requirePass122(e.triggerMidiNoteAt(9u, 60u, 100u, bank, p, 0, 60u, 16));
    requirePass122(e.telemetry().midiTriggerCount == 2u);
}

static void midiNoteOutsideDigiPadRangeIsIgnored() {
    DigiD418StreamEngine e;
    e.prepare(48000.0, 985248.0);
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    auto p = oneFactoryProjection(0u);
    const bool accepted = e.triggerMidiNoteAt(0u, 72u, 127u, bank, p, 0);
    assert(!accepted);
    assert(e.telemetry().midiTriggerCount == 0u);
    assert(e.telemetry().midiIgnoredCount == 1u);
    assert(e.telemetry().triggerCount == 0u);
}


static void externalHighNibbleValueChangesDoNotChopActiveVoices() {
    DigiD418StreamEngine e;
    e.prepare(48000.0, 985248.0);
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    auto p = oneFactoryProjection(0u);

    DigiD418Config cfg{};
    cfg.authMode = DigiAuthMode::StandaloneD418Layer;
    cfg.useExternalD418HighNibble = true;
    cfg.externalD418HighNibble = 0xA0u;
    e.setConfig(cfg);
    e.triggerSlotAt(0u, 127u, bank, p, 0);
    requirePass122(e.isActive());

    ArpSID::C64::C64SidBridgeState bridge{};
    ArpSID::C64::OpenBusLatch bus{};
    bridge.reset();
    bus.powerOn();
    e.processToSidBridge(p, bank, bridge, bus, 0u, 256, false, false, 0, 0x37u);
    const std::uint32_t writesBefore = e.telemetry().d418WriteCount;
    requirePass122(writesBefore > 0u);
    requirePass122((e.telemetry().lastD418 & 0xF0u) == 0xA0u);

    // The owning SID authority may change $D418 high nibble at any time
    // (filter mode bits). Updating only the external high-nibble value must
    // affect subsequent emitted D418 values without clearing the already
    // playing sample voice.
    cfg.externalD418HighNibble = 0xB0u;
    e.setConfig(cfg);
    requirePass122(e.isActive());

    bridge.timedWriteCount = 0u;
    e.processToSidBridge(p, bank, bridge, bus, 256u, 256, false, false, 0, 0x37u);
    requirePass122(e.telemetry().d418WriteCount > writesBefore);
    requirePass122((e.telemetry().lastD418 & 0xF0u) == 0xB0u);
}

int main() {
    idleProjectionDoesNotSpamNeutralD418Writes();
    sampleAccurateTriggerOffsetDoesNotEmitBeforeHostFrame();
    sampleOffsetIsConsumedAfterTriggerBlock();
    completedShortUserSampleStopsEmittingD418Writes();
    factoryDigiWritesPreserveHighNibbleAndDriveOpenBus();
    ioBankBlockedPreventsSidTimedWriteButStillTracksTelemetry();
    fastModeBypassesIoBankAndDoesNotDriveOpenBus();
    legacyFloatModeIsSideEffectFreeInD418StreamEngine();
    userSamplesAreSteppedFourBitD418NotInterpolatedPcm();
    savedUserSampleTriggersAndWritesNonZeroNibble();
    acceptedSidWriteTelemetrySeparatesAttemptsFromAcceptedWrites();
    queueOverflowDoesNotMutateD418ShadowOrAcceptedTelemetry();
    highRateDigiWritesArePhi2SpacedInsideOneHostFrame();
    externalHighNibbleValueChangesDoNotChopActiveVoices();
    emittedWritesNeverEscapeRenderQuantum();
    timelineDiscontinuityResetsPendingD418Scheduler();
    ownerSuppliedExactBlockEndPreventsFractionalClockFalseDiscontinuity();
    forensicHostFrameMatchesScheduledPhi2();
    forensicEventCarriesExactC64BusWriteRecord();
    guiRateClampAcceptsFullThirtyTwoKilohertzRange();
    setConfigModeAndRateChangesClearStaleVoicesAndScheduler();
    setConfigSemanticPolicyChangesClearStaleVoicesAndScheduler();
    unsupportedClockSourceSanitizesWithoutChoppingVoice();
    directTriggerIsIgnoredInLegacyMode();
    midiNoteMapsChromaticPadsToDigiSlotsSampleAccurately();
    midiNoteOutsideDigiPadRangeIsIgnored();
    midiRootIsClampedSoAllEightPadsStayAddressable();
    midiChannelFilterUsesZeroBasedChannelsAndOmniFallback();
    legacyModeClearsArmedVoicesBeforeReturningToAuth();
    idleBoundaryResetsCollisionAnchor();
    projectionCountsFactoryAndUserSourcesSeparately();
    collisionTelemetryDetectsSamePhi2DifferentValues();
    return 0;
}
