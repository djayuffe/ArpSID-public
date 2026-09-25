#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

#include "parameter_ids.h"
#include "sid_runtime_model.h"
#include "sid_runtime_engine_bank.h"

namespace ArpSID {

template <class Target>
inline void runtimeRenderHostApplySoftPedal(Target& target,
                                            int channel,
                                            bool on,
                                            std::array<float, 16>& savedCutoff) noexcept {
    const int ch = std::clamp(channel, 0, 15);
    const float current = std::clamp(target.runtimeParameterValues()[(size_t)kParamFilterCutoff], 0.0f, 1.0f);
    if (on) {
        savedCutoff[(size_t)ch] = current;
        target.runtimeExecutionOwner().applyProjectedNormalizedParameter(kParamFilterCutoff,
            std::max(0.0f, current - 0.15f));
    } else {
        target.runtimeExecutionOwner().applyProjectedNormalizedParameter(kParamFilterCutoff,
            std::clamp(savedCutoff[(size_t)ch], 0.0f, 1.0f));
    }
}


template <class Target>
inline void runtimeRenderHostAllNotesOff(Target& target) noexcept {
    auto& bank = target.runtimeEngineBank();
    if (bank.bitPerfect) bank.bitPerfect->allNotesOff();
    if (bank.drSid) bank.drSid->allNotesOff();
    target.runtimeHardSynthAllNotesOffChannel(-1);
    if (bank.arp) bank.arp->allNotesOff();
    if (bank.lfo) bank.lfo->reset();
}

// Channel-scoped variant: silence only voices on `channel` in the BitPerfect
// engine. The arp engine and LFO are not affected since they are global.
// Falls back to the full allNotesOff when channel < 0.
template <class Target>
inline void runtimeRenderHostAllNotesOffChannel(Target& target, int channel) noexcept {
    auto& bank = target.runtimeEngineBank();
    if (channel < 0) { runtimeRenderHostAllNotesOff(target); return; }
    if (bank.bitPerfect) bank.bitPerfect->allNotesOffChannel(channel);
    if (bank.drSid) bank.drSid->allNotesOffChannel(channel);
    target.runtimeHardSynthAllNotesOffChannel(channel);
    // Arp state is currently global/channel-less. A channel-scoped host
    // AllNotesOff/AllSoundOff on the active MIDI channel must still clear
    // pending arp notes/gates; otherwise the next render block can replay or
    // retain a channel-less arp voice indefinitely. This is intentionally
    // conservative until the arp input buffer grows per-channel ownership.
    if (bank.arp) bank.arp->allNotesOff();
}

template <class Target>
inline void runtimeRenderHostResetEngines(Target& target, bool resetDrSid = true) noexcept {
    auto& bank = target.runtimeEngineBank();
    runtimeRenderHostAllNotesOff(target);
    // A hard DrSID reset() at the transport Stop->Play boundary silences a
    // cowbell/tom hit that lands on the same block: the freshly-reset engine
    // renders those choke-family, filter+pitch-swept voices (SID voice 0/1) as
    // zero through the fractional interval path, while the same reset+hit renders
    // fine via the slice/processBlock path. DrSID drums are one-shots and the
    // engine already preserves its own transport state (v950), so the transport
    // boundary only needs a note release, not a destructive reset. Panic still
    // passes resetDrSid=true for a full wipe.
    if (bank.drSid) {
        if (resetDrSid) bank.drSid->reset();
        else            bank.drSid->allNotesOff();
    }
    bank.sidRegister.reset();
    if (auto* vp = target.runtimeVoicePolicy()) vp->reset();
    // v872: resetting the concrete backends (DrSID/SID808/sidRegister) wipes the
    // config the projection's change-tracking cache (firstApply/lastMode/lastFamily
    // /last*) believes is already applied. Without re-arming, the next
    // projectStateToBackends() sees "nothing changed" and skips rebuilding the
    // freshly-reset backend — so after a Stop→Play (or any engine reset) the drum
    // patch/model/kit is gone and stays silent until the user reapplies the patch.
    // Re-arm firstApply so the next projection does a FULL rebuild into the backend.
    target.runtimeProjectionState().firstApply = true;
}

template <class Target>
inline void runtimeRenderHostPanic(Target& target) noexcept {
    runtimeRenderHostResetEngines(target);
    // Clear soft-pedal saved cutoff — a panic must not leave stale filter state.
    auto& bank = target.runtimeEngineBank();
    for (auto& v : bank.hostSurface.softPedalSavedCutoff) v = 0.0f;
    if (auto* vp = target.runtimeVoicePolicy()) {
        VoiceEventBuffer panicEvents{};
        vp->panic(panicEvents);
    }
}

template <class Target>
inline void runtimeRenderHostHandleModeTransition(Target& target,
                                                  SidRuntimeRenderMode oldMode,
                                                  SidRuntimeRenderMode newMode) noexcept {
    auto& bank = target.runtimeEngineBank();
    if (oldMode != newMode) {
        // v940: a render-mode transition is a first-class authority boundary.
        // Silence every performance-note authority when crossing it so latent
        // BitPerfect/ARP/Synth voices cannot reappear after DrSID/Synth detours.
        if (bank.bitPerfect) bank.bitPerfect->allNotesOff();
        if (bank.drSid) bank.drSid->allNotesOff();
        if (bank.arp) bank.arp->allNotesOff();
        target.runtimeHardSynthAllNotesOffChannel(-1);
        if (bank.lfo) bank.lfo->reset();
        // v942: mode transitions are performance boundaries, not patch/kit
        // destruction boundaries. Do not call drSid->reset() here; it can wipe
        // the selected SID808/DrSID kit on BitPerfect<->DrSID transitions.
        // The transition only needs to silence active voices and clear canonical
        // note/SEQ mirrors. Backend projection will refresh structural config.
        target.runtimeModel().clearAllCanonicalVoiceState();
        target.runtimeModel().setSeqLastNote(-1);
        target.runtimeModel().setSeqSamplesUntilStep(-1.0);
        target.runtimeModel().setSeqStep(0);
    }
}

} // namespace ArpSID
