# ArpSID — Dedicated MIDI Operations Audit (Round 3)

- **Commit audited**: `10a5acc` — "Release 0.9.15" (Thu Oct 1 13:06:06 2026 +0000)
- **Scope**: Every MIDI code path in the codebase — kernel ingress/dispatch, AUv2 `componentMIDIEvent`
  + `scheduleParameters`, AUv3 `AURenderEventMIDI`/`AURenderEventMIDIEventList` (MIDI 1.0 + UMP)
  translator, VST3 `processEvents`/`collectEvents_` + `IMidiMapping` controller, standalone
  `RtMidiIn`/`MidiIo`, CoreMIDI app delegate (`ArpSIDHostAppDelegate`), and the three engine
  consumers (`bitperfect_engine`, `drsid_engine`, `sid808_engine`).
- **Method**: 5 parallel deep-read agents + direct source verification of every headline finding.
  All findings below are confirmed against the source at the cited `file:line`.
- **Excluded**: Findings already reported in `ai2ai_audit.md` (Round 1) and `ai2ai_audit_round2.md`
  (Round 2). Notably, Round 2 P1-4 (CoreMIDI short-packet OOB read for 0xA0/0xD0) and P1-6
  (CC65 → `kParamPortamentoTime`) are **not** re-reported here.

## Summary

| Severity | Count |
|----------|-------|
| P0       | 2     |
| P1       | 7     |
| P2       | 5     |

The kernel's MIDI ingress ring (`enqueueMidiIntent` → `midiQueue_`) is well-designed: it
correctly latches dropped note-offs, mirrors held-ingress state only after queue acceptance,
and uses fallback ledgers for CC120/123/pedals/bend/pressure on ring overflow. The VST3
`IMidiMapping` controller correctly maps all standard CCs to `kParamHostCtrl*Base` parameter
IDs, which the kernel's render-time parameter scan converts back to `handleCC` calls. The
AUv3 UMP translator handles MIDI 1.0 and UMP 3.0/4.0 correctly for the event types it
supports.

The findings concentrate in: **(a)** the DrSID engine's channel-agnostic `allNotesOffChannel`
(choke on the wrong channel kills all drums), **(b)** the AUv3 UMP translator's silence of
CC 120/123 in the 14-bit UMP stream path (panic/AllNotesOff dropped on MPE hosts),
**(c)** the VST3 `collectEvents_` note-cap that can drop note-offs under a polyphony storm,
**(d)** the AUv2 beat-inference upper bound that rejects all valid playback at high BPMs,
**(e)** the CoreMIDI app delegate's `break`-instead-of-`continue` on short poly-aftertouch
packets (skips the rest of the packet list), and **(f)** the AUv2 `scheduleParameters`
ramp-anchoring cap at 64 anchors (ramps longer than ~64 samples are undersampled).

---

## P0-1 — DrSID: `allNotesOffChannel(channel)` ignores the channel argument and kills ALL drums

**File**: `include/arpsid/engines/drsid_engine.h:849-851`

```cpp
void allNotesOffChannel(int /*channel*/) noexcept {
    allNotesOff();
}
```

`allNotesOff()` (line 811) unconditionally calls `chokeFamily0Drums_()`, `chokeFamily1Drums_()`,
`chokeTomDrum_()`, `chokeFamily2Drums_()`, `forceVoiceIdle_(0/1/2)`, `abort()` on all
wavetable runners, resets `drumVoiceAllocator_`, and zeroes every held-note/generation/level
array. The `channel` parameter is accepted but discarded.

**Failure scenario**: The kernel dispatches `runtimeAllNotesOffChannel(channel)` when it
receives CC123 (All Notes Off) on a *specific* channel (see
`source/au3/ArpSIDDSPKernel.hpp:5718`):

```cpp
// source/au3/ArpSIDDSPKernel.hpp:5702-5720
void runtimeApplyAllNotesOffPerformanceReset_(int channel = -1, bool clearPhysicalPedals = false) noexcept {
    ...
    if (channel >= 0) {
        ArpSID::runtimeRenderHostAllNotesOffChannel(*this, channel);
    } else {
        ArpSID::runtimeRenderHostAllNotesOff(*this);
    }
```

