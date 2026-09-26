// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include "parameter_ids.h"

namespace ArpSID {

template <class Target>
inline void runtimeHandleRenderedCC(Target& t, uint8_t ch, uint8_t cc, uint8_t val) noexcept {
    t.runtimeObserveDefaultEventChannel(static_cast<int>(ch));
    const float n = static_cast<float>(val) / 127.0f;
    const int c = std::clamp(static_cast<int>(ch), 0, 15);
    switch (cc) {
        case 1:
            t.runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(static_cast<int>(kParamHostCtrlModWheelBase) + c), n);
            t.runtimeHostSurface().prevModWheel[(size_t)c] = n;
            t.runtimeModel().setModWheelNorm(n, true);
            t.runtimeApplyNormalizedParameter(kParamFilterLFOAmount, n);
            break;
        case 2:
            t.runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(static_cast<int>(kParamHostCtrlBreathBase) + c), n);
            t.runtimeApplyNormalizedParameter(kParamFilterCutoff, n);
            break;
        case 4:
            t.runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(static_cast<int>(kParamHostCtrlExpressionBase) + c), n);
            t.runtimeHostSurface().prevExpression[(size_t)c] = n;
            t.runtimeApplyNormalizedParameter(kParamMasterVolume, n);
            break;
        case 7:
            t.runtimeApplyNormalizedParameter(kParamMasterVolume, n);
            break;
        case 10:
            break;
        case 11:
            t.runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(static_cast<int>(kParamHostCtrlExpressionBase) + c), n);
            t.runtimeHostSurface().prevExpression[(size_t)c] = n;
            t.runtimeApplyNormalizedParameter(kParamMasterVolume, n);
            break;
        case 64:
            t.runtimeHostSurface().prevSustain[(size_t)c] = val >= 64 ? 1.0f : 0.0f;
            t.runtimeSetSustainState(c, val >= 64);
            if (t.runtimeHasBitPerfectEngine()) t.runtimeBitPerfectSetSustainPedal(c, val >= 64);
            t.runtimeVoicePolicySetSustain(c, val >= 64);
            break;
        case 65:
            t.runtimeApplyNormalizedParameter(kParamPortamentoTime, val >= 64 ? 0.3f : 0.f);
            break;
        case 66:
            t.runtimeSetSostenutoState(c, val >= 64);
            if (t.runtimeHasBitPerfectEngine()) t.runtimeBitPerfectSetSostenutoPedal(c, val >= 64);
            t.runtimeVoicePolicySetSostenuto(c, val >= 64);
            break;
        case 67:
            t.runtimeApplySoftPedal(c, val >= 64);
            break;
        case 98: case 99: case 6: case 38:
            break;
        case 120: case 123:
            // P0 FIX: CC120 (AllSoundOff) and CC123 (AllNotesOff) are channel-scoped.
            // Only clear voices and controllers for channel `c`; do not trigger a global
            // kParamPanic which resets all 16 channels. The model-level clearing is done
            // here; the engine-level note-off is forwarded via runtimeAllNotesOff().
            t.runtimeModel().clearVoicesForChannel(c);
            t.runtimeModel().resetControllersForChannel(c);
            t.runtimeSetSustainState(c, false);
            t.runtimeSetSostenutoState(c, false);
            if (t.runtimeHasBitPerfectEngine()) t.runtimeBitPerfectSetSustainPedal(c, false);
            if (t.runtimeHasBitPerfectEngine()) t.runtimeBitPerfectSetSostenutoPedal(c, false);
            t.runtimeVoicePolicySetSustain(c, false);
            t.runtimeVoicePolicySetSostenuto(c, false);
            t.runtimeAllNotesOffChannel(c);  // physical engine: release voices on channel c only
            break;
        case 121:
            // CC121 (Reset All Controllers): per MIDI spec, channel-scoped.
            // P0 FIX: Replaced loop over all 16 channels with channel `c` only.
            t.runtimeApplyNormalizedParameter(kParamPortamentoTime, 0.f);
            t.runtimeSetSustainState(c, false);
            t.runtimeSetSostenutoState(c, false);
            if (t.runtimeHasBitPerfectEngine()) t.runtimeBitPerfectSetSustainPedal(c, false);
            if (t.runtimeHasBitPerfectEngine()) t.runtimeBitPerfectSetSostenutoPedal(c, false);
            t.runtimeVoicePolicySetSustain(c, false);
            t.runtimeVoicePolicySetSostenuto(c, false);
            break;
        default:
            break;
    }
}

