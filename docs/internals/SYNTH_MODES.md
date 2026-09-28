# Render modes, synth modes and projection engines — low-level reference

ArpSID has one kernel and several ways of turning notes into SID sound. This reference
covers:

- how the kernel decides which way is active (the **render mode**);
- how the product flavor constrains that choice;
- how each mode turns MIDI into sound, with most detail on the register-level
  **SYNTH / SID REG** mode;
- the **projection engines**: the one-way layers that project the parameter image into
  engines (backend projection), engine writes onto an emulated C64 bus (C64 SID projection
  mirror), factory kits into the drum engines (drum bridge), and GUI tab models into render
  data (GUI realtime projection).

Read [RUNTIME.md](RUNTIME.md) (pipeline and events) and [ENGINES.md](ENGINES.md) (the
engines) first.

| Source | Contents |
|---|---|
| `include/arpsid/core/sid_runtime_model.h` | `SidRuntimeRenderMode`, `sidResolveRenderModeFromLiveParams`, ARP/SEQ authority laws |
| `include/arpsid/core/sid_runtime_state_root_presentation.h` | `sidSanitizeRenderModeParamsRT` (mode-flag exclusivity) |
| `source/au3/ArpSIDComponentFlavor.h` | `ComponentFlavor`, AU subtypes, flavor predicates |
| `source/au3/ArpSIDDSPKernel.hpp` | `enforceComponentFlavorPolicy_`, `flushDirtyParams_`, synth-mode kernel hooks, drum routing, KIT scheduler, C64 mirror lifecycle, pure 1Q1 output |
| `include/arpsid/core/sid_runtime_host_policy.h` | GM drum auto-promotion law |
| `include/arpsid/core/sid_runtime_shared_kernel.h` | per-event routing (`runtimeKernelOnMidiNoteOn/Off`, transport, program change) |
| `include/arpsid/core/sid_runtime_audio_kernel.h`, `sid_runtime_target_adapter.h`, `sid_runtime_backend_impl.h` | per-mode render dispatch |
| `include/arpsid/core/sid_runtime_voice_policy.h` | `VoiceAllocator`: play modes, held-note ledger, stealing (SYNTH mode) |
| `include/arpsid/core/sid_runtime_synth_state.h`, `sid_runtime_synth_register_scheduler.h`, `sid_runtime_synth_performance.h` | SYNTH mode voices, note → register-write programs, bend/pressure/pedals |
| `include/arpsid/core/sid_runtime_sidreg_queue_render.h` | render of a timed register-write queue |
| `include/arpsid/core/sid_runtime_register_shadow*.h`, `sid_runtime_register_ops.h` | the queued-register shadow |
| `include/arpsid/core/sid_runtime_backend_projection.h` | parameter image → engines |
| `include/arpsid/core/sid_runtime_mod_ops.h` | the 9-slot parameter mod matrix and its per-mode targets |
| `include/arpsid/core/c64_sid_projection_bridge.h` | engine register writes → C64 bus mirror |
| `include/arpsid/engines/drum_engine_host_bridge.h`, `drum_engine_router.h` | factory kit / drum context projection |
| `include/arpsid/gui/gui_realtime_projection_v588.h` | MIX/KIT/DIGI models → render projection |

---

## 1. Vocabulary

| Term | Meaning |
|---|---|
| **Render mode** | Which engine owns note → sound: `BitPerfect` (`CLASSIC`), `SidRegister` (`SYNTH / SID REG`), `DrSid` (`DR SID`). It is derived from parameters, never stored separately. |
| **Flavor** | The product identity (`ComponentFlavor`): Hybrid, Instrument, DrumMachine, Sid808, C64SidPlayer. It forces some parameters. |
| **Voice mode** | Poly / Mono / Legato / Unison (`kParamVoiceMode`, `round(n × 3)`). Its meaning depends on the render mode. |
| **Topology** | Poly-Illusion (8 SIDs) or single SID (3 voices), for CLASSIC only. |
| **Authority** | The one component allowed to decide a given thing. For example, in SYNTH mode the `SidRegisterEngine` plus its write queue is the only register authority. |
| **Projection** | A one-way, idempotent translation from a source of truth to a consumer. A projection never writes back to its source. |
| **Secondary authority** | A note generator layered on top of a mode: the arpeggiator (ARP) or the step sequencer (SEQ). |

---

## 2. Render-mode resolution

**Single-source rule.** `sidResolveRenderModeFromLiveParams(params)` is the only function
that maps parameters to a mode. Every render-path branch switches on its result.

```cpp
if (params[kParamDrSidEnable]     > 0.5) return DrSid;
if (params[kParamSynthModeEnable] > 0.5) return SidRegister;
return BitPerfect;
```

| `SidRuntimeRenderMode` | Value | Engine | Editor name |
|---|---|---|---|
| `BitPerfect` | 0 | `BitPerfectEngine` (+ arpeggiator) | `CLASSIC` |
| `SidRegister` | 1 | `SidRegisterEngine` + `SidWriteQueue` | `SYNTH / SID REG` |
| `DrSid` | 2 | `DrSidEngine`, or the SID-808 bridge in the SID-808 flavor | `DR SID` |
| `C64Psid` | 3 | telemetry sentinel only | — |

- **Exclusivity.** DrSID and Synth can never both be on. After every parameter flush,
  `sidSanitizeRenderModeParamsRT` clears `SynthModeEnable` if both flags are set (DrSID
  wins). The corrected value is staged back through the canonical helper, so the atomic
  parameters, the render copy and the state root all agree.