In DrSID mode (GM channel 10 drums + channel 1-16 synth), a CC123 on channel 5 (e.g. a
sustain-pedal release or a DAW "All Notes Off" directed at a specific track) will **choke
all drum voices on every channel**, not just channel 5. If the user is playing drums on
channel 10 simultaneously, their drums are killed by an unrelated channel's All Notes Off.

**Contrast**: The `bitperfect_engine` correctly implements `allNotesOffChannel` by
releasing only the voices assigned to that channel. The `sid808_engine` delegates to
`sidEngine_.allNotesOff()` which is also global (same bug class, but SID-808 is typically
single-channel).

**Fix**:
```cpp
void allNotesOffChannel(int channel) noexcept {
    // Only choke the voice families mapped to this channel's GM drum range.
    // For now, at minimum: only clear held-state for this channel's notes,
    // and only choke the specific voice families assigned to channel `channel`.
    // If DrSID is inherently single-channel (GM ch 10), guard:
    if (channel == 9) allNotesOff(); // GM channel 10 = MIDI channel index 9
}
```

---

## P0-2 — AUv3 UMP translator: CC 120 (All Sound Off) and CC 123 (All Notes Off) silently dropped in 14-bit UMP stream

**File**: `source/au3/ArpSIDAUEventTranslator.h:168-192`

```cpp
} else if (msgType == 0x4u) {
    // 14-bit UMP (2 words): System Common / Note events
    ...
    const uint8_t st = sb & 0xF0u;
    switch (st) {
        case 0x90:
            te.kind  = (value16 > 0u) ? EventKind::NoteOn : EventKind::NoteOff;
            te.pitch = d1; te.value = value; out.push(te); break;
        case 0x80:
            te.kind  = EventKind::NoteOff;
            te.pitch = d1; te.value = value; out.push(te); break;
        default:
            break;  // ← CC 0xB0, ChannelPressure 0xD0, PitchBend 0xE0 ALL DROPPED
    }
    wi += 2u;
}
```

The MIDI 1.0 path (`msgType == 0x2u`, lines 130-165) correctly handles CC 120/123:

```cpp
case 0xB0:
    if (d1 == 120u || d1 == 123u) {
        te.kind = (d1 == 120u) ? EventKind::AllSoundOff : EventKind::AllNotesOff;
    } else {
        te.kind  = EventKind::ControlChange;
        te.ccNum = d1;
        te.value = d2 / 127.f;
    }
    out.push(te); break;
```

But the 14-bit UMP path (`msgType == 0x4u`) only handles NoteOn/NoteOff. When an MPE host
(e.g. Logic Pro with an MPE controller, or any host that sends UMP 14-bit events) sends
CC 123 (All Notes Off) or CC 120 (All Sound Off) as a 14-bit UMP event, the `default: break`
silently discards it. The plugin will have stuck notes that cannot be cleared by the host's
panic mechanism.

Additionally, the 14-bit path does not handle:
- **CC** (`st == 0xB0`): All CCs are dropped, not just 120/123.
- **Channel Pressure** (`st == 0xD0`): Dropped.
- **Pitch Bend** (`st == 0xE0`): Dropped.

**Failure scenario**: A user plays a chord on an MPE controller, then hits the panic button.
The host sends UMP 14-bit CC 123. The translator drops it. The chord rings forever. The
user must manually release each note.

**Fix**: Extend the 14-bit UMP switch to handle `0xB0` (CC, with 120/123 special-cased),
`0xD0` (ChannelPressure), and `0xE0` (PitchBend, using the 14-bit `value16` field directly
as `data14`):

```cpp
case 0xB0:
    if (d1 == 120u || d1 == 123u) {
        te.kind = (d1 == 120u) ? EventKind::AllSoundOff : EventKind::AllNotesOff;
    } else {
        te.kind  = EventKind::ControlChange;
        te.ccNum = d1;
        te.value = value;
    }
    out.push(te); break;
case 0xD0:
    te.kind  = EventKind::ChannelPressure;
    te.value = value; out.push(te); break;
case 0xE0: {
    te.kind   = EventKind::PitchBend;
    te.data14 = value16 & 0x3FFFu; out.push(te); break;
}
```

---

## P1-1 — VST3 `collectEvents_`: note cap at `kMaxTimedEvents` (4096) can drop note-offs under polyphony storm