template <class Target>
inline void runtimeHandleRenderedPitchBend(Target& t, uint8_t ch, int16_t bend14) noexcept {
    const int c = std::clamp(static_cast<int>(ch), 0, 15);
    const int raw14Signed = std::clamp(static_cast<int>(bend14), -8192, 8191);
    const float signedNorm = std::clamp(static_cast<float>(raw14Signed) / 8192.0f, -1.0f, 1.0f);
    const float norm01 = std::clamp((signedNorm + 1.0f) * 0.5f, 0.0f, 1.0f);
    t.runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(static_cast<int>(kParamHostCtrlPitchBendBase) + c), norm01);
    t.runtimeHostSurface().pitchBendNorm = norm01;
    t.runtimeHostSurface().currentPitchBendNorm[(size_t)c] = norm01;
    t.runtimeModel().setPitchBendNorm(c, signedNorm);
    if (t.runtimeHasBitPerfectEngine()) t.runtimeBitPerfectSetPitchBend14(c, raw14Signed + 8192);
}


template <class Target>
inline void runtimeContinueRenderedPitchBend(Target& t, uint8_t ch, int16_t bend14) noexcept {
    const int c = std::clamp(static_cast<int>(ch), 0, 15);
    const int raw14Signed = std::clamp(static_cast<int>(bend14), -8192, 8191);
    const float signedNorm = std::clamp(static_cast<float>(raw14Signed) / 8192.0f, -1.0f, 1.0f);
    const float norm01 = std::clamp((signedNorm + 1.0f) * 0.5f, 0.0f, 1.0f);
    t.runtimeHostSurface().pitchBendNorm = norm01;
    t.runtimeHostSurface().currentPitchBendNorm[(size_t)c] = norm01;
    t.runtimeModel().setPitchBendNorm(c, signedNorm);
}

template <class Target>
inline void runtimeHandleRenderedChannelPressure(Target& t, uint8_t ch, uint8_t pressure) noexcept {
    const int c = std::clamp(static_cast<int>(ch), 0, 15);
    const float norm = std::clamp(static_cast<float>(pressure) / 127.f, 0.0f, 1.0f);
    t.runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(static_cast<int>(kParamHostCtrlChannelPressureBase) + c), norm);
    t.runtimeHostSurface().channelPressure = norm;
    t.runtimeHostSurface().currentChannelPressure[(size_t)c] = norm;
    t.runtimeHostSurface().prevChannelPressure[(size_t)c] = norm;
    t.runtimeModel().setChannelPressureNorm(c, norm);
}

template <class Target>
inline void runtimeHandleRenderedPolyPressure(Target& t, uint8_t ch, uint8_t note, uint8_t pressure) noexcept {
    const int c = std::clamp(static_cast<int>(ch), 0, 15);
    const int n = std::clamp(static_cast<int>(note), 0, 127);
    auto& runtime = t.runtimeModel();
    runtime.setLastPolyPressureNote(c, n);
    runtime.setLastNoteState(n, c, runtime.lastNoteVelocity());
}

template <class Target>
inline void runtimeHandleRenderedPolyPressureIdentity(Target& t, uint8_t ch, uint8_t note, int32_t noteId, uint8_t pressure) noexcept {
    const int c = std::clamp(static_cast<int>(ch), 0, 15);
    const int n = std::clamp(static_cast<int>(note), 0, 127);
    const float norm = std::clamp(static_cast<float>(pressure) / 127.f, 0.0f, 1.0f);
    auto& runtime = t.runtimeModel();
    runtime.setLastPolyPressureNote(c, n);
    runtime.setLastNoteState(n, c, runtime.lastNoteVelocity());
    const uint64_t tok = runtime.resolveVoiceTokenForIdentity(c, n, noteId);
    if (tok != 0) runtime.bindPolyPressureToToken(tok, norm);
}

} // namespace ArpSID