- **`C64Psid`** is never returned by the resolver. The C64 SID player is a *flavor*, and it
  forces both enable flags off. Only the telemetry fill sets mode 3, to tell scopes and
  register readback to read from the C64 machine.
- **Secondary authorities:**
  - ARP is effective only in `BitPerfect`
    (`sidEffectiveArpAuthorityFromLiveParams`).
  - SEQ is effective in `BitPerfect` and `DrSid`, and blocked in `SidRegister`
    (`sidEffectiveSeqAuthorityFromLiveParams`). SEQ is the transport for DrSID and SID-808
    drum patterns.

---

## 3. Flavors and policy enforcement

Five AU components share one binary. The VST3 and the Standalone app run as `Hybrid`.

| Flavor | AU subtype | Display name | Forced parameters (every flush) |
|---|---|---|---|
| `Hybrid` | `ArpS` | Classic | none |
| `Instrument` | `ArIn` | Instrument | DrSID off, **Synth on**, ARP off, SEQ off |
| `DrumMachine` | `DrSD` | Drum Machine | Synth off, **DrSID on**, ARP off (SEQ kept for drum patterns) |
| `Sid808` | `S808` | SID-808 | Synth off, **DrSID on**, ARP off, DrSID machine model = AnalogX0X8; bank slot forced into 120–149 (default 120) if outside |
| `C64SidPlayer` | `C64P` | C64 SID Player | Synth off, DrSID off, ARP off, SEQ off |

- `enforceComponentFlavorPolicy_()` runs after every parameter flush, on setup, and on
  flavor change. It writes through `runtimeStageNormalizedParameterOnly`, so the atomics,
  the render copy and the state root cannot disagree.
- Flavor predicates:
  - `componentFlavorAllowsDrSid`: every flavor except Instrument and C64SidPlayer.
  - `componentFlavorIsDedicatedDrum`: DrumMachine and Sid808.
  - `componentFlavorForcesAnalogSid808`: Sid808.

### 3.1 GM drum auto-promotion

A channel-10 note 35–81 can switch the runtime into DrSID mode on its own, but only when the
policy allows it (`sidCanonicalGMDrumAutoPromotionAllowed`):

| Flavor | Promotion |
|---|---|
| DrumMachine, Sid808 | always allowed (they are drum machines) |
| Hybrid | only when **Auto GM Drum Promotion** (`kParamAutoGmDrumPromotion`, default off) is on |
| Instrument, C64SidPlayer | never |

When promotion happens:

1. `SynthModeEnable` is staged to 0 and `DrSidEnable` to 1.
2. Both are applied through the normal parameter path.
3. The note itself then plays in DrSID mode.

A user-selected CLASSIC or SYNTH mode is therefore never hijacked by channel-10 traffic
unless the user asks for it (the v909 rule).

---

## 4. Event routing per mode

`runtimeKernelOnMidiNoteOn` routes each canonical note-on in this order:

1. Arp-generated internal events go straight to BitPerfect.
2. The note is recorded for telemetry and velocity.
3. **DrSID mode**: `triggerDrumMidi(note, velocity)`. This goes to the SID-808 bridge in the
   Sid808 flavor, and to canonical DrSID otherwise (§8).
4. **SYNTH mode**: `synthNoteOn(ev)`, which is the register scheduler (§7).
5. **ARP enabled** (CLASSIC only): the note joins the arpeggiator's held buffer.
6. Otherwise BitPerfect `noteOn` with the voice token resolved for (channel, note, noteId).

Note-off mirrors this. In ARP mode, a note-off that carries a real host noteId is *also*
sent to BitPerfect. That releases a direct note that was already sounding when ARP was
switched on.

Other events:

| Event | Behaviour |
|---|---|
| Transport stop | all notes off, **except in DrSID mode**. Drum kits and their state survive Stop→Play and host reset storms. |
| Transport play (stopped → playing edge) | rewind of the ARP phase (not in DrSID mode) |
| MIDI Program Change | ignored by the runtime. Patches are selected only by `kParamBankSlot` and the AU/VST3 preset APIs (the VST3 `Program` parameter). |
| Tempo | clamped to 1–400 BPM; drives ARP, SEQ and LFO sync |
| All Notes Off / All Sound Off | channel-scoped |
| Live note-ons during host-sequenced playback | may be suppressed by the host block policy. GM channel-10 drums, SID-808 pad notes and DIGI pads are always let through. |

---

## 5. Mode transitions

`flushDirtyParams_` compares the mode before and after each flush:

- **Leaving BitPerfect** (to SYNTH or DrSID): ARP is forced off, its notes are released and
  the arp flag is cleared.
- **Entering SYNTH**: SEQ is forced off. Its step, last note and phase are reset.
- **Structural return to CLASSIC** from SYNTH or DrSID: ARP and SEQ are both cleared on the
  transition block. The order of host events can therefore not revive an old secondary
  authority.

The SYNTH enable toggle itself (`runtimePolicyHandleSynthModeEnable`) materializes
immediately, even during state restore:

| Edge | Actions |
|---|---|
| off → on | stage ARP and SEQ off; all-notes-off performance reset; clear the SID write queue; reset the 3 synth voices; `reseedSynthModeRealtimeState_(…, activeOnly = false)` writes the system byte, filter and volume registers, and each voice's frequency, PW, ADSR and control byte with the gate low |
| on → off | every active synth voice is hard-gated off (control byte without gate) and its tracking cleared |