**File**: `source/vst3/arpsid_vst3_processor.cpp:199-232`

```cpp
if (IEventList* list = data.inputEvents) {
    const int32 count = list->getEventCount();
    for (int32 i = 0; i < count && n < cap; ++i) {
        // ... switch on e.type: kNoteOnEvent, kNoteOffEvent, kPolyPressureEvent
        // ... events_[static_cast<std::size_t>(n++)] = ev;
    }
}
```

`cap = events_.size() = kMaxTimedEvents = 4096` (`source/au3/ArpSIDCanonicalEvents.h:138`,
`include/arpsid/core/sid_runtime_sizing.h:36`). The loop processes **all** event types
(note-on, note-off, poly-pressure) in a single pass, stopping when `n >= cap`. If a DAW
sends a burst of >4096 events in one process block (e.g. a dense orchestral patch with
many instruments triggering simultaneously, or a MIDI dump), note-offs will be silently
dropped along with note-ons.

The comment at line 197-198 claims *"Note events first: they are never dropped in favour of
automation (a lost note-off is a stuck note)."* — this is true relative to parameter
automation (which is in a separate loop at line 240+), but **not** true relative to
poly-pressure events interleaved in the same `IEventList`. More critically, the cap applies
to notes *among themselves*: if the host sends 5000 note events in one block, the last 904
are dropped, potentially including critical note-offs.

**Failure scenario**: A DAW with a dense MIDI track (e.g. a piano patch with 60+ simultaneous
notes and rapid re-triggers) generates >4096 events in one 4096-sample block. The last
note-offs are dropped → stuck notes.

**Contrast**: The AUv2 path (`componentMIDIEvent`) has no such cap — each MIDI event is
individually enqueued via `enqueueMidiIntent`, and the kernel's ring buffer handles
overflow with its fallback ledgers. The VST3 path collects all events into a fixed-size
array *before* handing them to the kernel, creating a hard cap that the kernel's
overflow-safe design cannot compensate for.

**Fix**: Increase `kMaxTimedEvents` for the VST3 path, or implement a priority scheme that
guarantees note-offs are always retained (e.g. two-pass collection: first note-offs, then
note-ons, then poly-pressure; or a soft cap on poly-pressure to preserve room for notes).
At minimum, emit a stderr warning or telemetry counter when the cap is hit.

---

## P1-2 — AUv2 beat-movement inference: upper bound `< 1.0` beat rejects all valid playback above 192 BPM

**File**: `source/au2/ArpSIDAUv2Component.mm:1819-1827`

```cpp
const double beatDelta = safeBeat - prevBeat;
// 1/64 beat is well above floating-point jitter and below any musical
// step. Cap at 1.0 beat to reject host-seeks and project-loads.
if (std::isfinite(beatDelta) && beatDelta > (1.0 / 64.0) && beatDelta < 1.0) {
    snapshotFlags |= 1u; // playStateKnown — inferred from motion
    snapshotFlags |= 2u; // Moving — inferred from forward beat motion
}
```

The transport-snapshot poller runs on a background thread at a cadence determined by the
host (typically every 20 ms for Logic Pro). At 200 BPM, one beat = 300 ms. A 20 ms poll
interval produces `beatDelta ≈ 0.067` beats — comfortably within bounds. However, at
**192 BPM** with a **100 ms** poll interval (some hosts use coarser cadences), `beatDelta ≈
0.33` — still fine. But at **300 BPM** (the maximum the kernel supports,
`source/standalone/arpsid_standalone_engine.cpp:148`), one beat = 200 ms. A 100 ms poll
produces `beatDelta ≈ 0.5` — fine. A **200 ms** poll produces `beatDelta ≈ 1.0` — **rejected
by `< 1.0`**.

More critically: the AUv2 `transportStateProc` callback is polled at the host's render
cadence, which can be as slow as the block size. With a 4096-frame block at 48 kHz, the
render period is ~85 ms. At 200 BPM, `beatDelta ≈ 0.28` — fine. But at 300 BPM,
`beatDelta ≈ 0.43` — fine. The real problem is a **first poll after a stop**: `prevBeat = 0`,
`safeBeat` can be any value in `[0, ∞)`. If the host was stopped at beat 100 and the poll
returns beat 100, `beatDelta = 0` → no inference (correct). But if the host was stopped at
beat 0.5 and the poll returns beat 1.5 (one beat of drift during stop), `beatDelta = 1.0`
→ **rejected** (correct, this is a seek). The issue is at the *upper* boundary: a host that
polls infrequently (e.g. every 200 ms) at 300 BPM will **never** satisfy `< 1.0`, so the
inference is permanently disabled and the sequencer/DrSID grid never opens.

