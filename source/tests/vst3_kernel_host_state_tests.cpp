// Copyright (C) 2024-2026 Ulf Bertilsson
// VST3 kernel host state (no VST3 SDK needed): the v5 component state keeps
// every GUI model, the loaded C64 tune and its subtune; a state without a
// tune unloads one left from before; subtunes can be switched on a restored
// tune; a truncated state keeps what was read.

#include "vst3/arpsid_vst3_kernel_host.h"

#include "au3/ArpSIDCanonicalEvents.h"
#include "parameter_ids.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <cstring>
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

// Minimal PSID v2 tune: 3 songs, init at $1000 and play at $1003 (both RTS).
std::vector<std::uint8_t> makePsid() {
    std::vector<std::uint8_t> f(0x7C, 0);
    std::memcpy(f.data(), "PSID", 4);
    f[0x05] = 2;                      // version 2
    f[0x07] = 0x7C;                   // data offset
    f[0x0A] = 0x10; f[0x0B] = 0x00;   // init $1000
    f[0x0C] = 0x10; f[0x0D] = 0x03;   // play $1003
    f[0x0F] = 3;                      // songs
    f[0x11] = 1;                      // start song
    std::memcpy(&f[0x16], "Test Tune", 9);
    std::memcpy(&f[0x36], "ArpSID", 6);
    std::memcpy(&f[0x56], "2026", 4);
    const std::uint8_t data[] = {0x00, 0x10, 0x60, 0xEA, 0xEA, 0x60}; // load $1000: RTS NOP NOP RTS
    f.insert(f.end(), std::begin(data), std::end(data));
    return f;
}

void render(Vst3KernelHost& h, int blocks) {
    std::vector<float> l(256), r(256);
    float* out[2] = {l.data(), r.data()};
    TransportState t{};
    t.sampleRate = 48000.0;
    t.bpm = 120.0;
    for (int b = 0; b < blocks; ++b) h.render(out, 2, 256, nullptr, 0, t);
}

} // namespace

int main() {
    const auto tune = makePsid();

    Vst3KernelHost a;
    a.setup(48000.0, 256);
    render(a, 2);
    check(a.loadSidFile(tune.data(), tune.size(), 1), "PSID loads");
    check(a.sidFileSize() == tune.size() && a.sidSubtune() == 1, "host keeps the file and subtune");
    auto mix = a.mix();
    mix.master.masterVolume = 123;
    a.setMix(mix);
    render(a, 4);
    const auto state = a.saveState();
    check(state.size() > tune.size(), "state holds the tune");

    // Restore into a fresh instance.
    Vst3KernelHost b;
    b.setup(48000.0, 256);
    check(b.loadState(state.data(), state.size()), "v5 state loads");
    render(b, 4);
    check(b.isSidFileLoaded(), "tune restored with the project");
    check(b.sidSubtune() == 1 && b.sidFileSize() == tune.size(), "restored subtune and file");
    check(b.mix().master.masterVolume == 123, "MIX model restored");
    check(b.selectSidSubtune(2) && b.sidSubtune() == 2, "restored tune switches subtune");

    // A state saved without a tune unloads the current one.
    Vst3KernelHost c;
    c.setup(48000.0, 256);
    render(c, 2);
    const auto noTune = c.saveState();
    check(b.loadState(noTune.data(), noTune.size()), "tune-less state loads");
    render(b, 2);
    check(!b.isSidFileLoaded() && b.sidFileSize() == 0, "tune-less state unloads the tune");
    check(!b.selectSidSubtune(1), "no subtune switch without a tune");

    // Truncated state: the chunks before the cut still apply.
    auto cut = state;
    cut.resize(cut.size() - 10);
    Vst3KernelHost d;
    d.setup(48000.0, 256);
    check(d.loadState(cut.data(), cut.size()), "truncated state keeps the root");
    check(d.mix().master.masterVolume == 123, "truncated state keeps the models read before the cut");

    // DIGI capture: arm, feed the capture input, stop -> a user sample in
    // the slot, selected as its source.
    {
        Vst3KernelHost e;
        e.setup(48000.0, 256);
        check(!e.stopDigiCapture("none"), "stop without a take records nothing");
        check(e.armDigiCapture(2) && e.digiCaptureStatus().armed, "capture arms");
        std::vector<float> l(256), r(256);
        const float* in[2] = {l.data(), r.data()};
        for (int b = 0; b < 40; ++b) {
            for (int i = 0; i < 256; ++i) l[i] = r[i] = 0.25f * std::sin(0.05f * float(b * 256 + i));
            e.captureDigiInput(in, 2, 256);
        }
        const auto st = e.digiCaptureStatus();
        check(st.frames == 40 * 256 && st.peak > 0.2f && st.slot == 2, "capture counts frames and peak");
        check(e.stopDigiCapture("take"), "stop stores the take");
        check(!e.digiCaptureStatus().armed, "capture disarmed after stop");
        GUI::DigiPanelModel m{};
        auto bank = std::make_unique<GUI::DigiSampleBankBlob>();
        e.digi(m, *bank);
        check(m.slots[2].sourceType == GUI::DigiSourceType::UserImport, "slot 3 plays the capture");
        check(bank->clips[2].frameCount > 0 && std::strcmp(bank->clips[2].displayName, "take") == 0,
              "the capture is in the sample bank, named");
        e.captureDigiInput(in, 2, 256);
        check(e.digiCaptureStatus().frames == 40 * 256, "a disarmed capture ignores input");
    }

    // Host bypass: saved as its own chunk, restored, and read by the controller.
    {
        Vst3KernelHost a;
        a.setup(44100.0, 512);
        check(!a.bypass(), "a new instance is not bypassed");
        a.setBypass(true);
        const auto on = a.saveState();
        check(Vst3KernelHost::decodeBypass(on.data(), on.size()), "decodeBypass reads the saved flag");
        Vst3KernelHost b;
        b.setup(44100.0, 512);
        check(b.loadState(on.data(), on.size()) && b.bypass(), "bypass restores");
        a.setBypass(false);
        const auto off = a.saveState();
        check(!Vst3KernelHost::decodeBypass(off.data(), off.size()), "decodeBypass: not bypassed");
        check(b.loadState(off.data(), off.size()) && !b.bypass(), "loading an un-bypassed state clears bypass");
        const std::uint8_t legacy[4] = {4, 0, 0, 0};
        check(!Vst3KernelHost::decodeBypass(legacy, sizeof legacy), "legacy state is never bypassed");
    }

    if (failures) {
        std::fprintf(stderr, "vst3_kernel_host_state_tests: %d failure(s)\n", failures);
        return 1;
    }
    std::puts("vst3_kernel_host_state_tests PASS");
    return 0;
}
