#include "arpsid/core/scope_triple_buffer.h"
#include "arpsid/core/c64_telemetry.h"
#include "arpsid/engines/drsid_engine.h"
#include "arpsid/engines/sid_register_engine.h"
#include "arpsid/gui/telemetry_scope_demand.h"
#include "arpsid/gui/telemetry_runtime_authority.h"
#include "arpsid_telemetry_iface.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

struct PatternSnapshot {
    uint64_t generation = 0;
    float values[512]{};
};

void testTripleBufferNoTear() {
    ArpSID::ScopeTripleBuffer<PatternSnapshot> triple;
    std::atomic<bool> done{false};
    std::atomic<bool> failed{false};
    std::thread producer([&] {
        for (uint64_t generation = 1; generation <= 20000; ++generation) {
            auto& snap = triple.writeSlot();
            snap.generation = generation;
            for (float& value : snap.values) value = static_cast<float>(generation);
            triple.publish();
        }
        done.store(true, std::memory_order_release);
    });
    std::thread consumer([&] {
        PatternSnapshot snap{};
        while (!done.load(std::memory_order_acquire)) {
            triple.peekLatest(snap);
            const float expected = static_cast<float>(snap.generation);
            for (float value : snap.values) {
                if (value != expected) {
                    failed.store(true, std::memory_order_release);
                    return;
                }
            }
        }
    });
    producer.join();
    consumer.join();
    require(!failed.load(std::memory_order_acquire), "triple buffer returned a torn frame");
}

void testChronologicalRotation() {
    float ring[256]{};
    for (int i = 0; i < 256; ++i) ring[i] = static_cast<float>(i);
    const uint32_t writePos = 73u;
    float chronological[256]{};
    for (int i = 0; i < 256; ++i)
        chronological[i] = ring[(writePos + static_cast<uint32_t>(i)) & 255u];
    require(chronological[0] == 73.0f, "rotation must begin at next-write position");
    require(chronological[182] == 255.0f, "rotation must preserve pre-wrap tail");
    require(chronological[183] == 0.0f, "rotation must wrap chronologically");
}

void testScopeDemandRuntime() {
    using ArpSID::GUI::TelemetryScopeDemand;
    using ArpSID::GUI::wantsTelemetryScopePayload;
    TelemetryScopeDemand options{};
    options.editorVisible = true;
    options.options = true;
    require(wantsTelemetryScopePayload(options), "Options must request scope payload");

    TelemetryScopeDemand popout{};
    popout.sidCorePopoutVisible = true;
    require(wantsTelemetryScopePayload(popout), "visible SIDCORE popout must request scopes without main editor");

    TelemetryScopeDemand bank{};
    bank.editorVisible = true;
    require(!wantsTelemetryScopePayload(bank), "scope-free tabs must not request heavy payloads");
}

void testPsidRuntimeAuthority() {
    using ArpSID::GUI::effectiveTelemetryNtsc;
    using ArpSID::GUI::effectiveTelemetrySidModel;
    require(effectiveTelemetryNtsc(true, true, false, false),
            "PSID NTSC file/runtime authority must override PAL UI fallback");
    require(!effectiveTelemetryNtsc(true, true, true, true),
            "PSID PAL file/runtime authority must override NTSC UI fallback");
    require(effectiveTelemetryNtsc(false, false, true, true),
            "UI clock remains fallback authority outside PSID runtime");
    require(effectiveTelemetrySidModel(true, true, true, 1, 0) == 1,
            "PSID file SID model must override UI SID model");
    require(effectiveTelemetrySidModel(true, true, false, 1, 0) == 0,
            "UI SID model remains fallback when file model is unspecified");
}

