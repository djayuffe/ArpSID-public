// Copyright (C) 2024-2026 Ulf Bertilsson
// ─── ArpSIDAUEventTranslator.h ───────────────────────────────────────────────
// Phase 2: AU-specific event translation layer.
// Converts AURenderEvent linked list into canonical TimedEvent/EventBuffer.
// Also translates musicalContextBlock + transportStateBlock → TransportState.
//
// This is the ONLY place in the AU codebase that touches AU event types.
// ArpSIDAudioUnit.mm calls these functions; the kernel never sees AURenderEvent.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include "ArpSIDCanonicalEvents.h"
#include "../parameter_ids.h"
#include <AudioToolbox/AudioToolbox.h>
#include <algorithm>
#include <cstring>

namespace ArpSID {

// ── Translate the full AURenderEvent linked list into canonical events ────────
//
// Handles:
// AURenderEventMIDI → NoteOn/Off/CC/Bend/Pressure/ProgramChange
// AURenderEventMIDIEventList → UMP MIDI 1.0 CV and MIDI 2.0 CV note packets
// (macOS 12+)
// AURenderEventParameter → ParameterSet
// AURenderEventParameterRamp → ParameterRamp
// AURenderEventMIDISysEx → dropped (no semantics)
//
// Bad events are dropped or clamped — never crash.

inline void translateAUEvents(const AURenderEvent* head,
                               int frameCount,
                               uint32_t& orderCounter,
                               EventBuffer& out) noexcept {
    for (const AURenderEvent* ev = head; ev; ev = ev->head.next) {

        switch (ev->head.eventType) {

        // ── MIDI 1.0 ───────────────────────────────────────────────────────────
        case AURenderEventMIDI: {
            const AUMIDIEvent& m = ev->MIDI;
            if (m.length < 1 || m.length > 3) break;
            TimedEvent te{};
            te.sampleOffset = (int32_t)std::clamp((long long)m.eventSampleTime,
                                                   0LL, (long long)(frameCount-1));
            te.channel  = (uint8_t)(m.data[0] & 0x0Fu);
            te.rawOrder = ++orderCounter;

            const uint8_t status = m.data[0] & 0xF0u;
            switch (status) {
                case 0x90:
                    te.kind  = (m.length >= 3 && m.data[2] > 0) ? EventKind::NoteOn : EventKind::NoteOff;
                    te.pitch = (int16_t)(m.data[1] & 0x7Fu);
                    te.value = (m.length >= 3) ? std::clamp((float)(m.data[2] & 0x7Fu) / 127.f, 0.f, 1.f) : 0.f;
                    out.push(te);
                    break;
                case 0x80:
                    te.kind  = EventKind::NoteOff;
                    te.pitch = (int16_t)(m.data[1] & 0x7Fu);
                    te.value = (m.length >= 3) ? std::clamp((float)(m.data[2] & 0x7Fu) / 127.f, 0.f, 1.f) : 0.f;
                    out.push(te);
                    break;
                case 0xA0: // Poly pressure
                    if (m.length >= 3) {
                        te.kind  = EventKind::PolyPressure;
                        te.pitch = (int16_t)(m.data[1] & 0x7Fu);
                        te.value = std::clamp((float)(m.data[2] & 0x7Fu) / 127.f, 0.f, 1.f);
                        out.push(te);
                    }
                    break;
                case 0xB0: // CC
                    if (m.length >= 3) {
                        const uint8_t cc  = m.data[1] & 0x7Fu;
                        const float   val = std::clamp((float)(m.data[2] & 0x7Fu) / 127.f, 0.f, 1.f);
                        if (cc == 120 || cc == 123) {
                            te.kind = (cc == 120) ? EventKind::AllSoundOff : EventKind::AllNotesOff;
                        } else if (cc == 64) {
                            te.kind  = EventKind::ControlChange;
                            te.ccNum = 64;
                            te.value = (m.data[2] >= 64) ? 1.f : 0.f;
                        } else {
                            te.kind  = EventKind::ControlChange;
                            te.ccNum = cc;
                            te.value = val;
                        }
                        out.push(te);
                    }
                    break;
                case 0xC0: // Program change
                    // MIDI Program Change is GM/song metadata, not an ArpSID factory-preset selector.
                    // Treating it as kParamProgram made imported piano MIDI files reset the selected
                    // ArpSID patch on transport start/play. Host/GUI preset selection still flows
                    // through kParamProgram / PresentPreset; raw MIDI 0xC0 is intentionally ignored.
                    break;
                case 0xD0: // Channel pressure
                    if (m.length >= 2) {
                        te.kind  = EventKind::ChannelPressure;
                        te.value = std::clamp((float)(m.data[1] & 0x7Fu) / 127.f, 0.f, 1.f);
                        out.push(te);
                    }
                    break;
                case 0xE0: // Pitch bend
                    if (m.length >= 3) {
                        te.kind   = EventKind::PitchBend;
                        te.data14 = (uint16_t)(((uint16_t)(m.data[2] & 0x7Fu) << 7) |
                                                (uint16_t)(m.data[1] & 0x7Fu));
                        out.push(te);
                    }
                    break;
                default: break;
            }
            break;
        }

        // ── UMP MIDIEventList (Logic 10.8+ / macOS 12+) ───────────────────────
#if defined(AURenderEventMIDIEventList)
        case AURenderEventMIDIEventList: {
            const MIDIEventList* el = &ev->MIDIEventsList.eventList;
            const MIDIEventPacket* pkt = &el->packet[0];
            for (uint32_t pi = 0; pi < el->numPackets; ++pi) {
                if (pkt->wordCount == 0) {
                    pkt = MIDIEventPacketNext(pkt);
                    continue;
                }
                for (uint32_t wi = 0; wi < pkt->wordCount;) {
                    const uint32_t w = pkt->words[wi];
                    const uint8_t msgType = (w >> 28) & 0xFu;
                    TimedEvent te{};
                    te.sampleOffset = (int32_t)std::clamp((long long)ev->head.eventSampleTime,
                                                           0LL, (long long)(frameCount-1));
                    te.rawOrder = ++orderCounter;
                    if (msgType == 0x2u) {
                        const uint8_t sb = (w >> 16) & 0xFFu;
                        const uint8_t d1 = (w >>  8) & 0x7Fu;
                        const uint8_t d2 =  w         & 0x7Fu;
                        te.channel = sb & 0x0Fu;
                        const uint8_t st = sb & 0xF0u;
                        switch (st) {
                            case 0x90:
                                te.kind  = (d2 > 0) ? EventKind::NoteOn : EventKind::NoteOff;
                                te.pitch = d1; te.value = d2 / 127.f; out.push(te); break;
                            case 0x80:
                                te.kind  = EventKind::NoteOff;
                                te.pitch = d1; te.value = d2 / 127.f; out.push(te); break;
                            case 0xA0:
                                te.kind  = EventKind::PolyPressure;
                                te.pitch = d1; te.value = d2 / 127.f; out.push(te); break;
                            case 0xB0:
                                if (d1 == 120u || d1 == 123u) {
                                    te.kind = (d1 == 120u) ? EventKind::AllSoundOff : EventKind::AllNotesOff;
                                } else {
                                    te.kind  = EventKind::ControlChange;
                                    te.ccNum = d1;
                                    te.value = d2 / 127.f;
                                }
                                out.push(te); break;
                            case 0xC0:
                                // Raw MIDI Program Change must not select/overwrite the ArpSID factory patch.
                                break;
                            case 0xD0:
                                te.kind  = EventKind::ChannelPressure; te.value = d1 / 127.f; out.push(te); break;
                            case 0xE0: {
                                te.kind   = EventKind::PitchBend;
                                te.data14 = (uint16_t)(((uint16_t)d2 << 7) | d1); out.push(te); break;
                            }
                            default: break;
                        }
                        ++wi;
                    } else if (msgType == 0x4u) {
                        if (wi + 1u >= pkt->wordCount) break;
                        const uint32_t w2 = pkt->words[wi + 1u];
                        const uint8_t sb = (w >> 16) & 0xFFu;
                        const uint8_t d1 = (w >>  8) & 0x7Fu;
                        const uint16_t value16 = static_cast<uint16_t>((w2 >> 16) & 0xFFFFu);
                        const float value = std::clamp(static_cast<float>(value16) / 65535.0f, 0.0f, 1.0f);
                        te.channel = sb & 0x0Fu;
                        const uint8_t st = sb & 0xF0u;
                        switch (st) {
                            case 0x90:
                                te.kind  = (value16 > 0u) ? EventKind::NoteOn : EventKind::NoteOff;
                                te.pitch = d1; te.value = value; out.push(te); break;
                            case 0x80:
                                te.kind  = EventKind::NoteOff;
                                te.pitch = d1; te.value = value; out.push(te); break;
                            default:
                                break;
                        }
                        wi += 2u;
                    } else {
                        ++wi;
                    }
                }
                pkt = MIDIEventPacketNext(pkt);
            }
            break;
        }
#endif

        // ── Parameter set ─────────────────────────────────────────────────────
        case AURenderEventParameter: {
            const AUParameterEvent& pe = ev->parameter;
            const int addr = (int)pe.parameterAddress;
            if (addr < 0 || addr >= (int)kNumParams) break;
            // Logic can emit render-time automation/metadata events for Program
            // and BankSlot exactly when transport starts. These are not explicit
            // ArpSID preset selection; if they enter the canonical render stream
            // the audio patch can become 0 while GUI/readback remains sticky.
            // Explicit preset APIs apply a SidStateRoot instead.
            if (addr == kParamProgram || addr == kParamBankSlot) break;
            const float val = std::clamp((float)pe.value, 0.f, 1.f);
            if (!std::isfinite(val)) break;
            TimedEvent te{};
            te.sampleOffset = (int32_t)std::clamp((long long)ev->head.eventSampleTime,
                                                   0LL, (long long)(frameCount-1));
            te.kind   = EventKind::ParameterSet;
            te.target = (uint32_t)addr;
            te.value  = val;
            te.value_f32 = val;
            te.rawOrder = ++orderCounter;
            out.push(te);
            break;
        }

        // ── Parameter ramp ────────────────────────────────────────────────────
        case AURenderEventParameterRamp: {
            const AUParameterEvent& pe = ev->parameter;
            const int addr = (int)pe.parameterAddress;
            if (addr < 0 || addr >= (int)kNumParams) break;
            // Same authority rule as ParameterSet: render-time Program/BankSlot
            // ramps are host metadata replay, not factory-preset commands.
            if (addr == kParamProgram || addr == kParamBankSlot) break;
            const float val = std::clamp((float)pe.value, 0.f, 1.f);
            if (!std::isfinite(val)) break;
            TimedEvent te{};
            te.sampleOffset = (int32_t)std::clamp((long long)ev->head.eventSampleTime,
                                                   0LL, (long long)(frameCount-1));
            te.kind   = EventKind::ParameterRamp;
            te.target = (uint32_t)addr;
            te.value  = val;
            te.value_f32 = val;
            te.data14 = (uint16_t)std::min((uint32_t)pe.rampDurationSampleFrames, (uint32_t)16383u);
            te.rawOrder = ++orderCounter;
            out.push(te);
            break;
        }

        case AURenderEventMIDISysEx:
        default:
            break;
        }
    }
}

// ── Build TransportState from AU host context blocks ─────────────────────────
} // namespace ArpSID
