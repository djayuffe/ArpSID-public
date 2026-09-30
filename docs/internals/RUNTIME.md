# Runtime and kernel — low-level reference

How a host block becomes audio: the `ArpSIDDSPKernel` block pipeline, the
canonical event model and its ordering law, ingress rings, the host-cycle
dispatcher and fractional render, register scheduling, voice identity and
stealing, state roots and mailboxes, telemetry, and the realtime rules that
hold everything together.

| Source | Contents |
|---|---|
| `source/au3/ArpSIDDSPKernel.hpp` | `ArpSIDDSPKernel` (≈ 10 600 lines): the one engine every format runs |
| `source/au3/ArpSIDCanonicalEvents.h` | `TimedEvent`, `EventKind`, `TransportState`, `EventBuffer` |
| `include/arpsid/core/sid_event_queue.h` | `SidTimedEvent`, `SidTimedEventQueue`, priorities, ordering, overflow ledger |
| `include/arpsid/core/sid_runtime_*.h` | the canonical runtime: model, host block, materializer, execution, fractional render, register scheduler, voice policy, held replay, reset and state-apply policy, parameter services |
| `include/arpsid/core/sid_host_cycle_dispatcher.h` | host sample ↔ SID cycle ↔ subphase dispatch |
| `include/arpsid/core/sid_voice_allocator.h`, `sid_voice_token*.h`, `sid_dynamic_state.h` | voice slots, tokens, stealing |
| `include/arpsid/core/sid_ownership_mailbox.h`, `scope_triple_buffer.h` | lock-free handoff between threads |
| `include/arpsid/core/sid_serializer_schema.h`, `sid_state_codec.h`, `source/au3/ArpSIDStateSerializer.h` | `SidStateRootV1` and its binary codec |
| `source/common/arpsid_telemetry_snapshot.h`, `source/au3/ArpSIDKernelTelemetryFill.h` | `ArpSIDTelemetry` and how it is filled |
| `include/arpsid/core/sid_realtime_guard.h`, `sid_runtime_sizing.h` | realtime guard and buffer sizing |

---

## 1. Threads and the kernel API

| Call | Thread | Notes |
|---|---|---|
| `setup(sampleRate, maxFrames)`, `setSampleRate`, `teardownReset` | non-realtime, never during render | builds engines, prewarms SID tables, sizes scratch |
| `processBlock(outputs, channels, frames, events, count, transport)` | render only, never concurrent | the pipeline in §2 |
| `processBlockMono(output, …)` | render | mono bus variant |
| `setParameter`, `enqueueParameterIntent(pid, value, sampleOffset, hostTime)` | any | staged or queued; never mutates runtime state directly |
| `getParameter(pid)` | any | the shadow value |
| `pushMidi(bytes, len, hostTime)`, `enqueueMidiIntent(…)` | any | MPSC ring, lock-free |
| `schedulePendingStateRestore(root)` | non-realtime | hands a state root to the render thread (§6) |
| `loadPsidData(data, size, subtune)`, `unloadPsid()`, `performC64ControlHubCommand(cmd)` | non-realtime | C64 player handoff |
| `publishGuiRealtimeModels(…)`, `setDigiD418RuntimePolicy`, `setDigiMidiPadMapping`, `triggerDigiPadForGui` | non-realtime | GUI models, DIGI policy |
| `readTelemetry(includeScopes)`, `readOscilloscope`, `readC64Telemetry` | any (editor) | published snapshots only |
| `requestAudioEngineMode(mode)` | any | topology change, applied at the next block edge |
| `setPureSid1Q1OutputMode`, `startPureSid1Q1RecordCapture` | non-realtime | raw engine output mode and capture |

Wrappers (AUv2/AUv3 adapter, Standalone, VST3 kernel host) only translate their
host's events, state and GUI calls into this API; policy lives in the kernel
and the core headers.

---

## 2. One block (`processBlock`)

1. **No-output buses**: if the host passes no output, the full pipeline still
   runs into fixed scratch (`sliceScratchL_/R_`), so the engine keeps advancing.
   A missing right channel is `nullptr`; mono callers use `processBlockMono`.