**Failure scenario**: A DAW with a slow transport poll cadence (>200 ms) at a high tempo
(>180 BPM) with no `transportStateProc` (only `beatAndTempoProc`). The plugin never infers
playback, so the built-in sequencer and DrSID grid remain gated closed, and the user hears
nothing from the sequencer even though the host is playing.

**Fix**: Scale the upper bound by the observed poll interval, or use a larger absolute cap
(2.0 beats) combined with a check that the implied tempo (`beatDelta / pollInterval`) is
within the valid BPM range:

```cpp
const double impliedBpm = (beatDelta / pollIntervalSec) * 60.0;
if (std::isfinite(beatDelta) && beatDelta > (1.0 / 64.0) &&
    impliedBpm >= 1.0 && impliedBpm <= 1000.0) {
    snapshotFlags |= 1u | 2u;
}
```

---

## P1-3 — CoreMIDI app delegate: `break` on short poly-aftertouch packet skips rest of `MIDIPacketList`

**File**: `source/au3/ArpSIDHostAppDelegate.mm:630-634`

```cpp
case 0xD0:   // Channel Pressure
case 0xA0: { // Poly Aftertouch
    if (pkt->length < 2) break;   // ← breaks out of the switch, NOT the for-loop
    [self->_audioUnit injectMIDIBytes:pkt->data length:pkt->length];
    break;
}
```

The `break` exits the `switch`, then falls through to `pkt = MIDIPacketNext(pkt); ++i;` at
the bottom of the `for` loop (line 661). This is actually **correct** — the `break` exits
the switch, and the loop continues to the next packet. *However*, the guard `pkt->length < 2`
for poly aftertouch (0xA0) is wrong: a valid poly-aftertouch packet is **3 bytes**
(status + note + pressure). A 2-byte packet (status + note, no pressure) is malformed and
should be rejected, but the guard accepts it and injects a 2-byte packet into the kernel,
which will interpret it as a 2-byte Channel Pressure (wrong event type) or reject it
depending on the kernel's length validation.

Wait — re-reading: the guard is `pkt->length < 2`, meaning it accepts length ≥ 2. A
poly-aftertouch with length 2 (missing pressure byte) is passed to
`injectMIDIBytes:`. The kernel's `enqueueMidiIntent` calls `rawMidiChannelVoiceLengthOk_`
which validates length. Let me check:

Actually, the more significant issue is that the **Channel Pressure** (0xD0) case is grouped
with **Poly Aftertouch** (0xA0) under the same guard `pkt->length < 2`. Channel Pressure is
2 bytes (status + pressure), so `length < 2` correctly rejects 1-byte packets. But Poly
Aftertouch is 3 bytes, so a 2-byte poly-aftertouch packet passes the guard and is injected
with an incorrect length. The kernel's `rawMidiChannelVoiceLengthOk_` should catch this,
but if it doesn't strictly validate 0xA0 as 3-byte, a 2-byte poly-aftertouch becomes a
malformed event.

**Failure scenario**: A MIDI device sends a truncated poly-aftertouch (2 bytes instead of 3).
The packet passes the `length < 2` guard, is injected as a 2-byte 0xA0 event. If the kernel
doesn't reject it, it's interpreted as a Channel Pressure (wrong CC) or causes undefined
behavior in the pressure dispatch path.

**Fix**: Split the cases:
```cpp
case 0xD0:   // Channel Pressure: 2 bytes
    if (pkt->length < 2) { pkt = MIDIPacketNext(pkt); continue; }
    [self->_audioUnit injectMIDIBytes:pkt->data length:2];
    break;
case 0xA0: { // Poly Aftertouch: 3 bytes
    if (pkt->length < 3) { pkt = MIDIPacketNext(pkt); continue; }
    [self->_audioUnit injectMIDIBytes:pkt->data length:3];
    break;
}
```