**Backend projection on entry.** When the render mode becomes `SidRegister` (or on first
apply or restore), `projectRuntimeStateToBackends` clears the write queue, resets the
interval cursor and seeds the live `SidRegisterEngine` **once** from the 29 register
parameters `kParamSidRegD400…`. After that, register changes arrive only as timed writes.
The register parameters are never written back into BitPerfect or DrSID. A stale register
page therefore cannot reappear on a later mode switch.

**Topology** (CLASSIC). The SETTINGS topology choice is published as a request.
`applyPendingAudioEngineMode_()` applies it at a block boundary with
`BitPerfectEngine::setSidChipTopologyMode`, which sends all notes off. Both topologies are
prepared in advance, so the switch allocates nothing.

---

## 6. CLASSIC (`BitPerfect`) mode

The engine is described in ENGINES.md §1. This section covers how the mode is driven.

**Notes.** Notes come from the host directly, from the arpeggiator, or from the sequencer.

**ARP render.** `canonicalRenderBitPerfectWithArp` renders the block in sub-blocks cut at
each `TimedArpEvent::sampleOffset`, so arp gates land on their exact sample.
`collectTimedEvents` runs every block, even when ARP is disabled, so a pending gate-off is
always flushed.

**ARP rate.** A rate parameter below 0.005 means *tempo sync*, at a quarter note:
`(bpm/60)/1` Hz. Otherwise the rate is the free exponential law.

**ARP glide.** *Portamento Arp Glide* only takes effect when the voice mode is not Poly.
In Poly, suppressed inter-step gate-offs would stack voices without limit.

**Arp enable edge.** On ARP off → on, BitPerfect sends all notes off first. Voices started
directly while ARP was off cannot then be left stuck.

**LFO tempo sync.** LFO *l* (0–3) uses base divisions {4, 2, 1, 0.5} beats. The rate knob
scales the division by `2^(4n − 2)`, i.e. ¼× to 4×. The rate is then `(bpm/60)/division`.

**Modulation.** The 9-slot parameter matrix is applied last, on top of the projected base
values:

| Slot (source + depth) | CLASSIC target | Depth scale |
|---|---|---|
| VCF cutoff | Filter Cutoff | 0.35 |
| VCF resonance | Filter Resonance | 0.30 |
| VCO1/2/3 freq | per-VCO pitch mod (Δ × 2 semitones, clamped ±2) | 0.20 |
| VCO1/2/3 PW | per-VCO Pulse Width | 0.35 |
| Master volume | Master Volume | 0.30 |

`Δ = clamp(source_bipolar × depth × scale, −1, 1)`. The depth is 0..1, and the source
comes from the canonical mod-source values (LFOs, velocity, wheel, and so on).

**Unison.** In CLASSIC, Unison stacks the engine's `unisonCount` voices. The projection
code that should derive the count from Voice Spread compares the *normalized* voice-mode
value against 2.5 and never runs. In practice the count stays at the engine default of
**4**, and Voice Spread sets only the ±24-cent detune. (SYNTH mode derives its count from
spread; see §7.2.)

---

## 7. SYNTH / SID REG (`SidRegister`) mode

In this mode the **SID registers are the instrument**. Every note, glide, bend and pedal
becomes a timed write to `$D400–$D418` on one 3-voice `SidRegisterEngine`, stamped with a
host sample offset and a SID cycle offset. What you see on the SID REG page is what the chip
plays. The Instrument AU flavor is locked to this mode.

### 7.1 Voice model

- There are exactly **3** voices (`kSidSynthVoiceCount`), one per SID voice. Voice *v* owns
  registers `7v … 7v+6`.
- Each `SynthModeVoiceState` holds:
  - identity: `midiNote, channel, noteId, voiceToken`;
  - state flags: `active, keyDown, sustained, sostenutoLatched, age`;
  - pressure state (with a last-projected-SR cache for de-duplication);
  - the ADSR nibbles last written;
  - `currentSidFreqReg`, `targetSidFreqReg` and a `DiscreteRegisterGlideState`.
- Voice events with an index ≥ 3 are never written to registers.

### 7.2 Voice policy (`VoiceAllocator`)

The allocator decides *which* SID voice plays and *what kind* of event it gets. It emits
`VoiceEvent`s of kind Start, Retrigger, Glide, GateOff or AllOff.

| Setting | Value in SYNTH mode |
|---|---|
| Play mode | from Voice Mode: Poly, Mono, Legato, Unison |
| Poly voices | 3 |
| Unison count | `1 + int(spread × 7)`, then **capped to 3** (`sidRegProjectedUnisonCount`) |
| Note priority | Last (newest held note) |
| Retrigger mode | Always |
| Held-note ledger | 32 entries (`kMaxHeldNotes`), each with a press order and a voice token |

**Poly note-on:**

1. If a voice with the *same identity* (same host noteId) is sounding, it gets a
   **Retrigger**. Anonymous repeats never match, so they get separate voices.
2. Otherwise a free voice gets a **Start**.
3. Otherwise a voice is **stolen**.
   - Victim class, from best to worst: a fully released tail, then a held voice with no
     pedal, then a pedal-latched voice.
   - Within a class the score decides: *oldest* by default, or *quietest* / *lowest
     energy*.
   - The stolen token is removed from the ledger. The engine emits GateOff(victim) and then
     Start(new note).

**Mono:**

- Voice 0 only.
- A new note while one is sounding gets a Retrigger. With RetrigMode Never it would be a
  Glide instead.
- On release, if the released note was the sounding one, the next priority held note takes
  over with a Glide (or a Retrigger if it has the same identity). If no note is left, the
  voice gets a GateOff, unless a pedal holds it.