2. **Realtime scope**: `SidRealtimeScope` marks the thread;
   `requireSidTablesPrewarmedForRealtime()` checks the SID tables exist.
3. **Mailboxes**, in order: `drainPendingStateRestore_()` (a preset or project
   root), `drainPendingPsidHandoff_()` (a newly loaded tune),
   `applyPendingAudioEngineMode_()` (topology).
4. **Chunking**: blocks over `kMaxFramesPerBlock` = 4096 frames are split. The
   MIDI and parameter rings are drained **once for the parent block**, their
   events sliced per chunk like host events (so a queued event keeps its
   position), and the transport beat position advanced per chunk.
5. **Denormals** flushed (FTZ/DAZ on x86, FZ/DN on AArch64) for the rest of
   the block by `ScopedFlushDenormals_`, which restores the caller's
   floating-point mode when `processBlock` returns (the host's audio thread
   also runs its mixer and other plug-ins); transport sanitized with the
   negotiated sample rate.
6. **Dirty parameters** flushed (`flushDirtyParams_`), transient controls
   applied, state projected to the backends, tempo-linked controllers synced.
7. **C64 branch**: if a `.sid` tune is playing, `renderC64SidplayPathIfActive_`
   renders the block with the C64 player (see [C64_MACHINE.md](C64_MACHINE.md))
   and returns.
8. **Host block**: `sidRuntimeBeginCanonicalHostBlock()` captures transport
   state (was/is playing, beat positions). Host note-ons while stopped are live
   input and always play.
9. **Parameter intents** from the ring become canonical events with finalized
   timing.
10. **Sequencer sync**: pattern from parameters, enable, sample rate, 4 steps per
    beat (16ths), swing, host tempo; transport discontinuities handled.
11. **MIDI ring** (UI keyboard, CoreMIDI): drained after refreshing the GUI →
    realtime projection (so DIGI pads see current slots); every event goes
    through `dispatchCanonicalIngressEvent_` — the same path as host events.
    Dropped note-offs are replayed with their original timestamps.
12. **Host events**: Program/BankSlot are rejected as timed audio events
    (presets arrive as state roots). `ParameterRamp` events expand into up to
    64 anchor points across the ramp. Everything else goes through the
    canonical ingress.
13. **Sequencer advance**: over the block's beat window (host-following or
    internal tempo), producing note events and **step boundaries** (up to 256
    per block).
14. **KIT sequencer**: fires drum notes on the step boundaries.
15. **Glide writes**: `scheduleSynthModeGlideWrites()` puts portamento
    `FREQ` writes into the SID write queue at their exact cycles.
16. **Canonical render**:
    `processCanonicalAudioBlockForTargetInto()` interleaves the sorted events
    with render slices, sub-sample cycle spans and sub-phase spans (§4);
    `runtimeEndFractionalBlock()` rebases writes scheduled past the block end.
17. **Pure-SID capture tap**, then **drum bridge** (DrSID/SID-808 bridge output)
    and the **DIGI layer** (after the bridge so it is not overwritten).
18. **Post**: in Pure SID 1Q1 mode everything below is bypassed. Otherwise
    the MIX FX chain, then reverb, an output DC blocker (5 Hz first-order
    high-pass, so saturation and drive cannot leave an offset at the host
    output) and limiter, and the Hi-Fi chain on the exact canonical timeline
    (automation at sample N affects N and later).
19. **Host block advance**, SIDCORE timeline publish.
20. **Stuck-note safety net**: in pure direct polyphony (BitPerfect, POLY, no
    arp, no sequencer), gated voices whose key is not held (and no sustain or
    sostenuto) are released after two consecutive blocks; Synth-mode voices are
    reconciled too.
21. **Telemetry** published.

---

## 3. Events

### 3.1 `TimedEvent` (wrapper-facing)