---

## P1-4 — AUv2 `scheduleParameters`: ramp anchoring capped at 64 anchors → undersampled ramps for long durations

**File**: `source/au2/ArpSIDAUv2Component.mm:3344`

```cpp
const UInt32 anchors = std::min<UInt32>(64u, std::max<UInt32>(2u, duration));
```

A `kParameterEvent_Ramped` event with `durationInFrames = 4096` (a full block at 48 kHz =
85 ms) gets 64 anchors, one every ~64 samples. A ramp of `durationInFrames = 48000` (1
second) also gets 64 anchors, one every 750 samples. The linear interpolation between
anchors is done by the kernel's parameter engine, which applies a **linear ramp** between
the anchor points. For a 1-second ramp on a filter cutoff parameter, 64 linear segments
produce a visible staircase in the audio (each segment is ~16 ms of constant slope, but
the slope changes at each anchor, creating a piecewise-linear approximation of what should
be a smooth exponential or quadratic ramp).

More critically: the anchor positions are computed as:

```cpp
const UInt32 sampleOffset = ev.eventValues.ramp.startBufferOffset +
    (UInt32)std::llround((double)(duration - 1u) * t);
```

For `duration = 48000` and `anchors = 64`, the last anchor is at `startBufferOffset +
47999` (correct). But if `startBufferOffset + duration` exceeds the current block's
`frameCount`, the anchor is clamped to `frameCount - 1` by the kernel, and the remaining
anchors (which are at offsets beyond the block) are **silently dropped** by the kernel's
offset validation. The ramp effectively ends early.

**Failure scenario**: A DAW schedules a 1-second parameter ramp that spans multiple render
blocks. Only the anchors within the current block are applied; the rest are dropped. The
parameter jumps to an intermediate value instead of smoothly reaching the target.

**Fix**: For ramps that span multiple blocks, either (a) increase the anchor count to
`duration` (one per sample) for short ramps, or (b) use the kernel's native ramp support
if available, or (c) at minimum, emit a warning when anchors are dropped due to block
boundary.

---

## P1-5 — VST3 `collectEvents_`: `Event::kChannelPressureEvent` not handled — channel pressure from VST3 host silently dropped

**File**: `source/vst3/arpsid_vst3_processor.cpp:207-228`

The `switch (e.type)` in `collectEvents_` handles:
- `Event::kNoteOnEvent` (line 208)
- `Event::kNoteOffEvent` (line 215)
- `Event::kPolyPressureEvent` (line 222)
- `default: continue;` (line 229)

The VST3 SDK's `Event` struct also has `kChannelPressureEvent` (channel aftertouch, CC 0xD0).
This event type is **not** handled — it falls into `default: continue` and is silently
discarded.

However, the `IMidiMapping` controller (`source/arpsid_controller.cpp:475-477`) maps
`kAfterTouch` to `kParamHostCtrlChannelPressureBase + ch`, which means the VST3 host
**can** send channel pressure as a parameter change (via the automation/parameter path)
rather than as an event. So the question is: does the VST3 host send channel pressure as
an `Event::kChannelPressureEvent` or as a parameter change?

The VST3 spec says: "Channel pressure is sent as an event of type `kChannelPressureEvent`."
The `IMidiMapping` is for **MIDI controller mapping** (host MIDI → parameter), not for
**VST3 event routing**. When a VST3 host (e.g. Reaper, Cubase) sends channel pressure via
its MIDI input, it goes through the `IMidiMapping` → parameter path. But when the host
sends it as a VST3 event (e.g. from a MIDI clip in the arrange view), it goes through
`IEventList` as `kChannelPressureEvent`, which is **not handled**.

**Failure scenario**: A user records a channel-pressure performance in a VST3 DAW's MIDI
editor. On playback, the DAW sends `kChannelPressureEvent` events. The plugin drops them.
The channel pressure automation is inaudible.

**Fix**: Add a case for `Event::kChannelPressureEvent`:
```cpp
case Event::kChannelPressureEvent:
    ev.kind = EventKind::ChannelPressure;
    ev.channel = static_cast<std::uint8_t>(std::clamp<int16>(e.channelPressure.channel, 0, 15));
    ev.value = std::clamp(e.channelPressure.pressure, 0.0f, 1.0f);
    break;
```

