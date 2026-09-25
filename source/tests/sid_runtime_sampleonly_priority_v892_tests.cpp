#include <iostream>
#include <vector>
#include <string>
#include "arpsid/core/sid_event_queue.h"
#include "arpsid/core/sid_host_cycle_dispatcher.h"
#include "arpsid/core/sid_runtime_synth_register_scheduler.h"

static void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "sid_runtime_sampleonly_priority_v892_tests FAIL: " << msg << "\n";
        std::exit(1);
    }
}

static ArpSID::SidTimedEvent ev(ArpSID::SidTimedEventType type,
                                uint32_t sample,
                                uint16_t cycle,
                                uint8_t sub,
                                uint32_t order) {
    ArpSID::SidTimedEvent e{};
    e.type = type;
    e.sample_offset = sample;
    e.cycle_offset = cycle;
    e.subphase = sub;
    e.arrival_order = order;
    return e;
}

static void sortQueue(ArpSID::SidTimedEventQueue& q) {
    for (int i = 1; i < q.count; ++i) {
        ArpSID::SidTimedEvent key = q.events[i];
        int j = i;
        while (j > 0 && ArpSID::SidTimedEvent::before(key, q.events[j - 1])) {
            q.events[j] = q.events[j - 1];
            --j;
        }
        q.events[j] = key;
    }
}

static void sampleOnlySubphaseDoesNotBeatPriority() {
    ArpSID::SidTimedEvent panic = ev(ArpSID::SidTimedEventType::Panic, 0u,
                                     ArpSID::kSidUnresolvedCycleOffset, 0xFFu, 2u);
    ArpSID::SidTimedEvent note = ev(ArpSID::SidTimedEventType::MidiNoteOn, 0u,
                                    ArpSID::kSidUnresolvedCycleOffset, 0u, 1u);
    require(ArpSID::SidTimedEvent::before(panic, note),
            "sample-only Panic must beat lower-priority sample-only NoteOn despite subphase 0xFF");
    require(!ArpSID::SidTimedEvent::before(note, panic),
            "sample-only NoteOn subphase 0 must not beat Panic");

    ArpSID::SidTimedEvent allOff = ev(ArpSID::SidTimedEventType::AllSoundOff, 0u,
                                      ArpSID::kSidUnresolvedCycleOffset, 0xFFu, 3u);
    ArpSID::SidTimedEvent noteOff = ev(ArpSID::SidTimedEventType::MidiNoteOff, 0u,
                                       ArpSID::kSidUnresolvedCycleOffset, 0u, 0u);
    require(ArpSID::SidTimedEvent::before(allOff, noteOff),
            "sample-only AllSoundOff must beat sample-only NoteOff by priority, not subphase");
}

static void cycleStampedSubphaseStillOrdersPhysicalWrites() {
    ArpSID::SidTimedEvent early = ev(ArpSID::SidTimedEventType::SidRegisterWrite, 0u, 4u, 3u, 2u);
    ArpSID::SidTimedEvent late = ev(ArpSID::SidTimedEventType::SidRegisterWrite, 0u, 4u, 9u, 1u);
    require(ArpSID::SidTimedEvent::before(early, late),
            "cycle-stamped events must still sort by subphase");
    require(!ArpSID::SidTimedEvent::before(late, early),
            "later physical subphase must not sort before earlier subphase");
}


static void synthModeDelayedWritesNormalizeSampleOnlySentinel() {
    const double sr = 48000.0;
    const double phi2 = 985248.0;
    require(ArpSID::normalizeSynthModeCycleOffset(ArpSID::kSidUnresolvedCycleOffset) == 0u,
            "synth-mode scheduler normalizes unresolved/sample-only cycle sentinel to boundary cycle 0");
    require(ArpSID::clampSynthModeCycleOffset(0, ArpSID::kSidUnresolvedCycleOffset, sr, phi2) == 0u,
            "clampSynthModeCycleOffset must not clamp the sentinel to the end of the sample");
    require(ArpSID::synthModeAbsoluteSidCycleAtSampleOffset(0, ArpSID::kSidUnresolvedCycleOffset, sr, phi2) == 0ull,
            "absolute-cycle helper maps sample-only base to PHI2 0, not the sample tail");

    ArpSID::SidWriteQueue q;
    q.clear();
    ArpSID::pushSynthModeWriteDelayedByCycles(q, 0x04u, 0x41u, 0u,
                                              ArpSID::kSidUnresolvedCycleOffset,
                                              1, sr, phi2);
    require(q.size() == 1u, "delayed synth write queued");
    require(q.data()[0].sampleOffset == 0u && q.data()[0].cycleOffset == 1u,
            "one-cycle delay from sample-only base lands at cycle 1 of sample 0, not a future/tail sample");
}

static void dispatcherAppliesSampleOnlyPriorityBeforeSpans() {
    ArpSID::SidTimedEventQueue q{ArpSID::SidTimedEventQueue::AllocateStorage{}};
    q.push(ev(ArpSID::SidTimedEventType::MidiNoteOn, 0u,
              ArpSID::kSidUnresolvedCycleOffset, 0u, 1u));
    q.push(ev(ArpSID::SidTimedEventType::Panic, 0u,
              ArpSID::kSidUnresolvedCycleOffset, 0xFFu, 2u));
    sortQueue(q);

    ArpSID::SidCycleClockState clock{};
    clock.configureFixedCyclesPerSample(6u);
    std::vector<std::string> log;

    ArpSID::SidHostCycleDispatcher::dispatchBlock(
        q, 1, clock, true,
        [&](const ArpSID::SidTimedEvent& e) {
            switch (e.type) {
                case ArpSID::SidTimedEventType::Panic: log.push_back("panic"); break;
                case ArpSID::SidTimedEventType::MidiNoteOn: log.push_back("note"); break;
                default: log.push_back("other"); break;
            }
        },
        [&](int, int) { log.push_back("slice"); },
        [&](int, uint16_t, uint16_t) { log.push_back("cycle-span"); },
        [&](int, uint16_t, uint16_t, uint16_t) { log.push_back("subphase-span"); });

    require(log.size() >= 3, "expected panic, note, render span/slice");
    require(log[0] == "panic", "dispatcher must apply sample-only Panic first");
    require(log[1] == "note", "dispatcher must apply lower-priority sample-only event after Panic");
    require(log[0] != "subphase-span" && log[1] != "subphase-span",
            "sample-only events must not render subphase spans before application");
}

int main() {
    sampleOnlySubphaseDoesNotBeatPriority();
    cycleStampedSubphaseStillOrdersPhysicalWrites();
    dispatcherAppliesSampleOnlyPriorityBeforeSpans();
    synthModeDelayedWritesNormalizeSampleOnlySentinel();
    std::cout << "sid_runtime_sampleonly_priority_v892_tests PASS\n";
    return 0;
}