| Field | Meaning |
|---|---|
| `sampleOffset` | offset in the block, or −1 = unresolved (block start) |
| `cycleOffset`, `subphase` | SID cycle and 1/256 sub-phase inside the sample (`0xFFFF` = unresolved) |
| `kind` | `NoteOn`, `NoteOff`, `PolyPressure`, `ControlChange`, `PitchBend`, `ChannelPressure`, `ProgramChange`, `ParameterSet`, `ParameterRamp`, `TransportChange`, `AllNotesOff`, `AllSoundOff`, `Panic` |
| `channel`, `pitch`, `value`, `data14`, `ccNum` | MIDI data (value normalised; bend −1..1) |
| `noteId`, `voiceToken` | host note ID and canonical voice token |
| `target`, `value_u32`, `value_f32` | parameter ID and value |
| `rawOrder` | arrival order (monotonic) |
| `ingressProvenance` | host events or async ring (prevents double-counting held notes) |

`sanitize(frames)` clamps offsets, channel (0–15), pitch (0–127), values and
14-bit data; `toCanonical()` converts to a `SidTimedEvent`.

### 3.2 `SidTimedEvent` and the ordering law

Types: `MidiNoteOn`, `MidiNoteOff`, `MidiCC`, `PitchBend`, `ChannelPressure`,
`PolyPressure`, `AutomationPoint`, `SidRegisterWrite`, `TransportChange`,
`TempoChange`, `VariantChange`, `ProgramChange`, `AllNotesOff`, `AllSoundOff`,
`Panic`.

`SidTimedEvent::before(a, b)` is a strict total order:

1. Resolved timing before unresolved.
2. Earlier `sample_offset` first.
3. In the same sample: sample-boundary **kills** (`Panic`, `AllSoundOff`,
   `AllNotesOff` without a cycle stamp) first; then cycle-stamped events before
   sample-only events; cycle-stamped events by `cycle_offset`, then by
   `subphase`.
4. Then `arrival_order` (the canonical merge token).
5. Last resort, priority: Panic 0, AllSoundOff 1, AllNotesOff 2, NoteOff 3,
   SidRegisterWrite 4, VariantChange 5, ProgramChange 6, TransportChange 7,
   TempoChange 8, AutomationPoint 9, NoteOn 10, CC 11, PolyPressure 12,
   ChannelPressure 13, PitchBend 14.

### 3.3 Capacity and overflow

| Buffer | Capacity (default) |
|---|---|
| Timed event queue | 4096 (`ARPSID_RUNTIME_TIMED_EVENT_CAPACITY`) |
| Ingress nodes / spill | 2048 / 512 |
| Merge lanes | 5 lanes × 256 |
| MIDI ring, parameter-intent ring | 8192 each (bounded MPSC, lock-free) |
| C64 SID timed writes per block | 4096 |

A reserve of `max(32, capacity/16)` slots is kept for **release-critical**
events (note-offs and kills), so a burst of note-ons can never block a
release. When full, a lower-priority event is replaced by a higher-priority one
and the overflow ledger counts it (published in telemetry). Note-offs dropped
from the MIDI ring are latched and replayed next block with their timestamps.

---

## 4. From events to samples

### 4.1 Host-cycle dispatcher

`SidHostCycleDispatcher` maps a block to SID time with no mutable state:
cycle boundaries are `sampleIndex × cyclesPerSample` (Q32 fixed point, see
`ArpSID_cyclesPerSampleQ32`). It walks the sorted events and calls:

- `onEvent(ev)` — apply canonical state and forward to the engine;
- `onSlice(offset, frames)` — render whole samples up to the next event;
- `onSubSampleSpan(sample, cycleStart, cycleEnd)` — render the cycles before a
  cycle-stamped event inside a sample;
- `onSubPhaseSpan(sample, cycle, subStart, subEnd)` — render sub-phases before
  a sub-phase-stamped event.

So a register write, note or automation point takes effect on its exact cycle
(and sub-phase), not at the sample or block boundary.

### 4.2 Fractional render