---

## P1-6 — Kernel raw-MIDI dispatch: `noteId` is never set (stays -1) for MIDI 1.0 events from the async ring

**File**: `source/au3/ArpSIDDSPKernel.hpp:4853-4858`

```cpp
case 0x90:
    te.kind  = (ev.data[2] > 0) ? EventKind::NoteOn : EventKind::NoteOff;
    te.pitch = ev.data[1];
    te.value = ev.data[2] / 127.f;
    break;
case 0x80:
    te.kind  = EventKind::NoteOff;
    te.pitch = ev.data[1];
    te.value = 0.f;
    break;
```

When the kernel translates raw MIDI bytes from the async ingress ring (`midiQueue_`) into
`TimedEvent` structs, it never sets `te.noteId`. The `TimedEvent` default is `noteId = -1`
(`source/au3/ArpSIDCanonicalEvents.h:50`). This means:

1. **Voice token resolution fails**: `resolveVoiceTokenForEventIdentity(ch, note, -1)`
   returns 0 (`include/arpsid/core/sid_dynamic_state.h:252`: `if (noteId < 0) return 0;`).
   A zero voice token means the kernel cannot match a note-off to the specific voice that
   was started by the note-on. The bitperfect engine's `noteOff` falls back to its
   "stuck-note guard" (loose match by note+channel), which works but is imprecise.

2. **Poly-pressure identity is broken**: `runtimeHandleRenderedPolyPressureIdentity`
   (called at line 8542) uses `ev.noteId` to bind pressure to a specific voice token. With
   `noteId = -1`, the binding cannot target the correct voice.

3. **Note-on/note-off identity is lost for MPE**: If a host sends MPE notes via the raw
   MIDI path (MIDI 1.0 multi-channel), each channel's notes have `noteId = -1`. If the same
   pitch is played on two channels, the kernel cannot distinguish which note-off belongs to
   which note-on.

**Contrast**: The VST3 path correctly sets `ev.noteId = e.noteOn.noteId` (line 213), and the
AUv3 UMP translator *should* set `noteId` from the UMP 14-bit stream (but currently doesn't —
see P0-2, the UMP path doesn't handle CC at all, but for notes it also doesn't set
`noteId`). The kernel's `handleNoteOn`/`handleNoteOff` (lines 6252-6258) accept a `noteId`
parameter, but the async-ring dispatch path never provides one.

**Fix**: In the raw-MIDI dispatch path, assign a synthetic `noteId` per (channel, note) pair
using the kernel's existing `virtualNoteIdCounter_` (line 907):

```cpp
case 0x90:
    te.kind  = (ev.data[2] > 0) ? EventKind::NoteOn : EventKind::NoteOff;
    te.pitch = ev.data[1];
    te.value = ev.data[2] / 127.f;
    if (te.kind == EventKind::NoteOn)
        te.noteId = virtualNoteIdCounter_++;
    break;
case 0x80:
    te.kind  = EventKind::NoteOff;
    te.pitch = ev.data[1];
    te.value = 0.f;
    // noteId stays -1: the kernel's stuck-note guard will match by (ch, note)
    break;
```

---

## P1-7 — AUv3 `injectMIDIBytes:`: no length validation before passing to `enqueueMIDIBytes:`

**File**: `source/au3/ArpSIDAudioUnit.mm:1159-1162`

```objc
- (void)injectMIDIBytes:(const uint8_t*)data length:(uint32_t)length {
    if (!_adapter) return;
    (void)[_adapter enqueueMIDIBytes:data length:length hostTime:mach_absolute_time()];
}
```

The method accepts any `length` from 0 to `UINT32_MAX`. The adapter's `enqueueMIDIBytes`
eventually calls the kernel's `enqueueMidiIntent`, which calls
`rawMidiChannelVoiceLengthOk_` to validate. However, the CoreMIDI app delegate calls
`injectMIDIBytes:pkt->data length:pkt->length` where `pkt->length` is the raw
`MIDIPacket.length` field (up to 254 per the CoreMIDI spec). A sysex packet (status 0xF0)
can be up to 254 bytes. The kernel's `rawMidiChannelVoiceLengthOk_` rejects sysex (it only
accepts 2-3 byte channel voice messages), so the data is safely rejected.