**Legato:**

- As Mono, but a note-on while another key is held is a **Glide**, not a retrigger.

**Unison:**

- All *n* voices are assigned the same note and token.
- Each gets Start, Retrigger or Glide depending on its previous state.
- Voices beyond *n* are gated off.

**Ledger.**

- When the ledger overflows, the oldest entry is evicted and its voice is **force-gated
  off**, ignoring pedals, so no orphan voice is left gated.
- An anonymous note-off (no noteId) releases the **newest** matching anonymous entry
  (LIFO). This rule is pinned by a test.
- A real host noteId is released only by that exact ID.

### 7.3 Note-on register program

`scheduleSynthModeStartVoice` writes the following for voice *v* (base `b = 7v`), all at the
event's `(sampleOffset, cycleOffset)` unless noted:

| # | Register | Value | Condition |
|---|---|---|---|
| 1 | `b+0`, `b+1` FREQ | `canonicalSidFrequencyRegisterForMidiNote(note, clock)` | always. Start/Retrigger always jump; they never glide. |
| 2 | `b+2`, `b+3` PW | `round(n × 4095)`: low byte, then high nibble | always, so a pulse never depends on stale state |
| 3 | `b+4` CTRL | control byte, gate **low** | if the start policy has hard restart with gate-off-before-start |
| 4 | `b+5` AD | `(A << 4) \| D` from the ADSR parameters, each `round(n × 15)` | always |
| 5 | `b+6` SR | `(S << 4) \| R` | always |
| 6 | `b+4` CTRL | gate low (waveform preload) | if `preloadWaveform` (default on) |
| 7 | `b+4` CTRL | TEST high, then TEST low **1 cycle later** | if `useTestBitPrecharge` |
| 8 | `b+4` CTRL | control byte, gate **high** | at `sampleOffset + postStartDelaySamples`, delayed by **46 SID cycles** (`kSidHardRestartCycles`) when hard restart is on |

The 46-cycle delay reproduces the C64 hard restart: the gate stays low long enough for the
envelope to reach a known state before the new attack. The start policy comes from the
patch (`PatchStartPolicy` in the state root):

| Field | Default | Meaning |
|---|---|---|
| `gateOffBeforeStart` | true | write gate low before the new note |
| `useHardRestart` | true | delay the gate-on by 46 cycles |
| `strictHardRestart` | false | force the 46-cycle delay even without `useHardRestart` |
| `preloadWaveform` | true | write the waveform with gate low first |
| `useTestBitPrecharge` | false | pulse TEST for one cycle to reset the oscillator phase |
| `postStartDelaySamples` | 0 | extra host-sample delay before gate-on |

Policy IDs: Default, HardRestart, StrictHardRestart, SoftLegato, DrumGate.

**Glide note-on** (`scheduleSynthModeGlideVoice`, Legato/Mono/Unison Glide events):

- If portamento time > 0.001 and the voice already has a frequency, a
  `makeDiscreteRegisterGlide(current → target, n² × 5 s, style, video-frame rate)` is armed.
- Otherwise the new FREQ is written at once.
- No gate and no ADSR writes: the envelope keeps running.
- The portamento style comes from `kParamPortamentoStyle`: `round(n × 3)` → C64 SLIDE,
  C64 FIXED, LINEAR, SMOOTH.

**Glide stepping.** `scheduleSynthModeGlideWrites` evaluates every host sample of the block
for each gliding voice. It emits a FREQ write at the exact sample and cycle where
`advanceDiscreteRegisterGlide` reports a step. A glide is therefore a stream of real
register writes, as a C64 player routine would produce.

### 7.4 Control byte law

`synthModeControlByteForVoice(params, v, gate)`:

- **Waveform:** `idx = int(n × 8)` → `{$10 TRI, $20 SAW, $40 PUL, $80 NOI, $30 TRI+SAW,
  $50 TRI+PUL, $60 SAW+PUL, $70 TRI+SAW+PUL}`. If no waveform bit results, TRI is used.
- **Sync** (`$02`): set **only for voice 2** (index 1), from *VCO2 Sync*.
- **Ring** (`$04`): per voice, from *VCO1/2/3 Ring*.
- **Gate** (`$01`): set only when `gate` is true.
- **TEST** is never inherited. The control law is parameter-authoritative, and TEST is set
  only by the explicit precharge write. A stale TEST bit from a queued or live shadow would
  mute the oscillator after a mode change.

### 7.5 Note-off

`scheduleSynthModeNoteOff`:

1. The allocator's `noteOff` updates the ledger and may emit events: a Glide or Retrigger
   to the next held note (Mono, Legato, Unison), or a GateOff.
2. **Token-first release.** Every active voice carrying the released token is marked key-up,
   then:
   - if the policy produced a replacement event for that voice, that event wins and the
     token path writes nothing;
   - if the voice is held by sustain or sostenuto, it stays gated;
   - otherwise it is gated off: `CTRL` without gate, at the event's sample and cycle.
3. **No token, no events.** A tiered compatibility search runs:
   - tier 0: exact channel + noteId;
   - tier 1: compatible channel, anonymous only;
   - tier 2: anonymous only;
   - finally: a positive-noteId mismatch fallback on the same channel.

   Candidates prefer key-down, then the newest token, then the youngest. An anonymous
   note-off can never release a voice that carries a real host noteId.
4. Remaining policy events run, skipping voices already handled by the token path.

`hardSynthModeVoiceOff_(v)` writes the gate-low control byte and optionally clears the
voice's tracking. It is also the sustain-release callback of the allocator.