`sid_runtime_fractional_render.h` is a planner only: it turns event timing
into `SidRenderInterval` requests and dispatches them to the backend's
`ISidIntervalRenderable` (the SID chip's interval renderers, see
[SID_CHIP.md](SID_CHIP.md#6-sample-planning-and-interval-rendering)). Partial
samples accumulate into `runtimeFractionalAccumL_/R_` with per-sample weights
and are finalized when the sample completes. SID register writes queued by the
synth scheduler (gates, frequencies, control bytes, delayed re-gates) are
consumed by this path.

### 4.3 Register scheduling

`sid_runtime_synth_register_scheduler.h` and the register ops decide which
`$D4xx` writes a musical event produces and when:

- note-on: frequency, pulse width, AD/SR, control with gate, in the SID's
  required order; optional **hard restart** (gate off + ADSR zero, then the
  real gate after the 46-cycle window) for clean retriggers;
- note-off: control without gate;
- glide: the portamento law's `FREQ` steps (§8 of SID_CHIP);
- filter: `FC`, resonance/routing and mode/volume from the parameters;
- writes past the block tail are rebased into the next block.

The **register shadow** keeps the last value of every register; changed-only
writes (`pushSidWriteTimedIfChanged_`) avoid redundant traffic, except `$D418`
digis, which are never filtered.

---

## 5. Voices

### 5.1 Tokens

Every voice has a **canonical token** (`sid_voice_token_pool.h`): a
monotonically increasing `uint64` (0 invalid). Host note IDs are preserved and
mapped to tokens; anonymous or replayed notes get stable synthetic tokens, so
overlapping same-pitch notes each release the right voice. The older
note-identity helpers (`sid_voice_identity.h`) are compatibility-only.

### 5.2 Voice policy and modes

`sid_runtime_voice_policy.h` implements POLY, MONO (retrigger), LEGATO (glide
without retrigger, last-note priority with note stack) and UNISON (stacked
voices spread by Voice Spread), sustain and sostenuto per channel, soft pedal,
and the pitch-bend range (RPN 0).

### 5.3 Allocation and stealing (`SidVoiceAllocator`)

Fixed slot count (3 for one SID, 6 for dual), POD and allocation-free.
Policies: `StealOldest` (default, classic SID), `StealQuietest` (lowest
envelope), `StealReleasingFirst` (prefer released voices, else oldest),
`Refuse` (drop the new note). **Choke groups** (e.g. open/closed hat) always
reuse the group's slot. `tick(samples)` ages voices deterministically.

### 5.4 Held replay

`sid_runtime_held_replay.h`: after a state apply or engine reseed, the notes
still held are replayed into the merge lanes with distinct tokens, bounded by
the token table, so held chords survive a preset change.

---

## 6. State

### 6.1 `SidStateRootV1`

The single persistent truth (magic `ASR1`, schema 1):

- `patch.variant_profile` — chip family, revision, video standard, board and
  output-stage profile;
- `patch.posterior` — the measured analog posterior;
- `patch.parameters.semantic_entries` — the persistent parameter surface as
  `(param_id, value)` pairs;
- `patch.mod_routes`, `patch.macros` (8 values);
- `patch.arp` (enabled, rate, hold, latch, transpose), `patch.seq` (enabled,
  tempo, length, swing), `patch.start_policy`;
- `patch.sid_runtime` — envelope rate counters, exponential counters and
  6581 delay holds per voice (so a restored patch continues exactly);
- forensic temperature, supply, revision and chip seed;
- `document` — program name, program ref, editor layout blob.

### 6.2 Binary codec

`sid_state_codec.h` writes a little-endian binary form with magic `ASSD`
(state), `ASPC` (patch) or `ASPR` (project), version 1.0. Every count and
string is capped before allocation (4096 parameters, 4096 semantic entries,
512 routes; name 512 B, ref 1 KB, layout 64 KB), so a crafted blob cannot force
a large allocation.

**Layout.** After the 20-byte header (magic, major/minor version, param count, feature
flags, Adler-32 checksum of the payload), the payload holds:

1. the root magic and schema;
2. the three document strings;
3. an `EXT1` extension block, version 7:
   - the semantic parameter entries (`u32` id + `f32` value each);
   - the variant profile and measured posterior;
   - the mod routes and macros;
   - ARP/SEQ summaries;
   - the start policy;
   - the envelope runtime counters;
   - the forensic analogue state.

Older extension versions (1–6) and pre-schema flat-parameter blobs still decode.

**State-law revision.** The encoder appends one extra semantic entry,
`kSidStateLawMarkerParamId` (`$7FFF4C41`), with value `revision / 256`, where revision =
`kSidStateLawRevisionCurrent`. The decoder removes it from the entry list, and runs
`sidMigrateLegacySynthModeLaws` when the revision is below 1, i.e. for any pre-0.9.10
state. Older builds keep the entry until canonicalization drops it (its id is outside the
parameter range), so the marker needs no format bump and is forward compatible. The
migration rules are in SYNTH_MODES.md §7.14.

### 6.3 Applying a root

`schedulePendingStateRestore(root)` prepares the root off the audio thread
(`prepareStateRootForApplyNonRealtime_`, including SID-808 bridge slots) and
publishes it through the **ownership mailbox**. The next block drains it
before any event: a hard realtime engine reset, the canonical apply
(`applyStateRootCanonical`), then held replay. The state-apply policy table
(`sid_runtime_state_apply_policy.h`) decides which authority wins for each
reason (transport reset, project restore, factory preset, wrapper snapshot).

### 6.4 Ownership mailbox

`sid_ownership_mailbox.h`: three buffers and one atomic word. At all times one
buffer belongs to the producer, one to the consumer and one is parked. The
producer writes its private buffer and atomically swaps it into the mailbox;
the consumer swaps its buffer for the parked one. No buffer is ever touched by
both threads at once, even when the producer publishes twice while the
consumer is stalled (the ABA problem of the old seqlock).

---

## 7. Telemetry

`ArpSIDTelemetry` (`source/common/arpsid_telemetry_snapshot.h`, about 360
fields, append-only ABI) is published every block:

- output peak/RMS per channel and decaying peaks, active voices, render mode,
  transport (tempo, playing, beat), arp and sequencer state and step;
- the `$D400–$D41C` register image, voice tokens (note, gate, envelope), LFO
  values and phases, macro and modulation source values;
- drum activity per class and the last hit, DIGI voices/triggers/`$D418`
  writes/last nibble, forensic model values, Hi-Fi meters (dry/wet/delta,
  correlation, safety gain);
- scopes: main output (512), filter in/out, the three VCOs, DIGI, the C64 bus
  (128 samples × 5 lanes);
- a full C64 snapshot (see [C64_MACHINE.md](C64_MACHINE.md#10-telemetry-and-tracing));
- diagnostics: event overflows, limiter clamp hits, parameter-intent
  fallbacks, C64 catch-up and budget counters.

Scopes and the C64 snapshot are only copied while an editor view has asked for
them (`notePresentationScopeRequest`, `noteC64TelemetryRequest`, held for
125 ms / 250 ms); the heavy C64 part refreshes at most 60 times a second.
Scopes use a wait-free SPSC **triple buffer** (`scope_triple_buffer.h`) so the
GUI can never collide with the render thread. `ArpSIDKernelTelemetryFill.h` is
the one projection from kernel to telemetry, shared by the AU adapter and the
VST3 kernel host.

---

## 8. Realtime rules

- **No allocation, no locks, no I/O, no unbounded loops** on the render thread.
  All scratch (event buffers, fractional accumulators, slice scratch, C64
  timed writes) is sized in `setup()` with headroom.
- `sid_realtime_guard.h`: a thread-local guard marks render scopes and records
  violations (e.g. a late table build). Each plug-in instance owns all its
  render state; nothing mutable is shared between instances.
- Heavy producer work — file parsing, `.sid` loading, state building, sample
  conversion, bank I/O — runs off the render thread and is published through
  mailboxes.
- Every float leaving a stage is sanitized; denormals are flushed while the
  kernel renders (and only then: the caller's FP mode is restored); a NaN
  guard protects the output.
- The **reset law** (`sid_runtime_reset_policy.h`): panic amputates every
  transient queue, ingress lane, spill and fallback state, and every active
  token, so nothing stale survives a reset.
- The **materializer** (`sid_runtime_event_materializer.h`) is the only CC
  table and host-input policy; AU and VST3 both call it, so the formats cannot
  drift.

See [REALTIME_OWNERSHIP.md](../REALTIME_OWNERSHIP.md) for the full
render-vs-producer ownership table, and [ENGINES.md](ENGINES.md) for what the
engines do with these events.