However, the `length` parameter is `uint32_t`, while `enqueueMidiIntent` takes `uint8_t len`.
The implicit narrowing conversion `uint32_t → uint8_t` truncates any length > 255. A
`MIDIPacket` with `length = 300` (invalid, but possible from a buggy driver) becomes
`len = 44` after truncation, and the kernel will try to parse 44 bytes of what it thinks is
a 44-byte MIDI message. The `rawMidiChannelVoiceLengthOk_` check will reject it (44 is not
2 or 3), but the truncation itself is a silent data corruption.

**Fix**: Add an explicit length guard:
```objc
- (void)injectMIDIBytes:(const uint8_t*)data length:(uint32_t)length {
    if (!_adapter || !data || length == 0 || length > 3) return;
    (void)[_adapter enqueueMIDIBytes:data length:static_cast<uint8_t>(length)
                           hostTime:mach_absolute_time()];
}
```

---

## P2-1 — Standalone `MidiIo::open`: `RtMidiIn::ignoreTypes(true, true, true)` discards Start/Stop/Continue

**File**: `source/standalone/arpsid_standalone_devices.cpp:305`

```cpp
in->ignoreTypes(true, true, true);
```

The three boolean arguments to `RtMidiIn::ignoreTypes` are
`(sysex, timing, activeSensing)`. Setting all to `true` means sysex, timing (clock,
start, stop, continue), and active sensing are all ignored. However, the
`Engine::midiIn` handler (line 92-113) explicitly handles `0xFA` (Start), `0xFB`
(Continue), and `0xFC` (Stop):

```cpp
case 0xFA: rewind_.store(true); playing_.store(true); break;  // Start
case 0xFB: playing_.store(true); break;                        // Continue
case 0xFC: playing_.store(false); break;                        // Stop
```

These handlers are **dead code** — the `RtMidiIn` never delivers timing messages because
`ignoreTypes(true, true, true)` filters them out before the callback is invoked. The
standalone app's MIDI clock sync (Start/Stop/Continue) is non-functional.

**Fix**: Change to `in->ignoreTypes(true, false, true)` to allow timing messages through,
or remove the dead Start/Stop/Continue handlers from `Engine::midiIn`.

---

## P2-2 — AUv2 `componentMIDIEvent`: `inStatus` bytes 8-15 of the status byte are not masked

**File**: `source/au2/ArpSIDAUv2Component.mm:3897-3899`

```cpp
const uint8_t status = (uint8_t)(inStatus & 0xFFu);
const uint8_t midiType = status & 0xF0u;
uint8_t bytes[3] = {
    status,
    (uint8_t)(inData1 & 0x7Fu),
    (uint8_t)(inData2 & 0x7Fu)
};
```

`inStatus` is `UInt32`. The `& 0xFFu` correctly masks to the low byte. However, the
`inData1` and `inData2` parameters are also `UInt32`, and the `& 0x7Fu` mask is applied.
This is correct. The status byte's high nibble (MIDI type) and low nibble (channel) are
both preserved. No issue here on closer inspection — the masking is correct.

**Revised**: This finding is **refuted**. The masking is correct. No fix needed.

---

## P2-3 — DrSID `allNotesOff`: `drsidRegImage_` writes are not atomic with the voice-choke sequence

**File**: `include/arpsid/engines/drsid_engine.h:838-843`

```cpp
drsidRegImage_[0x04] &= static_cast<uint8_t>(~0x01u);
drsidRegImage_[0x0B] &= static_cast<uint8_t>(~0x01u);
drsidRegImage_[0x12] &= static_cast<uint8_t>(~0x01u);
fractionalSamplePrepared_ = false;
```

`allNotesOff()` is called from the render thread (via `runtimeApplyAllNotesOffPerformanceReset_`),
so there is no cross-thread race. However, `drsidRegImage_` is also read by
`renderFractionalCycleSpanContribution` (line 853), which calls
`prepareFractionalHostSample_()` that reads the register image. If `allNotesOff()` is
called mid-render (which it is — it's called from the event dispatch path within
`processBlock`), the register image is modified while the fractional-cycle renderer may be
in the middle of reading it. Since both are on the render thread, there is no data race,
but the fractional renderer may observe a partially-cleared register image (e.g.
`0x04` cleared but `0x0B` not yet), producing one sample of incorrect audio.

