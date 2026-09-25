#pragma once
#include "arpsid/engines/bitperfect_engine.h"
#include "arpsid/engines/drsid_engine.h"
#include "arpsid/engines/arpeggiator.h"
#include "arpsid/engines/sid_register_engine.h"
#include "arpsid/modulation/lfo.h"
#include "sid_runtime_voice_policy.h"
#include "sid_runtime_synth_state.h"
#include "sid_runtime_pedal_state.h"
#include "sid_runtime_host_surface.h"
#include "sid_runtime_register_shadow.h"
#include "sid_runtime_register_shadow_ops.h"
#include <memory>

namespace ArpSID {

struct SidRuntimeEngineBank {
    std::unique_ptr<BitPerfectEngine> bitPerfect{};
    std::unique_ptr<Arpeggiator> arp{};
    std::unique_ptr<DrSidEngine> drSid{};
    std::unique_ptr<LFOBank> lfo{};
    VoiceAllocator voicePolicy{};
    SidRegisterEngine sidRegister{};   // chip 0 (primary)
    SidRegisterEngine sidRegister2{};  // chip 1 (2nd SID)
    SidRegisterEngine sidRegister3{};  // chip 2 (3rd SID)
    SidRegisterEngine sidRegister4{};  // chip 3 (4th SID)
    SidRegisterEngine sidRegister5{};  // chip 4 (5th SID)
    SidWriteQueue sidWriteQueue{};
    SynthModeVoices3 synthVoices{};
    SidRuntimePedalState pedalState{};
    SidRuntimeHostSurface hostSurface{};
    SidRuntimeRegisterShadow registerShadow{};

    void create(double sampleRate) {
        bitPerfect = std::make_unique<BitPerfectEngine>();
        arp = std::make_unique<Arpeggiator>();
        drSid = std::make_unique<DrSidEngine>();
        lfo = std::make_unique<LFOBank>();
        prepare(sampleRate);
        if (arp) arp->setInstanceSeed(0xAB3D1F7Eu);
        if (lfo) lfo->setInstanceSeed(0xC0FFEE12u);
    }

    void prepare(double sampleRate) noexcept {
        const double sr = (sampleRate > 0.0) ? sampleRate : 44100.0;
        if (bitPerfect) bitPerfect->setSampleRate(sr);
        if (arp) arp->setSampleRate(sr);
        if (drSid) drSid->setSampleRate(sr);
        if (lfo) lfo->setSampleRate(sr);
        sidRegister.prepare(sr);
        sidRegister2.prepare(sr);
        sidRegister3.prepare(sr);
        sidRegister4.prepare(sr);
        sidRegister5.prepare(sr);
        sidWriteQueue.clear();
        for (auto& v : synthVoices) v.reset();
        pedalState = SidRuntimePedalState{};
        hostSurface.resetIngress();
        hostSurface.hostTempo = 120.0;
        hostSurface.transportPlaying = false;
        hostSurface.hostBeatPosition = 0.0;
        hostSurface.lastPublishedBeatPosition = 0.0;
        hostSurface.hasPublishedBeatPosition = false;
        hostSurface.pitchBendNorm = 0.5f;
        hostSurface.channelPressure = 0.0f;
        for (auto& v : hostSurface.currentPitchBendNorm) v = 0.5f;
        for (auto& v : hostSurface.currentChannelPressure) v = 0.0f;
        for (auto& v : hostSurface.softPedalSavedCutoff) v = 0.0f;
        registerShadow.invalidate();
        voicePolicy.reset();
    }

    void resetOwned() noexcept {
        bitPerfect.reset();
        arp.reset();
        drSid.reset();
        lfo.reset();
        voicePolicy.reset();
    }
};

} // namespace ArpSID