### 7.6 Performance controls

| Input | Register effect |
|---|---|
| Pitch bend (per channel) | For every active voice on the channel: FREQ = f(`midiNote + bend`). Only `currentSidFreqReg` changes, so a running glide keeps its destination and bend layers on top of portamento. Range: per-channel RPN 0, 0–48 semitones in the runtime model. |
| Channel pressure | For every active voice on the channel: `$D40x+6` SR = `(round(S × (0.25 + 0.75·p)) << 4) \| R`. Pressure scales the sustain level. Duplicate values are not rewritten. |
| Poly pressure | The same law, but only for voices whose token matches the note's token. Unison voices sharing a token all get it. |
| Sustain (CC64) | on: active voices on the channel are marked sustained. off: sustained voices whose key is up are hard-gated off. |
| Sostenuto (CC66) | on: *currently key-down* voices are latched. off: latched key-up voices are gated off. |
| CC1, CC2, CC7, CC11, CC64, CC66 | also trigger a reseed (§7.8) and a bend re-application for every channel with active voices |

### 7.7 Filter and global registers

`pushSynthModeFilterRegsRealtime_` runs before every note-on and on every reseed. It writes
only registers whose value changed (§10.2):

| Register | Value |
|---|---|
| `$D415` FC LO | `fc & 7`, where `fc = round(cutoff × 2047)` |
| `$D416` FC HI | `fc >> 3` |
| `$D417` RES/FILT | `(round(res × 15) << 4) \| route`. The route is the low 3 bits of the runtime's synth filter-route state (V1, V2, V3). |
| `$D418` MODE/VOL | `modeBits \| round(masterVolume × 15)`, with `modeBits` from `round(filterModeNorm × 2)` → `$10` LP, `$20` BP, `$40` HP |

**Known discrepancy.** The `$D418` mode law has only three outcomes. The 8-way Filter Mode
choice (OFF, LP, BP, LP+BP, HP, NOTCH, BP+HP, ALL) is therefore collapsed: for example HP
selects BP, and OFF still sets the LP bit. CLASSIC decodes all eight modes. In SYNTH mode,
the filter type you hear may differ from the one the Filter Mode control shows.

The system byte (pseudo-register `$D41D`, with model and ADSR-bug bits) is derived from the
chip-revision selector. A 6581 selection implies the ADSR bug. The byte is kept in the
shadow at `$19`.

### 7.8 Live parameter changes: reseed

**Whenever** a parameter is applied while SYNTH mode is on
(`runtimeApplyProjectedParameterBody`):

1. Voice Mode or Voice Spread updates the allocator's play mode and its unison count
   (≤ 3).
2. `reseedSynthModeRealtimeState_(0, 0, activeOnly = true)` runs:
   1. It syncs the system byte and the queued shadow from the live engine, and sanitizes
      the shadow.
   2. It writes the filter and volume registers.
   3. For each **active** voice it writes FREQ, PW, AD, SR and CTRL. The gate is on if the
      voice is key-down, sustained or latched.
3. Pitch bend is re-applied to every channel with active voices.

Because every write goes through the change-only shadow, a reseed after an unrelated
parameter change produces no register traffic. Turning a knob that maps to a register
produces exactly one write.

Pulse width, ADSR and waveform are therefore live on sounding voices, as on a real SID
edited by a player routine. The 9-slot parameter matrix (§6) is applied by the backend
projection to BitPerfect and DrSID only. In SYNTH mode the registers follow the base
parameter values.

### 7.9 Direct register editing (SID REG page)

- The 30 parameters `kParamSidRegD400 … kParamSidRegD41D` hold register bytes as
  `round(n × 255)`.
- `runtimeImportSidRegisterNormalized` imports registers `$00–$19` into the runtime model's
  register snapshot.
- In SYNTH mode, `$00–$18` are also written **directly** into the live `SidRegisterEngine`
  and into the shadow. `$19–$1C` are read-only on hardware and are never written.
- Automating a register parameter is literally automating the chip.

### 7.10 Cycle timing

- **Offsets.** Every SYNTH write carries `(sampleOffset, cycleOffset)`. The unresolved cycle
  sentinel `$FFFF` means "at the sample boundary" and is normalized to 0 everywhere
  (`normalizeSynthModeCycleOffset`). Before v900, delayed writes computed from sample-only
  events drifted to the end of the sample.
- **Delayed writes.** `pushSynthModeWriteDelayedByCycles` converts:
  1. `(sample, cycle)` to an absolute SID cycle, using Q32 fixed-point cycles per sample
     (`ArpSID_cyclesPerSampleQ32`);
  2. adds the delay;
  3. maps back with an O(log n) integer search for the host sample whose cycle interval
     contains the target (`ArpSID_sampleForAbsoluteCycleQ32`).

  There is no floating-point rounding drift for awkward clock/sample-rate ratios.
- **Clamp.** A cycle offset is clamped to the cycles that actually fall inside its host
  sample.

### 7.11 Rendering

`renderSynthRegisterAudio` → `renderSidRegisterQueueToStereo(engine, queue, L, R, n)`:

1. The queue is stable-sorted: by sample, then cycle, then insertion order.
2. For each output sample:
   - writes stamped *before* the sample are applied;
   - `renderTimedSampleAccurate(first, last)` renders the sample while applying that
     sample's writes at their cycle positions inside it.
3. Every applied write is also passed to the *applied-write observer*, which feeds the C64
   mirror (§10.3).
