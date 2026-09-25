#include "arpsid/core/sid_runtime_voice_policy.h"
#include "arpsid/core/sid_runtime_synth_register_scheduler.h"
#include "parameter_ids.h"
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

static int countKind(const ArpSID::VoiceEventBuffer& b, ArpSID::VoiceEvent::Kind k) {
    int n = 0;
    for (int i = 0; i < b.count; ++i) if (b.events[i].kind == k) ++n;
    return n;
}

struct TestWrite {
    uint8_t reg{};
    uint8_t value{};
    uint16_t sample{};
    uint16_t cycle{};
};
struct TestQueue {
    std::vector<TestWrite> writes;
    void push(uint8_t reg, uint8_t value, uint16_t sampleOffset, uint16_t cycleOffset) {
        writes.push_back({reg, value, sampleOffset, cycleOffset});
    }
};

static std::array<float, ArpSID::kNumParams> defaultParams() {
    std::array<float, ArpSID::kNumParams> p{};
    for (int i = 0; i < ArpSID::kNumParams; ++i) p[(size_t)i] = ArpSID::kParamInfos[(size_t)i].defaultNorm;
    p[(size_t)ArpSID::kParamVCO1Waveform] = 0.0f; // triangle
    p[(size_t)ArpSID::kParamVCO1Level] = 1.0f;
    p[(size_t)ArpSID::kParamAttack] = 0.0f;
    p[(size_t)ArpSID::kParamDecay] = 0.1f;
    p[(size_t)ArpSID::kParamSustain] = 0.8f;
    p[(size_t)ArpSID::kParamRelease] = 0.2f;
    p[(size_t)ArpSID::kParamSynthModeEnable] = 1.0f;
    return p;
}

static void unison_release_non_sounding_note_must_not_retrigger() {
    ArpSID::VoiceAllocator vp;
    vp.setPlayMode(ArpSID::PlayMode::Unison);
    vp.setNotePriority(ArpSID::NotePriority::High);
    vp.setUnisonCount(3);
    ArpSID::VoiceEventBuffer out{};
    vp.noteOn(60, 0.7f, 0, 10, out);
    out.reset();
    vp.noteOn(72, 0.8f, 0, 11, out);
    require(countKind(out, ArpSID::VoiceEvent::Kind::Glide) + countKind(out, ArpSID::VoiceEvent::Kind::Retrigger) > 0,
            "unison high-priority note-on did not move active unison voices");
    out.reset();
    vp.noteOff(60, 0, 10, out);
    require(out.count == 0, "releasing non-sounding held unison note emitted retrigger/glide/gate-off");
}

static void synth_mode_token_noteoff_must_allow_held_fallback_glide() {
    ArpSID::VoiceAllocator vp;
    vp.setPlayMode(ArpSID::PlayMode::Mono);
    vp.setNotePriority(ArpSID::NotePriority::Last);
    ArpSID::VoiceEventBuffer ev{};
    vp.noteOn(60, 0.7f, 0, 10, ev);
    const uint64_t tok60 = ev.events[0].voiceToken;
    ev.reset();
    vp.noteOn(64, 0.8f, 0, 11, ev);
    const uint64_t tok64 = ev.events[0].voiceToken;

    ArpSID::SynthModeVoices3 voices{};
    voices[0].active = true;
    voices[0].keyDown = true;
    voices[0].midiNote = 64;
    voices[0].channel = 0;
    voices[0].noteId = 11;
    voices[0].voiceToken = tok64;
    voices[0].currentSidFreqReg = 1000;
    voices[0].targetSidFreqReg = 1000;

    TestQueue q;
    uint8_t shadow[32]{};
    auto params = defaultParams();
    ArpSID::PatchStartPolicy pol{};
    int hardOffs = 0;
    ArpSID::scheduleSynthModeNoteOff(vp, voices, q, pol, params.data(), shadow,
                                     44100.0, 985248.0, 1,
                                     64, 0, 0, 0, 11,
                                     [&](int, uint16_t, uint16_t, bool){ ++hardOffs; }, tok64);
    require(hardOffs == 0, "token path hard-gated mono voice before fallback glide");
    require(voices[0].active && voices[0].keyDown, "fallback voice is not active/keyDown after note-off");
    require(voices[0].midiNote == 60, "fallback glide did not restore previous held MIDI note");
    require(voices[0].voiceToken == tok60, "fallback glide did not restore previous held token");
    require(!q.writes.empty(), "fallback glide generated no SID writes");
}

int main() {
    unison_release_non_sounding_note_must_not_retrigger();
    synth_mode_token_noteoff_must_allow_held_fallback_glide();
    return 0;
}