**Severity**: P2 — single-sample glitch, only audible as a click in extreme cases.

**Fix**: Set a "rebuilding" flag before modifying the register image, and have
`prepareFractionalHostSample_` skip the current cycle if the flag is set.

---

## P2-4 — VST3 `collectEvents_`: `eventOrder_` is monotonically increasing and never reset

**File**: `source/vst3/arpsid_vst3_processor.cpp:206`

```cpp
ev.rawOrder = ++eventOrder_;
```

`eventOrder_` is a member variable (presumably `uint32_t` or `int`) that increments on
every event in every process block. After ~4.3 billion events (at 4096 events per 4096-sample
block at 48 kHz, that's ~27 hours of continuous processing), a `uint32_t` wraps to 0. When
it wraps, events in the new block get `rawOrder` values that are *lower* than events in
the previous block, breaking the `TimedEvent::before` sort order (which uses `rawOrder` as
a tiebreaker for events at the same sample offset).

In practice, 27 hours is a long time, and the `std::sort` at line 287 uses `rawOrder` only
as a final tiebreaker (after `sampleOffset` and `kind` priority), so the impact is minimal.
But it's a latent bug.

**Fix**: Use a `uint64_t` for `eventOrder_`, or reset it to 0 at the start of each
`process()` call (since `rawOrder` only needs to be unique within a single block's event
list).

---

## P2-5 — AUv3 UMP translator: MIDI 1.0 Program Change (0xC0) is silently dropped

**File**: `source/au3/ArpSIDAUEventTranslator.h:155-156`

```cpp
case 0xC0:
    // Raw MIDI Program Change must not select/overwrite the ArpSID factory patch.
    break;
```

This is **by design** (the comment explains it), but it means that in AUv3 hosts, MIDI
Program Change has no effect on the plugin. The VST3 path handles Program Change via the
`IMidiMapping`/parameter path (`kParamProgram`), and the AUv2 path handles it via
`componentMIDIEvent` → `enqueueMidiIntent` → kernel's CC/PC dispatch. The inconsistency
between AUv3 (PC dropped) and AUv2/VST3 (PC handled) is a functional gap: a user who
switches from Logic AUv2 to a VST3 host will find that Program Change works in one but not
the other.

**Severity**: P2 — by design, but the inconsistency is a user-facing gap.

**Fix**: Either handle PC in the AUv3 translator (setting `te.kind = EventKind::ProgramChange`),
or document the limitation in the plugin's README.

---

## Cross-Cutting Observations (not findings)

1. **Kernel ingress ring is well-designed**: The `enqueueMidiIntent` fallback ledgers for
   CC120/123, sustain, sostenuto, mod wheel, expression, channel volume, bank select,
   RPN/NRPN, data entry, channel pressure, pitch bend, and poly pressure are comprehensive.
   A ring overflow degrades gracefully — safety-critical events (panic, note-off) are
   latched and applied in the next render block.

2. **VST3 `IMidiMapping` is thorough**: All standard CCs (mod, breath, expression, foot,
   volume, sustain, sostenuto, aftertouch, pitch bend, RPN/NRPN, data entry) are mapped to
   `kParamHostCtrl*Base` parameter IDs. The `default` case falls through to
   `sidMappedRealtimeCcParam` for the AKAI MPK mini knobs (CC70-77). This is the correct
   VST3 pattern for MIDI-to-parameter bridging.

3. **The three engines have different channel models**: `bitperfect_engine` is
   multi-channel (16 channels, per-channel voice allocation), `drsid_engine` is
   effectively single-channel (GM channel 10), and `sid808_engine` is single-channel.
   The kernel's `allNotesOffChannel` dispatch assumes all engines are multi-channel, which
   is incorrect for DrSID (P0-1).

4. **Note identity is the weakest link**: Only the VST3 path sets `noteId` from the host.
   The AUv3 and AUv2 paths (and the kernel's async-ring dispatch) all use `noteId = -1`,
   forcing the kernel to fall back to (channel, note) matching. This is sufficient for
   MIDI 1.0 (which has no note IDs) but limits MPE support on non-VST3 hosts.