4. **The register engine is mono**: the same sample goes to L and R.
5. Writes consumed in this slice are erased. Future-stamped writes stay queued.

The live AU/VST path uses the fractional sub-span renderer (RUNTIME.md), which delivers the
same writes at their exact subphases through `SidRegisterEngine::renderIntervalAccurate`
(ENGINES.md §3.2).

### 7.12 Orphan reconciler

`reconcileSynthModeUnheldVoices_` runs only in SYNTH mode. It finds voices that the
allocator believes are key-down, but whose (channel, note) is not held in the raw-MIDI
held-ingress ledger. The ledger is checked by generation and depth. Pedal-held channels are
exempt. Such voices are gated off with a two-pass grace period.

Host-timed events mirror into the same ledger at dispatch time with synthetic MIDI bytes.
Before v898 they did not, and every host-started SYNTH voice was treated as an orphan.

### 7.13 CLASSIC versus SYNTH at a glance

| | CLASSIC (`BitPerfect`) | SYNTH / SID REG |
|---|---|---|
| Chip(s) | 8 SIDs (Poly-Illusion) or 1 SID | 1 SID |
| Max voices | 8 (× 3 oscillators) | 3 |
| Oscillators per note | 3 (VCO 1–3 layered) | 1 (one SID voice per note) |
| How parameters reach the chip | engine setters on every chip | timed register writes |
| Unison | fixed 4 voices, ±24 ct spread | 1–3 voices (from spread), same pitch |
| Filter modes | all 8 | LP / BP / HP (see §7.7) |
| Hard sync source | all three VCOs selectable | VCO2 only |
| Velocity | `sqrt(v)` gain | no velocity gain; the SID envelope sets the level |
| Mod matrix | 9 slots applied | not applied to registers |
| ARP / SEQ | both | neither (forced off) |
| Pressure | mod-matrix source | scales the sustain nibble |
| Output | stereo, Poly gain × 1.12 | mono duplicated, through the register-engine output stage and limiter |

---

## 8. DR SID mode

In DrSID mode, notes trigger drums (`triggerDrumMidi`), and note-offs release gated drums.

- **DrumMachine and Hybrid flavors.** Canonical `DrSidEngine` is the only audio authority
  (ENGINES.md §8.2).
- **Sid808 flavor.** Notes go to the SID-808 bridge instead (§10.5):
  - The GM spec of the note is resolved.
  - If the bridge is not already in the SID-808 context, its identity is repaired from the
    **currently loaded** bank slot (120–149, default 120).
  - The hit is sent with a `Sid808HitOverride` that carries the selected factory slot, so
    direct MIDI, the host events array, the MIDI ring and the KIT scheduler all resolve the
    same kit data.
  - The bridge output *replaces* the drum bus.
  - Canonical DrSID is not triggered as well, which avoids a double render. It remains the
    fail-open path if the bridge identity cannot be set up.
- **KIT scheduler.**
  - The KIT grid (9 classes × 32 steps) is compiled into `CompiledKitSequencer` when its
    hash changes.
  - The sequencer's `StepBoundary` list drives it at exact sample offsets.
  - For each step event: an accent adds +27 velocity, capped at 127. The KIT class is
    mapped explicitly to the GM class (`kitDrumClassToSidGM`; the two enums do not share an
    order). The event then routes by engine target:

    | Target | Route |
    |---|---|
    | **SID808** (or any hit in the Sid808 flavor) | `noteOnAtWithOverride(offset, class, vel, note, override)`; the override carries the per-class voice (waveform, AD, SR, PW, flags, mask) and the selected slot |
    | **DrSID** | `triggerKitMidiNote(note, vel, selectedSlot, …voice override…)` |
    | **DIGI** | a one-shot DIGI projection for the assigned slot, played through the `$D418` stream |
- **Mod matrix.** The same 9 matrix slots retarget to drum parameters:

  | Slot | DrSID target | Depth scale |
  |---|---|---|
  | VCF cutoff | Kick Tune | 0.28 |
  | VCF resonance | Kick Decay | 0.30 |
  | VCO1 freq | Snare Tone | 0.26 |
  | VCO1 PW | Snare Snap | 0.30 |
  | VCO2 freq | Hat Tune | 0.24 |
  | VCO2 PW | Hat Decay | 0.28 |
  | VCO3 freq | Cowbell Tune | 0.24 |
  | VCO3 PW | Tom Tune | 0.24 |
  | Master volume | DrSID Volume | 0.28 |

- **Filter Drive** is capped at 4.5 dB of output-gain bias in DrSID mode, against 12 dB in
  the other modes. This keeps drive from rebuilding the drum-bus distortion stack.
- **Transport stop** does not release drums (§4).

---

## 9. C64 SID player flavor

The C64 SID player is not a render mode. The flavor forces SYNTH, DrSID, ARP and SEQ off,
so the resolver reports `BitPerfect`. `processBlock` then takes the C64 branch whenever a
tune is loaded:

- The PSID/RSID runtime (C64_MACHINE.md) plays the tune.
- Telemetry reports mode `C64Psid`.
- With no tune loaded, the flavor never falls through to the live synth.

---

## 10. Projection engines

### 10.1 Backend projection: parameters → engines

`projectRuntimeStateToBackends(model, bank, params, voicePolicy, tempo, force, …)` is the
one place where the canonical parameter image becomes engine configuration. It is
idempotent. Expensive setters (ADSR, cutoff/resonance) are skipped unless their inputs
changed or `force`/`firstApply` is set. It runs in this order:

