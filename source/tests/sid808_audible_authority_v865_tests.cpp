// sid808_audible_authority_v865_tests.cpp
//
// v865 closure:
// 1. Every SID808 drum family must render an audible minimum through the bridge.
// 2. SID808 waveform config accepts both SID control bits and internal nibbles.
// 3. The bridge publishes routed-hit, configured-kit and output-peak telemetry.
// 4. The AU UI consumes a unified audible-authority display.

#include "arpsid/engines/drum_engine_host_bridge.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef ARPSID_SOURCE_DIR
#define ARPSID_SOURCE_DIR "."
#endif

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "sid808_audible_authority_v865_tests FAIL: " << message << "\n";
        std::exit(1);
    }
}

std::string readFile(const std::string& rel) {
    const std::string path = std::string(ARPSID_SOURCE_DIR) + "/" + rel;
    std::ifstream in(path);
    require(in.good(), ("missing source file: " + path).c_str());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void requireContains(const std::string& haystack,
                     const std::string& needle,
                     const char* message) {
    require(haystack.find(needle) != std::string::npos, message);
}

struct RenderStats {
    float peak = 0.0f;
    float rms = 0.0f;
};

template <std::size_t N>
RenderStats statsFor(const std::array<float, N>& l, const std::array<float, N>& r) {
    double sum = 0.0;
    float peak = 0.0f;
    for (std::size_t i = 0; i < N; ++i) {
        require(std::isfinite(l[i]) && std::isfinite(r[i]), "render produced non-finite sample");
        peak = std::max(peak, std::fabs(l[i]));
        peak = std::max(peak, std::fabs(r[i]));
        sum += static_cast<double>(l[i]) * static_cast<double>(l[i]);
        sum += static_cast<double>(r[i]) * static_cast<double>(r[i]);
    }
    RenderStats s{};
    s.peak = peak;
    s.rms = static_cast<float>(std::sqrt(sum / static_cast<double>(N * 2u)));
    return s;
}

template <std::size_t N>
RenderStats renderBridgeHit(ArpSID::DrumEngineHostBridge& bridge,
                            ArpSID::SidGMDrumClass drumClass,
                            std::uint8_t midiNote) {
    std::array<float, N> l{};
    std::array<float, N> r{};
    bridge.allNotesOff();
    bridge.noteOn(drumClass, 120u, midiNote);
    bridge.processBlock(l.data(), r.data(), static_cast<int>(N));
    return statsFor(l, r);
}

template <std::size_t N>
RenderStats processBridgeBlock(ArpSID::DrumEngineHostBridge& bridge) {
    std::array<float, N> l{};
    std::array<float, N> r{};
    bridge.processBlock(l.data(), r.data(), static_cast<int>(N));
    return statsFor(l, r);
}

template <std::size_t N>
RenderStats renderEngineHit(ArpSID::Sid808Engine& engine,
                            ArpSID::Sid808Drum drum,
                            std::uint8_t midiNote) {
    std::array<float, N> l{};
    std::array<float, N> r{};
    engine.allNotesOff();
    engine.noteOn(drum, 120u, midiNote);
    engine.processBlock(l.data(), r.data(), static_cast<int>(N));
    return statsFor(l, r);
}

void testWaveformNormalization() {
    using namespace ArpSID;
    static_assert(sid808NormalizeWaveformControl(0x10u) == 0x10u, "raw triangle");
    static_assert(sid808NormalizeWaveformControl(0x01u) == 0x10u, "internal triangle");
    static_assert(sid808NormalizeWaveformControl(0x40u) == 0x40u, "raw pulse");
    static_assert(sid808NormalizeWaveformControl(0x04u) == 0x40u, "internal pulse");
    static_assert(sid808NormalizeWaveformControl(0x80u) == 0x80u, "raw noise");
    static_assert(sid808NormalizeWaveformControl(0x08u) == 0x80u, "internal noise");
    static_assert(sid808NormalizeWaveformControl(0x0Fu) == 0xF0u, "internal all waveforms");
    static_assert(sid808ControlWaveformToInternal(0x80u) == 0x08u, "raw noise to internal");
    static_assert(sid808ControlWaveformToInternal(0x08u) == 0x08u, "internal noise to internal");

    constexpr std::size_t kFrames = 4096u;
    Sid808Engine engine;
    engine.prepare(48000.0);

    Sid808VoiceConfig cfg = sid808DefaultConfig(Sid808Drum::ClosedHat);
    cfg.waveform = 0x08u;
    engine.setDrumVoiceConfig(Sid808Drum::ClosedHat, cfg);
    const RenderStats hat = renderEngineHit<kFrames>(engine, Sid808Drum::ClosedHat, 42u);
    require(engine.lastAppliedConfig().waveform == 0x80u,
            "setDrumVoiceConfig must normalize internal noise nibble to raw SID control bit");
    require(hat.peak > 0.02f && hat.rms > 0.001f,
            "internal-noise closed hat override must remain audible");

    Sid808HitOverride ov{};
    ov.hasWaveform = true;
    ov.waveform = 0x08u;
    engine.allNotesOff();
    engine.noteOnWithOverride(Sid808Drum::Clap, 120u, 39u, ov);
    std::array<float, kFrames> l{};
    std::array<float, kFrames> r{};
    engine.processBlock(l.data(), r.data(), static_cast<int>(kFrames));
    const RenderStats clap = statsFor(l, r);
    require(engine.lastAppliedConfig().waveform == 0x80u,
            "Sid808HitOverride must normalize internal noise nibble to raw SID control bit");
    require(clap.peak > 0.02f && clap.rms > 0.001f,
            "internal-noise clap override must remain audible");
}

void testBridgeAudibleMinimumAndTelemetry() {
    using namespace ArpSID;
    constexpr std::size_t kFrames = 4096u;
    constexpr float kAudiblePeak = 0.02f;
    constexpr float kAudibleRms = 0.001f;

    DrumEngineHostBridge bridge;
    bridge.prepare(48000.0);
    require(bridge.loadFactorySlot(120), "bridge must load SID808 factory slot 120");

    const auto initial = bridge.sid808OutputTelemetry();
    require(initial.configuredKitSlot == 120, "bridge telemetry must publish configured SID808 kit");
    require(initial.sid808ContextActive, "bridge telemetry must publish active SID808 context");

    struct DrumCase {
        Sid808Drum drum;
        SidGMDrumClass gm;
        std::uint8_t note;
        const char* name;
    };

    const std::array<DrumCase, 8> drums{{
        {Sid808Drum::Kick,      SidGMDrumClass::Kick,      36u, "Kick"},
        {Sid808Drum::Snare,     SidGMDrumClass::Snare,     38u, "Snare"},
        {Sid808Drum::ClosedHat, SidGMDrumClass::ClosedHat, 42u, "ClosedHat"},
        {Sid808Drum::OpenHat,   SidGMDrumClass::OpenHat,   46u, "OpenHat"},
        {Sid808Drum::Clap,      SidGMDrumClass::Clap,      39u, "Clap"},
        {Sid808Drum::Cowbell,   SidGMDrumClass::Cowbell,   56u, "Cowbell"},
        {Sid808Drum::Tom,       SidGMDrumClass::Tom,       47u, "Tom"},
        {Sid808Drum::Rim,       SidGMDrumClass::Rim,       37u, "Rim"},
    }};

    std::uint64_t expectedHits = 0u;
    for (const DrumCase& dc : drums) {
        const RenderStats s = renderBridgeHit<kFrames>(bridge, dc.gm, dc.note);
        ++expectedHits;
        if (s.peak <= kAudiblePeak || s.rms <= kAudibleRms) {
            std::cerr << dc.name << " peak=" << s.peak << " rms=" << s.rms << "\n";
            require(false, "SID808 drum family did not meet audible minimum");
        }
        const auto tel = bridge.sid808OutputTelemetry();
        require(tel.routedHitCount == expectedHits, "bridge telemetry routed-hit counter mismatch");
        require(tel.configuredKitSlot == 120, "bridge telemetry configured kit changed unexpectedly");
        require(tel.lastRoutedDrumClass == static_cast<int>(dc.drum),
                "bridge telemetry last routed drum class mismatch");
        require(tel.lastRoutedMidiNote == static_cast<int>(dc.note),
                "bridge telemetry last routed MIDI note mismatch");
        require(tel.lastRoutedVelocity > 0.90f, "bridge telemetry last routed velocity too low");
        require(tel.outputPeak > kAudiblePeak, "bridge telemetry output peak did not report audible signal");
        require(tel.sid808ContextActive, "bridge telemetry lost SID808 active context");
    }

    bridge.allNotesOff();
    Sid808VoiceConfig mutedKick = bridge.sid808Engine().drumVoiceConfig(Sid808Drum::Kick);
    mutedKick.voiceLevel = 0.0f;
    bridge.sid808Engine().setDrumVoiceConfig(Sid808Drum::Kick, mutedKick);
    bridge.sid808Engine().innerEngine().setMasterVolume(0.0f);
    const std::uint64_t silentBefore = bridge.sid808OutputTelemetry().silentActiveBlockCount;
    bridge.noteOn(SidGMDrumClass::Kick, 120u, 36u);
    (void)processBridgeBlock<512u>(bridge);
    const auto silentTel = bridge.sid808OutputTelemetry();
    require(silentTel.activeVoiceCount > 0u,
            "muted SID808 hit must still publish active voice count");
    require(silentTel.outputPeak <= 0.005f,
            "muted SID808 hit must publish below-audible output peak");
    require(silentTel.silentActiveBlockCount > silentBefore,
            "active SID808 voice below audible peak must increment silent-active blocks");
    require(silentTel.silentActiveSinceLastHit,
            "active SID808 voice below audible peak must publish since-last-hit warning");
}

void testScheduledBridgeTelemetry() {
    using namespace ArpSID;
    constexpr std::size_t kFrames = 4096u;
    constexpr float kAudiblePeak = 0.02f;
    constexpr float kAudibleRms = 0.001f;

    DrumEngineHostBridge bridge;
    bridge.prepare(48000.0);
    require(bridge.loadFactorySlot(120), "scheduled bridge test must load SID808 factory slot 120");

    bridge.allNotesOff();
    bridge.noteOnAt(64, SidGMDrumClass::Kick, 120u, 36u);
    RenderStats s = processBridgeBlock<kFrames>(bridge);
    require(s.peak > kAudiblePeak && s.rms > kAudibleRms,
            "delayed noteOnAt must render audible SID808 output");
    auto tel = bridge.sid808OutputTelemetry();
    require(tel.routedHitCount == 1u, "delayed noteOnAt must increment routed-hit telemetry");
    require(tel.lastRoutedDrumClass == static_cast<int>(Sid808Drum::Kick),
            "delayed noteOnAt must publish last routed drum");
    require(tel.lastRoutedMidiNote == 36, "delayed noteOnAt must publish last routed MIDI note");
    require(tel.outputPeak > kAudiblePeak, "delayed noteOnAt must publish output peak telemetry");

    Sid808HitOverride ov{};
    ov.hasWaveform = true;
    ov.waveform = 0x08u;
    ov.hasVoiceLevel = true;
    ov.voiceLevel = 1.0f;
    bridge.allNotesOff();
    const std::uint64_t beforeOverride = bridge.sid808OutputTelemetry().routedHitCount;
    bridge.noteOnAtWithOverride(96, SidGMDrumClass::Clap, 118u, 39u, ov);
    s = processBridgeBlock<kFrames>(bridge);
    require(s.peak > kAudiblePeak && s.rms > kAudibleRms,
            "delayed noteOnAtWithOverride must render audible SID808 output");
    tel = bridge.sid808OutputTelemetry();
    require(tel.routedHitCount == beforeOverride + 1u,
            "delayed noteOnAtWithOverride must increment routed-hit telemetry");
    require(tel.lastRoutedDrumClass == static_cast<int>(Sid808Drum::Clap),
            "delayed noteOnAtWithOverride must publish last routed drum");
    require(tel.lastRoutedMidiNote == 39, "delayed noteOnAtWithOverride must publish last MIDI note");

    bridge.allNotesOff();
    const std::uint64_t beforeMulti = bridge.sid808OutputTelemetry().routedHitCount;
    bridge.noteOnAt(8, SidGMDrumClass::Kick, 120u, 36u);
    bridge.noteOnAt(96, SidGMDrumClass::Snare, 112u, 38u);
    bridge.noteOnAt(160, SidGMDrumClass::Cowbell, 104u, 56u);
    s = processBridgeBlock<kFrames>(bridge);
    require(s.peak > kAudiblePeak && s.rms > kAudibleRms,
            "multiple delayed notes in one block must render audible SID808 output");
    tel = bridge.sid808OutputTelemetry();
    require(tel.routedHitCount == beforeMulti + 3u,
            "multiple delayed notes must increment routed-hit telemetry once per event");
    require(tel.lastRoutedDrumClass == static_cast<int>(Sid808Drum::Cowbell),
            "multiple delayed notes must leave the final drum in telemetry");
    require(tel.lastRoutedMidiNote == 56, "multiple delayed notes must leave the final MIDI note");

    bridge.allNotesOff();
    const std::uint64_t beforeEnd = bridge.sid808OutputTelemetry().routedHitCount;
    bridge.noteOnAt(static_cast<int>(kFrames), SidGMDrumClass::Rim, 120u, 37u);
    (void)processBridgeBlock<kFrames>(bridge);
    tel = bridge.sid808OutputTelemetry();
    require(tel.routedHitCount == beforeEnd + 1u,
            "block-end scheduled note must publish routed-hit telemetry");
    require(tel.lastRoutedDrumClass == static_cast<int>(Sid808Drum::Rim),
            "block-end scheduled note must publish last routed drum");
    require(tel.lastRoutedMidiNote == 37, "block-end scheduled note must publish last MIDI note");
    s = processBridgeBlock<kFrames>(bridge);
    require(s.peak > kAudiblePeak && s.rms > kAudibleRms,
            "block-end scheduled note must render audibly in the following block");
    tel = bridge.sid808OutputTelemetry();
    require(tel.outputPeak > kAudiblePeak,
            "block-end scheduled note must publish output peak after the following block");

    bridge.allNotesOff();
    bridge.resetScheduledNoteDiagnostics();
    for (int i = 0; i < 128; ++i) {
        bridge.noteOnAt(16 + i, SidGMDrumClass::Kick, 100u, 36u);
    }
    require(bridge.scheduledNoteOverflowCount() > 0u,
            "scheduled note overflow must be counted");
    bridge.allNotesOff();
}

void testSourceWiring() {
    const std::string kernel = readFile("source/au3/ArpSIDDSPKernel.hpp");
    const std::string snapshot = readFile("source/common/arpsid_telemetry_snapshot.h");
    const std::string adapter = readFile("source/au3/ArpSIDDSPKernelAdapter.mm");
    const std::string view = readFile("source/au3/ArpSIDViewController.mm");

    requireContains(kernel, "sid808OutputTelemetry()", "kernel must read SID808 bridge telemetry");
    requireContains(kernel, "telemetrySid808OutputPeak_", "kernel must publish post-DC SID808 bridge peak");
    requireContains(kernel, "sid808BridgeAuthority", "kernel must select SID808 levels when bridge owns audio");
    requireContains(snapshot, "sid808RoutedHitCount", "shared telemetry must expose SID808 routed-hit count");
    requireContains(snapshot, "sid808OutputPeak", "shared telemetry must expose SID808 output peak");
    requireContains(snapshot, "sid808SilentActiveBlockCount", "shared telemetry must expose SID808 silent-active count");
    requireContains(snapshot, "sid808ZeroPeakWithActiveVoiceCount", "shared telemetry must expose literal zero-peak count");
    requireContains(snapshot, "sid808RawMeanBeforeDc", "shared telemetry must expose pre-DC mean");
    requireContains(snapshot, "sid808PostDcMean", "shared telemetry must expose post-DC mean");
    requireContains(snapshot, "sid808LastSnareBodyRms", "shared telemetry must expose snare body RMS");
    requireContains(adapter, "out->sid808OutputPeak", "adapter must copy SID808 output peak");
    requireContains(adapter, "out->sid808SilentActiveBlockCount", "adapter must copy silent-active count");
    requireContains(adapter, "out->sid808RawMeanBeforeDc", "adapter must copy pre-DC mean");
    requireContains(adapter, "out->sid808BridgeReplacedOutput", "adapter must copy SID808 output replacement state");
    requireContains(view, "ArpSIDUnifiedAudibleAuthorityLabel", "view must provide unified audible-authority label");
    requireContains(view, "AUTH SID808-BRIDGE", "view must name SID808 bridge authority");
    requireContains(view, "SILENT-ACTIVE", "view must label SID808 active voices with zero output");
    requireContains(view, "tel.sid808OutputPeak", "view must include SID808 bridge peak in drum activity");
}

} // namespace

int main() {
    testWaveformNormalization();
    testBridgeAudibleMinimumAndTelemetry();
    testScheduledBridgeTelemetry();
    testSourceWiring();
    std::cout << "sid808_audible_authority_v865_tests PASS\n";
    return 0;
}
