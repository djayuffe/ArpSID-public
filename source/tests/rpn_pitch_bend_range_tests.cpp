// Copyright (C) 2024-2026 Ulf Bertilsson
// RPN 0 (pitch-bend range) through the shared kernel, by both routes a host
// uses: host-control parameters (VST3 IMidiMapping of CC 101/100/6/38) and
// raw MIDI CCs (AU / standalone). Each lands on the channel it was sent on;
// selecting an RPN changes nothing until Data Entry; Data Entry after an NRPN
// select or a Reset All Controllers leaves the bend range alone.

#include "au3/ArpSIDDSPKernel.hpp"
#include "parameter_ids.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace ArpSID;

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        ++failures;
    }
}

constexpr int F = 256;

struct Rig {
    std::unique_ptr<ArpSIDDSPKernel> k = std::make_unique<ArpSIDDSPKernel>();
    std::array<float, F> l{}, r{};
    TransportState t{};
    Rig() {
        k->setup(48000.0, F);
        t.sampleRate = 48000.0;
        t.frameCount = F;
        run({});
    }
    void run(std::vector<TimedEvent> ev) {
        float* o[2] = {l.data(), r.data()};
        k->processBlock(o, 2, F, ev.empty() ? nullptr : ev.data(), (int)ev.size(), t);
    }
    float range(int ch) const { return k->runtimeBitPerfectEngine()->pitchBendRangeSemis(ch); }
};

TimedEvent param(int base, int ch, int midiValue) {
    TimedEvent e{};
    e.sampleOffset = 0;
    e.kind = EventKind::ParameterSet;
    e.target = (uint32_t)(base + ch);
    e.value = e.value_f32 = (float)midiValue / 127.0f;
    return e;
}

TimedEvent cc(int ch, int number, int midiValue) {
    TimedEvent e{};
    e.sampleOffset = 0;
    e.kind = EventKind::ControlChange;
    e.channel = (uint8_t)ch;
    e.ccNum = (uint8_t)number;
    e.value = e.value_f32 = (float)midiValue / 127.0f;
    return e;
}

} // namespace

int main() {
    ArpSID::prewarmAllSidTables();

    // Parameter route (VST3): RPN 0 on channel 3 = 12 semitones + 50 cents.
    {
        Rig g;
        check(g.range(3) == 2.0f && g.range(0) == 2.0f, "default bend range is 2 semitones");
        g.run({param(kParamHostCtrlRpnMsbBase, 3, 0), param(kParamHostCtrlRpnLsbBase, 3, 0)});
        check(g.range(3) == 2.0f, "selecting RPN 0 alone leaves the range");
        g.run({param(kParamHostCtrlDataEntryMsbBase, 3, 12), param(kParamHostCtrlDataEntryLsbBase, 3, 50)});
        check(std::fabs(g.range(3) - 12.5f) < 1e-4f, "Data Entry sets channel 3's range (12 + 50 cents)");
        for (int ch = 0; ch < 16; ++ch)
            if (ch != 3) check(g.range(ch) == 2.0f, "other channels keep their range");

        // NRPN select deselects the RPN: its Data Entry is not a bend range.
        g.run({param(kParamHostCtrlNrpnMsbBase, 3, 1), param(kParamHostCtrlNrpnLsbBase, 3, 2),
               param(kParamHostCtrlDataEntryMsbBase, 3, 24)});
        check(std::fabs(g.range(3) - 12.5f) < 1e-4f, "Data Entry after an NRPN select leaves the range");

        // Channel 16 (the last block entry) works too.
        g.run({param(kParamHostCtrlRpnMsbBase, 15, 0), param(kParamHostCtrlRpnLsbBase, 15, 0),
               param(kParamHostCtrlDataEntryMsbBase, 15, 48)});
        check(g.range(15) == 48.0f, "channel 16 range set");
    }

    // Raw MIDI route (AU / standalone): the same state machine.
    {
        Rig g;
        g.run({cc(5, 101, 0), cc(5, 100, 0), cc(5, 6, 7), cc(5, 38, 0)});
        check(g.range(5) == 7.0f, "raw CC RPN 0 sets channel 6's range");
        check(g.range(0) == 2.0f && g.range(4) == 2.0f, "raw CC leaves other channels");
        g.run({cc(5, 99, 0), cc(5, 98, 1), cc(5, 6, 30)});
        check(g.range(5) == 7.0f, "raw NRPN Data Entry leaves the range");
        g.run({cc(9, 101, 0), cc(9, 100, 0), cc(9, 121, 0), cc(9, 6, 30)});
        check(g.range(9) == 2.0f, "Reset All Controllers deselects the RPN");
        g.run({cc(9, 101, 0), cc(9, 100, 0), cc(9, 6, 5)});
        check(g.range(9) == 5.0f, "reselected RPN 0 works after the reset");
    }

    if (failures) {
        std::fprintf(stderr, "rpn_pitch_bend_range_tests: %d failure(s)\n", failures);
        return 1;
    }
    std::puts("rpn_pitch_bend_range_tests PASS");
    return 0;
}