1. **Render mode and transition** detection.
2. **Chip.**
   - The chip-revision selector gives the family (6581/8580) and the revision. The variant
     profile is updated, and on a family change so are the board revision and output
     stage.
   - The model and revision go to BitPerfect, DrSID and the register engine.
   - The ADSR bug is on for a 6581 or when forced.
   - The clock comes from the variant.
   - The system byte is written to the register engine.
   - External RC and oversampling go to BitPerfect and DrSID.
3. **Voice.**
   - Master tune and volume, Voice 3 off (VCO3 level ≈ 0), portamento time, style and C64
     glide delta.
   - Voice mode: a change clears all canonical voice state.
   - Allocator play mode and unison count (≤ 3) for SYNTH mode; voice spread.
4. **ADSR** (with a 0.001 floor on A/D/R), cutoff, resonance, filter mode and routing (from
   the synth route nibble).
5. **VCO 1–3**: waveform, PW, detune, level, LF mode, PWM depth, sync, ring.
6. **LFO 1–4**: rate `0.1 × 200ⁿ`, depth, shape `round(6n)`, sync. The values are published
   into the model for the mod sources. Then the random scope is resolved, and Env1 is set
   from BitPerfect's strongest envelope.
7. **Arpeggiator**: enable (per §2, with the off → on BitPerfect all-notes-off), mode, rate
   or tempo sync, octaves, swing, gate, hold, latch, transpose, pattern length, random,
   glide gating.
8. **Modulation**: the typed mod routes, then the 9-slot parameter matrix to BitPerfect.
9. **DrSID**: every drum parameter, machine model, accent, drive, hat metal, clap spread,
   bus gain (`sidCanonicalDrSidBusGain`), then the DrSID matrix.
10. **Forensic**: `buildEffectiveForensicConfigFromParams`, with the revision forced to the
    selector, sent to BitPerfect, DrSID and the register engine.
11. **SID-register seeding** on entry into SYNTH mode (§5).

Separately, `syncTempoLinkedRuntimeControllers` re-rates tempo-synced LFOs and the
tempo-synced arp whenever the host tempo changes. `advanceTempoLinkedRuntimeControllers`
steps the LFO bank once per frame.

### 10.2 Queued-register shadow

`sidQueuedShadow_` records, for each of the 32 register indices, the last value **queued**:
`value`, `valid`, `sample` and `cycle`.

- `pushSidWriteTimed_` pushes to the write queue *and* updates the shadow.
- `pushSidWriteTimedIfChanged_` skips the write when the shadow entry is valid and equal.
  This keeps reseeds idempotent.
- `syncSidQueuedShadowFromLive_` re-reads the live engine's registers when entering or
  reseeding. `$19` holds the system byte.
- The shadow is bookkeeping for the scheduler only. It is never an audio source.

### 10.3 C64 SID projection mirror

The C64 tab and HUD show a live C64 (CPU, VIC-II, CIAs, bus lanes, `$D400` registers) even
when no tune is loaded. They do this by **mirroring the engine's applied register writes
onto an emulated C64 bus**. The mirror is cosmetic: it is never an audio source.

- **Source of the writes.** The applied-write observer of the register-queue renderer
  (`runtimeMirrorAppliedProjectionWrite`) sees *exactly* the writes the audio engine
  consumed: seeds, note-ons and offs, glides, bends, pressure. Since v874 the mirror is fed
  only from there. Mirroring individual call sites had missed the scheduler writes.
- **Gating.** The observer is active only while a visible C64 view asks for it: demand
  serial plus a hold time (`beginC64TelemetryDemandBlock_`).
- **Clock.** Before the observer opens, `ensureC64ProjectionMirrorClockReady_` picks PAL or
  NTSC from the live PSID platform if one exists, otherwise from the runtime SID clock. An
  unknown clock falls back rather than guessing NTSC. On a change it resets, boots and
  starts the mirror C64, so no first-note burst is lost.
- **Timing.** `c64HostSampleOffsetToPhi2(sample, cycle, sr, phi2)` converts a host position
  to a φ2 offset with the same Q32 law and sentinel handling as the audio path (the v899
  fix). `projectSidHostTimedWriteThroughC64Bus` schedules `$D400 + reg` as a
  **projection-mirror CPU write** at that φ2 offset. The write goes through the PLA, so
  banking decides whether it reaches the SID, as on hardware.
- **Single image.** The C64 platform's own `sidRegisterImage()` is the only mirror state;
  the bridge keeps no second array. `reconcileSidRegisterImageThroughC64Bus` diffs that
  image against a rendered register image after a reset or restore. It writes only
  mismatches, through `cpuWrite`.
- **Early exit.** If `processBlock` exits early (for example the C64 SIDPLAY branch),
  `cancelC64TelemetryDemandBlock_` closes the observer and **flushes** (applies, then
  removes) the scheduled projection writes. The register image still lands on audio that
  already played.

**Projection bootstrap** (C64 Control Hub command 5, `loadC64SidProjectionBootstrap_`)
loads a tiny deterministic program at `$0801`, then boots and starts the mirror:

```
0801  A9 11     LDA #$11
0803  8D 00 D4  STA $D400
0806  A9 0F     LDA #$0F
0808  8D 18 D4  STA $D418
080B  4C 01 08  JMP $0801
```

It exercises the same `6510 → $D400 → sidRegisterImage()` path that PSID/RSID playback
uses. Other hub commands: 1 boot, 2 start, 3 stop, 4 reset. The VST3 adds 6/7 (VIC-II
fast on/off) and 8/9 (CPU fast on/off).

### 10.4 Register write authority, summarized

