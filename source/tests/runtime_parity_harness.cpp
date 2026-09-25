#include "runtime_test_common.h"

#include <vector>

using namespace ArpSID;
using namespace ArpSID::Tests;

static std::vector<SidTimedEvent> makeScript() {
    std::vector<SidTimedEvent> script;

    SidTimedEvent tempo = makeEvent(SidTimedEventType::TempoChange, 0u, 0u, 0u, 1u);
    tempo.value_f32 = 126.0f;
    script.push_back(tempo);

    SidTimedEvent transport = makeEvent(SidTimedEventType::TransportChange, 0u, 0u, 0u, 2u);
    transport.value = 1.0f;
    script.push_back(transport);

    SidTimedEvent noteOn = makeEvent(SidTimedEventType::MidiNoteOn, 0u, 0u, 1u, 3u);
    noteOn.pitch = 64;
    noteOn.value = 0.75f;
    noteOn.noteId = 9001;
    script.push_back(noteOn);

    SidTimedEvent reg = makeEvent(SidTimedEventType::SidRegisterWrite, 1u, 2u, 0u, 4u);
    reg.target = 0x04u;
    reg.value_u32 = 0xAAu;
    script.push_back(reg);

    SidTimedEvent noteOff = makeEvent(SidTimedEventType::MidiNoteOff, 2u, 0u, 0u, 5u);
    noteOff.pitch = 64;
    noteOff.noteId = 9001;
    script.push_back(noteOff);

    return script;
}

int main() {
    const auto script = makeScript();
    const auto traceA = runCanonicalTrace(script, 4, 4u);
    const auto traceB = runCanonicalTrace(script, 4, 4u);

    ARPSID_TEST_EXPECT(traceA == traceB);
    ARPSID_TEST_EXPECT(!traceA.empty());
    std::puts("runtime_parity_harness: PASS");
    return 0;
}