void testFullTelemetryRing() {
    ArpSID::FullTelemetryRing ring;
    ArpSIDTelemetry published{};
    published.telemetryFrameId = 42u;
    published.scopeFrameId = 42u;
    published.vcoScope[2][255] = 0.75f;
    published.c64SidWritePulseScope[17] = 1.0f;
    ring.publish(published);
    ArpSIDTelemetry read{};
    require(ring.loadLatest(read), "full telemetry ring must publish");
    require(read.telemetryFrameId == 42u && read.scopeFrameId == 42u,
            "full telemetry frame IDs must remain coherent");
    require(read.vcoScope[2][255] == 0.75f &&
            read.c64SidWritePulseScope[17] == 1.0f,
            "full telemetry payload must retain scopes");
}

void testC64HeavySnapshotNoTear() {
    ArpSID::C64::C64TelemetryGate gate;
    std::atomic<bool> done{false};
    std::atomic<bool> failed{false};
    std::thread producer([&] {
        for (uint64_t frame = 1; frame <= 4000; ++frame) {
            ArpSID::C64::C64ChipSnapshot snap{};
            snap.valid = true;
            snap.blockIndex = frame;
            snap.openBus = static_cast<uint8_t>(frame & 0xFFu);
            for (float& sample : snap.openBusScope)
                sample = static_cast<float>(frame);
            gate.publish(snap);
        }
        done.store(true, std::memory_order_release);
    });
    std::thread consumer([&] {
        ArpSID::C64::C64ChipSnapshot snap{};
        while (!done.load(std::memory_order_acquire)) {
            if (!gate.read(snap)) continue;
            const float expected = static_cast<float>(snap.blockIndex);
            for (float sample : snap.openBusScope) {
                if (sample != expected) {
                    failed.store(true, std::memory_order_release);
                    return;
                }
            }
        }
    });
    producer.join();
    consumer.join();
    require(!failed.load(std::memory_order_acquire),
            "heavy C64 snapshot gate returned a torn frame");
}

void testEngineScopeReaders() {
    ArpSID::DrSidEngine drsid;
    drsid.setSampleRate(48000.0);
    drsid.trigger(ArpSID::DrSidEngine::DrumType::Kick, 1.0f);
    ArpSID::SidRegisterEngine sidreg;
    sidreg.prepare(48000.0);
    sidreg.write(0x00u, 0x34u);
    sidreg.write(0x01u, 0x12u);
    sidreg.write(0x04u, 0x21u);
    sidreg.write(0x05u, 0x11u);
    sidreg.write(0x06u, 0xF1u);
    sidreg.write(0x18u, 0x0Fu);

    std::atomic<bool> done{false};
    std::atomic<bool> failed{false};
    std::thread producer([&] {
        float left[64]{};
        float right[64]{};
        float* outputs[2]{left, right};
        for (int block = 0; block < 500; ++block) {
            drsid.processBlock(outputs, 64);
            sidreg.renderBlock(left, right, 64);
        }
        done.store(true, std::memory_order_release);
    });
    std::thread consumer([&] {
        float osc[3][256]{};
        float filter[2][256]{};
        uint8_t mask = 0;
        uint32_t writePos = 0;
        while (!done.load(std::memory_order_acquire)) {
            drsid.getScopeSnapshot(osc, filter, mask, writePos);
            if (writePos >= 256u) failed.store(true, std::memory_order_release);
            sidreg.getScopeSnapshot(osc, filter, mask, writePos);
            if (writePos >= 256u) failed.store(true, std::memory_order_release);
            for (const auto& voice : osc)
                for (float sample : voice)
                    if (!std::isfinite(sample)) failed.store(true, std::memory_order_release);
        }
    });
    producer.join();
    consumer.join();
    require(!failed.load(std::memory_order_acquire), "engine scope snapshots must stay finite and bounded");
}

} // namespace

int main() {
    static_assert(ArpSID::C64::C64ChipSnapshot::kBusScopeLen == 128,
                  "heavy C64 scope must retain full 128-sample resolution");
    testTripleBufferNoTear();
    testChronologicalRotation();
    testScopeDemandRuntime();
    testPsidRuntimeAuthority();
    testFullTelemetryRing();
    testC64HeavySnapshotNoTear();
    testEngineScopeReaders();
    std::cout << "Telemetry runtime coherency v745: PASS\n";
    return 0;
}