| Mode | Live register authority | Presentation mirrors |
|---|---|---|
| CLASSIC | each `SIDChip` inside BitPerfect | runtime register image (readback), C64 mirror |
| SYNTH | `SidRegisterEngine` + `SidWriteQueue` | queued shadow, runtime register image, register parameters, C64 mirror |
| DrSID | the `SIDChip` inside DrSID (and SID-808's chip) | scopes, C64 mirror |
| C64 player | the C64 machine's SID bridge | C64 snapshot |

A mirror is never written back into an authority.

### 10.5 Drum bridge: kit and context projection

`DrumEngineHostBridge` owns one `Sid808Engine` and one `DrumEngineRouter`. The router also
references the canonical `DrSidEngine`.

- **`loadFactorySlot(slot)`** runs off the render thread:
  - it sets the router identity from the slot's `DrumContext`;
  - for SID-808 slots 120–149 it applies the factory kit (`applySid808FactorySlot`);
  - it counts loads for diagnostics.
- **Production rule:**
  - Only the **SID-808** context renders through the bridge.
  - DrSID audio is always the canonical engine; the bridge is not given a DrSID identity.
  - In the Sid808 flavor, `configureDefaultDrumBridgeIdentityNonRealtime_` makes sure the
    bridge holds a SID-808 identity for the loaded slot.
- **Sync.** `syncDrumBridgeProjectionFromRenderParams_` projects only the chip model, the
  SID clock and the forensic configuration into the bridge. It runs at setup and on every
  sample-rate change.
- **Render.** When active, `renderDrumBridgeIfActive_` replaces the engine-bank drum
  output.
- **Telemetry** (`Sid808BridgeOutputTelemetry`): routed hits, the configured kit, the last
  class, note and velocity, output peak, active voices, silent-while-active counters and
  snare micro-stage timing.

### 10.6 GUI realtime projection

The MIX, KIT and DIGI tabs keep compact POD models (TAB_ARCHITECTURE.md). Before the render
thread reads them, `projectGuiRealtime(mix, kit, digi, step)` turns them into a bounded,
trivially copyable projection. It is at most 2 KB, schema 1, with no allocation, locks or
strings.

**MIX** (`projectMixRealtime`):

| Quantity | Law |
|---|---|
| Channel gain | `volume / 200` (255 → 1.275, about +2.1 dB); volume 0 is silent |
| Master gain | `masterVolume / 255`, × 0.3162 (−10 dB) when Dim is on |
| Pan | linear balance: `L = base × (pan > 0 ? 1 − pan : 1)`, `R = base × (pan < 0 ? 1 + pan : 1)` |
| Sends | `send / 255 × busReturn`; a disabled bus gives 0 |
| Stereo width | `width / 255 × 2` (0–200 %) |
| Limiter | threshold `−24 + 24 × b` dB; release `10 + 490 × b` ms |
| Solo and mute | if any channel is soloed, only soloed channels are audible |
| Active FX | per channel, slots with a type and no bypass are counted |

**KIT** (`projectKitRealtime`): the current step (wrapped to 32), the active engine target,
and for each of the 9 classes: GM note, active-at-step, step velocity, the DrSID, SID-808
and DIGI factory slots, the selected slot and the SID-808 voice override bytes.

**DIGI** (`projectDigiRealtime`): for each of the 8 slots: active-at-step, source type,
factory slot (absolute = 150 + index), velocity, tune, start, length, volume, flags and the
user-sample handle.

The KIT and DIGI sequencers and the MIDI DIGI pads read this projection, which is refreshed
once per step, never the live GUI models.

---

## 11. Virtual gate and note

`kParamVirtualGate` and `kParamVirtualNote` let a host automate a note as a parameter, for
example from a modulation lane:

- **While the host transport plays:**
  - a gate rising edge, or a note change while the gate is high, sends a note-on (velocity
    100, a fresh synthetic noteId) on the default event channel;
  - the previous virtual note gets its note-off first;
  - a falling edge sends the note-off.
- **While stopped:** any sounding virtual note is released.

The events go through the normal routing (§4), so they play in whichever mode is active.

## 12. Pure SID 1Q1 output

`pureSid1Q1OutputMode_` (AU state key; VST3 chunk `OUTM`) bypasses every post stage: MIX
FX, reverb, limiter, the ±1 clamp and Hi-Fi. The host receives the engine output as it is,
with only NaN and denormal clean-up.

A related **REC capture**:

- It records the same pre-post-FX stream into a preallocated buffer, whether or not 1Q1
  output is on.
- A render/control lease protects the render thread, and a control-side state machine
  (Idle / Recording / Busy) serializes the control operations.
- Peak, RMS and dropped-frame counts are published.
- It is used for bouncing raw SID audio into DIGI or to disk.

## 13. Invariants

- Exactly one render mode is active. DrSID beats SYNTH; neither means CLASSIC.
- The flavor policy is re-applied after every parameter flush.
- ARP is only effective in CLASSIC. SEQ is only effective in CLASSIC and DrSID.
- In SYNTH mode, the register engine and its timed queue are the only register authority.
  Register parameters seed it once on entry and are then only mirrors.
- Every SYNTH write is stamped `(sample, cycle)`. The unresolved sentinel means cycle 0.
  Hard-restart gate-ons land exactly 46 SID cycles later.
- Note-offs resolve by token first. An anonymous note-off never releases a noteId voice.
  The anonymous release order is LIFO.
- Projections are one-way: parameters → engines, engine writes → C64 mirror,
  kits → bridge, GUI models → render projection.
