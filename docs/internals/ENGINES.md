# Sound engines — low-level reference

This reference covers every engine that sits above the SID chip core (`SIDChip`, see
[SID_CHIP.md](SID_CHIP.md)) and below the kernel pipeline (see
[RUNTIME.md](RUNTIME.md)):

- the synth engines (BitPerfect, single-SID 3-voice, SID register);
- the note generators (arpeggiator, step sequencer);
- the modulation system (LFO bank, mod matrix);
- the three drum engines (DrSID, SID-808, DIGI) and their router and stem mixer;
- the post-processing (MIX FX, Hi-Fi Transcendence, PostFX automation, forensic model);
- the parameter presentation layer.

Every engine follows the same realtime rules:

- no allocation after `prepare()` / `setSampleRate()`;
- no locks and no virtual dispatch in per-sample code;
- every float input goes through `isfinite` and a clamp before it reaches state.

| Source | Contents |
|---|---|
| `include/arpsid/engines/bitperfect_engine.h` | `BitPerfectEngine`: 8-chip polyphonic synth plus the single-chip topology switch |
| `include/arpsid/engines/single_sid_three_voice_engine.h` | `SingleSidThreeVoiceEngine`: one `SIDChip`, 3 voices, deterministic stealing |
| `include/arpsid/engines/sid_register_engine.h` | `SidRegisterEngine`, `SidWriteQueue`, `SidRegFile`: register-stream renderer used by PSID/RSID playback and SID-register synth mode |
| `include/arpsid/engines/voice_manager.h` | `VoiceManager`: poly allocation, sustain and sostenuto, note identity |
| `include/arpsid/core/sid_voice_allocator.h` | `SidVoiceAllocator<N>`, `VoiceStealingPolicy`, choke groups |
| `include/arpsid/engines/arpeggiator.h` | `Arpeggiator`: 7 modes, sample-accurate timed events |
| `source/au3/ArpSIDSequencerEngine.h` | `SequencerEngine`: transport-window step sequencer |
| `include/arpsid/modulation/lfo.h` | `LFO`, `LFOBank` (4 LFOs) |
| `source/au3/ArpSIDModMatrix.h`, `include/arpsid/core/sid_mod_matrix_types.h` | `ModMatrix`, sources, targets, transforms |
| `include/arpsid/engines/drsid_engine.h` | `DrSidEngine`: SID-physics drum machine |
| `include/arpsid/core/drsid_instrument_program.h`, `drsid_kit_compiler.h`, `include/arpsid/engines/drsid_wavetable_program_runner.h` | DrSID register microprograms, the compiler and the runner |
| `include/arpsid/engines/sid808_engine.h`, `sid808_gm_projection.h` | `Sid808Engine`: analog x0x projection on one SID |
| `include/arpsid/core/sid_gm_drum_kit.h`, `drum_context.h`, `include/arpsid/engines/drum_engine_router.h`, `drum_engine_host_bridge.h`, `drum_stem_mixer.h` | GM drum map, context enum, routing, stem mix |
| `include/arpsid/engines/digi_d418_stream_engine.h`, `digi_sampler_engine.h` | 4-bit `$D418` DIGI stream and the legacy float sampler |
| `include/arpsid/audio/mix_fx_processors.h`, `include/arpsid/gui/mix_panel_model.h` | MIX channel strip FX |
| `include/arpsid/core/sid_hifi_transcendence.h` | Hi-Fi post chain |
| `include/arpsid/core/sid_postfx_timeline.h` | sample-accurate automation of reverb, limiter and Hi-Fi |
| `include/arpsid/core/sid_runtime_forensic_config.h` | forensic parameter → `ArpSIDForensicConfig` resolution |
| `include/arpsid/core/sid_parameter_presentation.h`, `include/arpsid/core/math_utils.h` | parameter units, value↔text, the canonical normalized laws |

---

## 1. BitPerfect engine (`BitPerfectEngine`)

BitPerfect is the default melodic synth. It implements `ISidIntervalRenderable`, so the
kernel's host-cycle dispatcher can advance it by an exact `[cycle, subphase)` interval
(see RUNTIME.md §4).

### 1.1 Topologies

| `SidChipTopologyMode` | Value | Behaviour |
|---|---|---|
| `MultiChipPolyIllusion` | 0 (default) | `std::array<SIDChip, 8>`: one full 3-oscillator SID per polyphonic voice. Musically a polysynth, not a C64. |
| `SingleChip3Voice` | 1 | all rendering is delegated to the embedded `SingleSidThreeVoiceEngine` (§2). One chip, never more than 3 sounding voices. |

`setSampleRate()` prepares both topologies, so switching mode allocates nothing: it calls
`allNotesOff()` and flips the flag. Every parameter setter writes to all 8 chips *and* to
the single-SID engine, which keeps both topologies in sync.

### 1.2 Voice modes

`setVoiceMode(norm)` maps to `idx = round(norm × 3)`:

| idx | Mode | Authority |
|---|---|---|
| 0 | Poly | `VoiceManager` (8 slots, stealing, sustain/sostenuto per channel) |
| 1 | Mono | forced-voice plane: last-pressed held note on voice 0, envelope retriggers |
| 2 | Legato | forced plane, no envelope retrigger while a note is held; the pitch is retargeted instead |
| 3 | Unison | forced plane on `unisonCount` (1–8) voices, all playing the top held note |

Switching modes is a hard safety barrier:

1. The held-note intent is captured with `seedForcedHeldFromPolyState()` or
   `seedForcedHeldFromForcedVoices()`.
2. `silenceHardwareAndClearPitchState_(true)` gates every voice off and clears glide state.
3. The voice manager and the forced state are reset.
4. The new mode is rebuilt from the captured intent.

This stops release tails, glide targets and slot bookkeeping from leaking across modes.

**Unison detune.** Slot *i* of *n* is detuned by

```
cents(i) = (2i/(n−1) − 1) × 24 × voiceSpread          (0 when n = 1)
ratio    = 2^(cents/1200)
```

The slots therefore spread symmetrically across ±24 cents at full spread.

### 1.3 Pitch path

```
note → Hz   : 440 × 2^((midi + masterTune + bend(channel) − 69) / 12)
Hz  → reg   : ArpSID_hzToSidFrequencyRegister(hz, clock) = round(hz × 2^24 / clock), clamped 0..65535
per osc     : reg × voiceDetuneRatio × (1 + vcoNDetune) × 2^(pitchModSemis/12)
```

- `masterTune`: `(norm − 0.5) × 2` semitones, i.e. ±100 cents.
- Pitch bend is 14-bit per channel: `(raw − 8192) / 8191` (or `/8192` below centre) ×
  the range. The range is 0–24 semitones per channel, default 2.
- `activeClockFreq` follows the PAL or NTSC selection. Earlier builds always used the PAL
  clock, which made NTSC 3.8 cents sharp; this is fixed.
- VCO detune: `ArpSID_normToDetuneCents(norm) = (norm − 0.5) × 200` → ±100 cents, applied
  as a ratio offset.
- A pitch change from bend, tune or detune calls `retuneActiveVoicesNoRetrigger()`. The
  sounding voices glide or jump to the new register value without re-gating.

### 1.4 Portamento

- Time law: `ArpSID_normToPortamentoSeconds(n) = n² × 5 s`.
- When the time is above 1 ms and the voice already has a frequency, `startVoice` builds a
  `makeDiscreteRegisterGlide(current, target, seconds, sampleRate, clock, style,
  c64FixedGlideDelta, videoFrameRate)`.
- The glide advances **per sample** inside the render loop through
  `advanceDiscreteRegisterGlide`. Earlier builds advanced it per block, which caused audible
  staircasing at large block sizes.
- The video frame rate is 50 Hz for PAL and 60 Hz for NTSC. It sets the step cadence of the
  frame-locked C64 portamento styles.
- `setC64FixedGlideDelta(norm)` sets the fixed per-step register delta,
  `round(norm × 255)` clamped to 1–255.
- The styles themselves are described in SID_CHIP.md §8.

### 1.5 Parameter quantization

All controls round to the nearest hardware step. Truncation would bias every knob one step
low.

| Setter | Law |
|---|---|
| Attack / Decay / Sustain / Release, Resonance | `quantizeNibble01`: `round(n × 15)` |
| Filter cutoff | `quantize11Bit01`: `round(n × 2047)` |
| Pulse width (per VCO) | `quantize12Bit01`: `round(n × 4095)` |
| Waveform (per VCO) | `floor(min(n, 0.999999) × 8)` → Tri, Saw, Pulse, Noise, Tri+Saw, Tri+Pulse, Saw+Pulse, Tri+Saw+Pulse |
| Filter mode | `int(n × 8)` → None, LP, BP, LP+BP, HP, Notch (LP+HP), BP+HP, LP+BP+HP |
| Filter routing | 3 booleans → `$D417` bits 0–2 |
| Sync / Ring mod | per VCO, `> 0.5`. The topology is cyclic, as in hardware: V1←V3, V2←V1, V3←V2 |
| Low-frequency mode | per VCO, `> 0.5` |
| Voice level | per VCO, 0..1 into the chip mixer |

### 1.6 Block render

`processBlock(outputs, n)` works in six steps:

1. **Topology dispatch.** In `SingleChip3Voice` the whole block comes from
   `singleSidEngine_.processBlock`. Steps 5 and 6 below still run.
2. **Active-voice gathering.**
   - In Poly, voices are aged by `n / sampleRate`.
   - Voices whose key is up, that are not pedal-held and whose three oscillators are all
     silent are garbage-collected (`releaseFinished`).
   - Voices that are still sounding a release tail are added, so poly release is never cut.
   - Forced modes add the forced voices plus any tails.
3. **Per-voice loop.**
   - When no pitch modulation is active, `applyOscFrequencies` runs once per block;
     otherwise it runs per sample.
   - Per sample: glide step, `chip.processSample(l, r)`, NaN flush, then × velocity gain.
   - The velocity gain is `sqrt(velocity)`: 0 at velocity 0, 1 at velocity 1.
4. **Scope capture.**
   - Scope capture runs only while a scope is visible (`setScopeCaptureEnabled`).
   - Per voice it stores the voice mix, and **sums** the per-oscillator and filter in/out
     taps. The taps are not averaged; the sum is what the mixer hears.
   - The ring holds 256 samples and is published through a triple buffer.
5. **Master gain ramp.**
   - `gain = masterVolume × (Poly ? 1.12 : 1.0)`, ramped linearly from the block-start
     value to the block-end value. This keeps automation from producing zipper noise.
   - `masterVolume` is clamped to 0–2.
6. **Output stage, once per output sample (not per voice).**
   - TPDF dither: two independent xorshift32 streams per channel, `(u₁ + u₂ − 1) × 2⁻²³`.
   - Then an isfinite check and a clamp to ±1.
   - The noise floor does not grow with polyphony because it is applied here, after the
     mix.

`renderIntervalAccurate(iv)` is the cycle-exact path. For every renderable voice it advances
the chip in three phases:

1. leading sub-cycle phases;
2. whole cycles;
3. trailing sub-cycle phases.

It averages by the actual subphase width. At the host-sample boundary it applies velocity
gain and the smoothed master gain, which slews at most 1/64 per fractional sample.

### 1.7 Forensic wiring

`applyForensicSettings_` builds each chip's `ArpSIDForensicConfig` from the composite
config. Each enable/amount pair is zeroed unless both the master enable and its own enable
are on. Each chip then gets:

- a per-chip ID seed, `mixSeed(base, chipIndex + 1)`, so the 8 chips differ like 8 physical
  parts;
- a startup-randomization seed derived from both analog-noise RNG words.

The single-SID chip gets the same treatment as index 0.

---

## 2. Single-SID 3-voice engine (`SingleSidThreeVoiceEngine`)

This is the authentic topology: exactly one `SIDChip` whose three hardware voices are the
allocation target.

- Allocation goes through `SidVoiceAllocator<3>` with a `VoiceStealingPolicy`:

  | Policy | Value | Rule |
  |---|---|---|
  | `StealOldest` | 0 (default) | take the slot with the largest `ageSamples` |
  | `StealQuietest` | 1 | take the slot with the lowest envelope |
  | `StealReleasingFirst` | 2 | prefer a slot already in release, else oldest |
  | `Refuse` | 3 | drop the incoming note when all 3 are busy |

- `tick(samples)` ages the slots once per block *before* allocation, so age is monotone
  within a block. Age saturates at `UINT32_MAX`.
- **Choke groups:** a note-on carrying `Drsid::ChokeGroup::HiHat` chokes any other HiHat
  voice on the chip.
- **Master volume:** `round(norm × 15)` is written to the chip's `$D418` volume nibble.
  This is the real DAC, not a float gain.
- **Voice level:** `level × velocity/127` per voice.
- Master tune and global bend are clamped to ±24 semitones. Per-channel 14-bit bend and
  range work exactly as in §1.3.

---

## 3. SID register engine (`SidRegisterEngine`)

This engine renders a *register-write stream* rather than notes. It is the output stage for:

- PSID/RSID playback (the 6510 writes `$D400–$D41C` through the SID bridge);
- the SID-register synth mode, where notes become timed register writes through
  `sid_runtime_synth_register_scheduler.h`;
- the isolated DIGI `$D418` layer (§8).

### 3.1 Register file and write queue

| Constant | Value | Meaning |
|---|---|---|
| `kSidBase` | `$D400` | |
| `kSidRegCount` | `0x1E` | 29 real registers + 1 pseudo system register (`$1D`) |
| `kSidLiveWritableRegCount` | `0x19` | `$00–$18` are writable |
| read-only | `$19–$1C` | POTX, POTY, OSC3, ENV3 |

- `sanitizeSidRegisterValue` masks the pulse-width high bytes (`$03`, `$0A`, `$11`) to 4 bits
  and FC LO (`$15`) to 3 bits.
- `SidRegFile::reset()` sets `$18 = $0F` (volume 15) and `$1D = $02`.
- A `SidWrite` holds `{regIndex, value, sampleOffset, cycleOffset, order}`.
- `SidWriteQueue::kMaxWrites = 65 536`, which is about 1 MB per queue. The sizing covers
  192 kHz × 2048-sample blocks under hostile forensic traffic, which is about 44 k writes.
  Real traffic is under 1 000 writes per block.
- On overflow a write to the same register is coalesced (last write wins); otherwise the
  oldest entry is retired. Both cases are counted in telemetry.

### 3.2 Interval render with subphase-timed writes

`renderIntervalAccurate(iv)`:

- **Phase 0.** Any queued write positioned *before* `iv.begin` is applied first. A tiling
  gap (a whole-cycle window ending at N followed by a span starting at (N, s > 0)) can
  otherwise skip the position (N, 0). A synth gate-on queued exactly there was silently
  lost before v898.
- **Phase 1.** Leading partial cycle: writes that land in the subphase range are applied
  first, then the chip advances.
- **Phase 2.** Whole cycles.
  - The no-write hot path renders the whole window in one scratch pass.
  - When any write is pending, the engine goes cycle by cycle and applies writes at cycle
    boundaries, so CIA, VIC and SID timing stays exact.
- **Phase 3.** Trailing subphases.
- Output is weighted by subphase width. `kMaxSubphaseWrites = 256` per subphase span.

### 3.3 Output stage

The register engine carries its own models of what sits between the SID and the jack:

- a DC blocker, a one-pole at 16 Hz;
- the motherboard low-pass/high-pass network;
- the `$D418` volume-DAC bias memory and asymmetry;
- bus-latch and POT-line state;
- envelope TDM hold.

Each of these is driven by the forensic config.

**Look-ahead limiter** (`LookaheadLimiter`):

- look-ahead of 5 ms (`round(sr × 0.005)` samples) in a 1024-sample ring, which is enough up
  to 192 kHz;
- a monotonic deque of peaks (O(1) amortised) with 64-bit indices, so it never wraps;
- threshold 0.9885531 (≈ −0.1 dBFS);
- instant attack; release time constant 50 ms;
- no artificial silence while the delay line fills, so the first note-on sample is audible.

The filter model is described in SID_CHIP.md §5. The parity-law result is cached and
recomputed only when (model, fc, res, drift, supply, revision) change. Before v822 it was
rebuilt every sample, which dominated audio-thread CPU.

Scope: 3 oscillators × 256 plus filter in/out × 256, published through a triple buffer. The
active mask is gate OR envelope counter > 0 OR hard restart pending.

---

## 4. Voice manager (`VoiceManager`)

`MAX_VOICES = 8`. Each slot is a `VoiceState`:

```
midiNote, channel, noteId, voiceToken (0 = unbound), velocity, age,
isActive (allocated or still sounding), keyDown, isSustained, isSostenuto
```

- **Identity.** Only a real host `noteId` is a safe retrigger identity. Two anonymous
  note-ons for the same pitch allocate *independent* voices. Otherwise two held keys would
  collapse into one voice and a FIFO note-off could release the wrong tail.
- **Token-first note-off.** `noteOff` by token does nothing when the token is 0. There is
  deliberately no fallback to identity matching; callers resolve the token first through
  `SidDynamicState::resolveVoiceTokenForEventIdentity()`.
- **Anonymous pairing.** A noteId-less note-off pairs only with an anonymous note-on, never
  with a voice that carries a real noteId.
- **Stuck-note guard.** Some hosts do not round-trip note IDs symmetrically. For them a
  last-resort call releases the oldest key-down voice with the same note and channel,
  ignoring noteId. It still honours the pedals.
- **Pedals.**
  - Sustain and sostenuto are tracked per channel.
  - Unscoped voices (`channel < 0`) fall back to the global sustain flag.
  - A voice that is already inactive is never marked sustained. Earlier builds could leave
    such a voice in sustained-but-inactive limbo.
- **Stealing.** The callback form of `noteOn` invokes `onSteal(voice)` so the engine can
  gate the victim off before reusing it.

---

## 5. Arpeggiator (`Arpeggiator`)

### 5.1 State

| Constant / field | Value |
|---|---|
| `MAX_PATTERN_STEPS` | 32 |
| `MAX_HELD_NOTES` | 128 (sorted ascending by pitch) |
| `kMaxTimedEventsPerProcess` | 512 |
| default rate | 4 Hz; gate 0.9; 1 octave; pattern length 16 |

- `physicalHoldCount[128]` counts overlapping note-ons of the same pitch, saturating at 255.
  The note leaves the buffer only when its count reaches 0.
- In Hold or Latch mode, released notes stay in the buffer. Turning both off rebuilds the
  buffer from the physically held keys.

### 5.2 Modes (`setMode`: `round(norm × 6)`)

Let `C` be the chord size, `O` the octave count and `T = C × O`.

| Mode | Cycle length | Index law |
|---|---|---|
| Up | T | `note = step % C`, `oct = (step / C) % O` |
| Down | T | `r = T − 1 − (step % T)`; `note = r % C`, `oct = r / C` |
| UpDown | `2T − 2` (T if T ≤ 1) | ping-pong without repeating the end notes |
| DownUp | `2T − 2` | mirror of UpDown |
| Random | T | two rejection-sampled draws (no modulo bias): note ∈ [0, C), octave ∈ [0, O) |
| Pattern | pattern length (1–32) | `v = pattern[step]`; `note = v % C`, `oct = (v / C) % O` |
| Chord | T | the whole chord sounds together, rotated by `step % C`, over all octaves (`getChordNotesForStep`, up to 32 cached notes) |

The final note is `buffer[note] + 12 × oct + transpose + jitter`, clamped to 0–127.

### 5.3 Parameter laws

| Parameter | Law |
|---|---|
| Rate (free) | `0.1 × 500^norm` Hz, clamped 0.01–50 Hz. At norm 0.5 this gives 2.24 Hz. |
| Rate (sync) | `(bpm / 60) / division` Hz; division 1 = quarter, 0.5 = eighth, 0.25 = sixteenth |
| Octaves | `1 + int(norm × 3)` → 1–4 |
| Gate | `0.1 + norm × 0.9` → 10–100 % |
| Swing | `norm × 0.75`. The step length is `base × (1 − swing)` on even steps (min 0.10) and `base × (1 + swing)` on odd steps. |
| Transpose | `round((norm − 0.5) × 48)` → ±24 semitones, symmetric |
| Random amount | 0..1, see §5.5 |
| Pattern length | `1 + round(norm × 31)` |

### 5.4 Sample-accurate event generation

`collectTimedEvents(numSamples, out, max)` emits `{sampleOffset, stepIndexBefore, {note,
velocity, gate}}` in six stages:

1. **Explicit block-start flush.** This runs only after a transport rewind or jump, or after
   a step boundary whose events did not fit (`flushGateOffAtBlockStart_`). The previous
   note's gate-off is emitted at offset 0. Before v961 every note flushed here, which chopped
   arp notes to about one block in mono, legato and unison.
2. A pending event carried over from the last block is emitted.
3. **First step fires immediately** on a fresh chord (`firstStepPending`), not one step
   period later. This matters for short taps.
4. Step loop:
   - The gate-off of the sounding note is emitted at its **true intra-step position**
     `stepLen × gate` when render time reaches it. Before v961 it was emitted only at the
     next step boundary.
   - At each boundary a gate-off is placed at least 1 sample before the new note-on, then
     the note-on follows.
5. With **portamento arp glide** on, the gate stays open between steps. The next note-on
   only retargets the pitch, which gives the classic C64 arp-slide/legato.
6. **Buffer exhaustion never loses a release.** The note stays armed and is flushed at the
   next block start.

Transport:

- `rewindPhase()` resets the step phase and keeps held or latched notes.
- `seedFromHostPosition(beat)` seeds the RNG from `beat × 16` (1/16-beat resolution). Random
  arps therefore bounce identically from the same start point.
- `setInstanceSeed()` makes instances differ from each other.

### 5.5 Random pitch jitter

For each note, two xorshift32 draws `u₁`, `u₂ ∈ [0, 1]` are made with `amt` = the random
amount.

- A move happens when `u₂ < 0.03 + 0.34·amt + 0.08·amt²`. Otherwise the offset is 0.
- The direction is the sign of `2u₁ − 1`.
- The magnitude is 1 semitone, or 2 semitones when `amt > 0.72` and `|2u₁−1| > 1 −
  ((amt−0.72)/0.28) × 0.14`.
- When `amt < 0.22`, moves only happen for `|2u₁−1| ≥ 0.70`.

The result is small, musical deviations rather than uniform noise.

---

## 6. Step sequencer (`SequencerEngine`)

### 6.1 Data

```cpp
struct SeqStep {            // sanitized on load
    int16_t midiNote = 60;  // 0..127
    float   velocity = .75; // 0..1
    float   gate = 1;       // 0 = rest, fraction = gate length
    bool    tied, accent;   // tie = extend, no retrigger; accent = +0.2 velocity
    uint8_t ratchet = 1;    // 1..4 subdivisions
    float   probability = 1;// 0..1
    bool    active = true;
};
struct SeqPattern { SeqStep steps[32]; int length = 8; uint32_t seed = 0xDEADBEEF; };
```

### 6.2 Timing

- `stepsPerBeat` defaults to 4, i.e. sixteenth notes.
- The step length is `(1/stepsPerBeat) × (1 ± swing/2)`, with the minus on even steps and the
  plus on odd steps. The swing parameter is 0..1.
- Tempo, when not synced to the host, uses the canonical law `20 + 280 × norm` BPM.
- `advanceWindow(beatStart, beatEnd, frames, out, boundaries…)` walks the host beat window:
  - a pending gate-off inside the window is emitted first;
  - at each step start it records a `StepBoundary{offset, step}`, **even for rests and
    ties**. KIT and DIGI consume these boundaries, so all three layers share one transport
    timeline;
  - then it fires the step.
- Beat → frame: `floor((beat − beatStart) / beatsPerFrame)`, clamped into the block.

### 6.3 Firing a step

1. **Probability.** The step is skipped when `xorshift(rng) / 2³² > probability`. The RNG
   starts from the pattern seed at every restart, so probability patterns are deterministic
   per cycle.
2. Inactive steps and steps with gate below 0.01 are rests.
3. **Tie.** If the step is tied and the same note is still gated, the gate-off is extended
   to `beat + stepBeats × gate` with no retrigger.
4. Otherwise the previous note is closed.
5. **Ratchet.** The step is divided into *r* equal parts. Each part gets a note-on and a
   gate-off at `part × gate × 0.95`. The last part's gate-off can carry into the next block.

### 6.4 Traversal and restart

| `SeqTraversalMode` | Rule |
|---|---|
| Forward | `step = (step + 1) % len` |
| Reverse | starts at `len − 1`, `step = (step − 1) % len` |
| PingPong | bounces with cycle `2·len − 2`; end steps are not repeated |
| Random | `xorshift(rng) % len` |

| `SeqRestartPolicy` | Resets the cursor on |
|---|---|
| OnTransportPlay (default) | the host play edge |
| OnLoopWrap | a host loop wrap |
| Free | never |

`syncToBeatPosition(beat)` places the cursor at `fmod(beat, cycleBeats)` by walking the
traversal order and summing swing-aware step lengths. Starting playback mid-song therefore
lands on the right step. Random mode starts at step 0.

---

## 7. Modulation: LFO bank and mod matrix

### 7.1 LFO (`LFO`, `LFOBank`)

There are 4 LFOs. They advance **one sample at a time** (`LFOBank::process()` per sample),
so modulation is sample-accurate.

| Shape (`round(norm × 6)`) | Output (phase φ ∈ [0, 1)) |
|---|---|
| Sine | 512-point table + linear interpolation |
| Triangle | `4φ − 1` for φ < ½, `3 − 4φ` otherwise |
| Sawtooth | `2φ − 1` |
| ReverseSaw | `1 − 2φ` |
| Square | `+1` for φ < ½, `−1` otherwise |
| SampleAndHold | a new bipolar random value on each phase wrap |
| Random | linear interpolation from the previous to the next random value over one cycle |

- Rate: the parameter law is `ArpSID_normToLfoRateHz(n) = 0.1 × 200^n` → 0.1–20 Hz, with
  1.41 Hz at n = 0.5. The engine clamps to 0.01–50 Hz.
- Tempo sync: `(bpm/60) / division`.
- Depth is 0..1, phase offset 0..1.
- Retrigger resets the phase to the offset and re-draws the S&H endpoints.
- The retrigger callback is a raw `void(*)(void*)` function pointer, not `std::function`,
  so it never allocates on the audio thread.
- Each LFO in a bank is seeded with `mixSeed(instance, i)`. After a reset, S&H starts from a
  pre-drawn value rather than 0.

### 7.2 Mod matrix (`ModMatrix`)

- `kMaxModRoutes = 32`.
- A `SidModRoute` is `{source, target, depth ∈ [−1, 1], transform, bipolar, enabled}`.
  It is active when enabled, both ends are non-None, and `|depth| > 1e−5`.

**Sources** (`SidModSource`, 21 plus None):

| Source | Notes |
|---|---|
| LFO1–4 | |
| Velocity | |
| NoteNumber | normalized |
| KeyFollow | (note − root) / 48 |
| ModWheel | |
| PitchBend | converted to bipolar: `2b − 1` |
| AfterTouch, PolyPressure | |
| Random | per-note seed |
| Macro1–8 | |
| Env1 | envelope follower |

**Targets** (`SidModTarget`, 13 plus None): FilterCutoff, FilterResonance, VCO1–3 Detune,
VCO1–3 PulseWidth, MasterVolume, LFO1–4 Rate.

**Transforms:** Linear; Squared (sign-preserving `v·|v|`); Abs; Invert.

Evaluation: for each active route,

1. `src = bipolar ? clamp(v, −1, 1) : clamp(v/2 + ½, 0, 1)`;
2. apply the transform;
3. `Δ = clamp(src × depth, −1, 1)`;
4. add Δ to the target's accumulator, clamped to ±1.

The consumer applies `clamp(base + Δ, 0, 1)` per target. Routes are persisted by explicit
enum index. Normalized-float decoding of a source exists only for the UI.

---

## 8. Drum engines

### 8.1 Context, factory ranges and routing

`DrumContext` selects the engine, factory range, note map and GUI tab:

| Context | Engine | Canonical factory slots |
|---|---|---|
| `DrSID_C64Wavetable` (1) | `DrSidEngine` | 80–119 |
| `SID808_AnalogProjection` (2) | `Sid808Engine` | 120–149 |
| `Digi4Bit` (3) | `DigiD418StreamEngine` | 150–179 |

- The ranges are checked at compile time (`static_assert`) to be disjoint, monotonic and
  inside 0–255.
- The legacy 0–127 bank's DrSID projection slots {47, 112–124, 127} are classified by
  `factorySlotContextLegacy()` and are never moved, so existing sessions keep their
  automation.
- `DrumEngineRouter` holds references (not ownership) to the engines and dispatches
  kit-loads and notes by the active `DrumKitIdentity.context`. `None` renders silence.

**GM drum map** (`sidGMDrumSpecForNote`):

- Every GM note 35–81 has a `{class, name, shortName, velocityScale, tuneOffsetNorm,
  decayScale}` entry. For example, 49 Crash Cymbal 1 is `OpenHat, 1.04, +0.18, 1.70`.
- The 8 classes are Kick, Snare, ClosedHat, OpenHat, Clap, Cowbell, Tom and Rim.
- MIDI channel 10 (index 9) is the GM drum channel.

**Stem mixer** (`mixDrumStemFrame`). Each output is clamped to ±1.

| `DrumStemMixPolicy` | Output |
|---|---|
| `AdditiveDrumMachine` (default) | main + DrSID + SID-808 + DIGI |
| `ReplaceWithSid808` | SID-808 only |
| `ExplicitSelectedStem` | main + the stems whose `use*` flag is set |

### 8.2 DrSID (`DrSidEngine`)

A drum machine rendered through **one real `SIDChip`**, with the same three-phase interval
render as BitPerfect.

**Playback modes** (`DrSidPlaybackMode`), shown in the HUD:

| Mode | What renders |
|---|---|
| Legacy (0) | original fallback path |
| Authentic (1) | SID physics + SID-authentic waveform sequences |
| Clean (2) | SID physics without filter irregularities |
| Overlay (3) | SID physics + analog overlay synthesis |
| Wavetable (4) | register microprograms (§8.3) |

**Machine models.** `DrSidMachineModel`: `SidAuthentic` (0) selects Wavetable playback with
no overlay. `AnalogX0X8` (1) selects Overlay playback with the digital overlay at 1.0.

**Voice layout** (`voiceIndexForGMClass_`):

| SID voice | Drums |
|---|---|
| 0 | Kick |
| 1 | Snare, Clap, Rim |
| 2 | Closed hat, Open hat, Cowbell, Tom |

**Trigger path.** `triggerMidiNote(note, vel)`:

1. Velocity below 1/127 is a note-off, per the MIDI spec.
2. The note resolves to a GM spec.
3. The spec's velocity scale is applied, then the class program.

Non-GM notes fall back through a range map only when `allowUnsupportedMidiFallback` is set:
≤36 kick, ≤40 snare, ≤44 tom, ≤46 closed hat, and so on.

`triggerKitMidiNote(…)` additionally applies a KIT tab voice override: waveform, AD, SR,
PW, flags and an override mask. The selected factory slot shapes the runtime (velocity
scale, tuning and decay) for the duration of the hit, through the scoped
`ScopedKitRuntimeShape_`.

**Per-drum controls:**

| Control | Range |
|---|---|
| Kick tune | 0..1 |
| Kick decay | 0.01 + n × 0.5 s |
| Snare tone, snare snap | 0..1 |
| Hat tune | 0..1 |
| Hat decay | 0.01 + n × 0.3 s |
| Clap decay | 0.05 + n × 0.4 s |
| Cowbell tune | 0..1 |
| Cowbell decay | 0.04 + n × 0.56 s |
| Tom tune | 0..1 |
| Tom decay | 0.04 + n × 0.56 s |
| Accent, output drive, hat metal, clap spread | 0..1, smoothed |

Performance inputs: pitch bend ±24 semitones, mod wheel and pressure.

**Output law** (`sid_runtime_drsid_gain.h`):

```
busGain    = clamp(master × drsidVolume × 1.08, 0, 1.0)
accentDyn  = clamp(0.18 + 0.66·env + 0.08·modWheel + 0.08·pressure, 0, 1)
accentGain = clamp((1 − b) + accentDyn·b, 0.25, 1),  b = 0.20 + 0.55·accentAmount
mixed      = (sid × accentGain + overlay) × gain × 0.82
out        = sidDrSidOutputShape(mixed)
             linear below the 0.82 knee, soft-bends peaks toward a 0.98 ceiling
AnalogX0X8 : mixed is first blended with tanh(mixed × (1 + 3.4·drive)),
             wet = 0.16 + 0.34·drive
```

**Clock.** An invalid clock (non-finite or ≤ 0) is *rejected*, not replaced by PAL, and
counted in `invalidClockFrequencyRejectCount`. A glitching upstream therefore cannot
silently re-pitch every drum by 3.8 %.

**Diagnostic allocator.** `SidVoiceAllocator<12>` tracks steal policy and choke groups for
telemetry only. It does not select render voices.

### 8.3 DrSID register microprograms

A drum hit is a short **program of timed SID register writes**, not an ADSR on an
oscillator.

```cpp
struct DrSidRegisterStep {          // exactly 16 bytes (static_assert)
    uint16_t cycleOffset;           // SID cycles since trigger, monotonic
    uint16_t durationCycles;
    uint16_t freq;                  // $D400/01 (voice-relative)
    uint16_t pulseWidth;            // 12-bit
    uint8_t  waveform;              // $D404 control: GATE 01 SYNC 02 RING 04 TEST 08 TRI 10 SAW 20 PUL 40 NOI 80
    uint8_t  attackDecay, sustainRelease;          // $D405, $D406
    uint8_t  filterCutoffLo, filterCutoffHi;       // $D415, $D416
    uint8_t  filterResRoute, modeVolume;           // $D417, $D418
    uint8_t  flags;                 // HardRestartTransient 01, VolumeDacClick 02,
                                    // FilterRouteChange 04, ADSRReArm 08, IsTerminal 80
};
struct DrSidInstrumentProgram {     // ≤ 32 steps × 16 + 64 bytes
    SidGMDrumClass drumClass; ChokeGroup chokeGroup; VoicePolicy voicePolicy;
    uint8_t stepCount; DrSidRegisterStep steps[32];
    DrSidControlMap controls;       // 8 bytes: (step, fieldOffset) for tune/decay/accent/colour
    DrSidExpectedFingerprint fingerprint;  // 16 bytes: CRC32 trace, step count, first word
    uint32_t schemaVersion = 1;
};
```

**Validation.** `programIsWellFormed` is `constexpr` and checks all of the following:

- the step count is 1–32;
- `drumClass` is not Unsupported;
- the schema version matches;
- `cycleOffset` never decreases;
- the terminal flag is set on the last step and only there.

**Choke groups:**

| Group | Members |
|---|---|
| None | |
| HiHat | closed hat chokes open hat |
| Cymbal | crash may choke open hat |
| TomShared | toms |
| NoiseShared | clap, snare and hat share the noise oscillator |

**Voice policies:** FixedVoice0–2, AnyFree, StealOldest, StealQuietest, ChokeGroup,
OverlayTableOnVoice.

**Canonical programs.** `makeCanonicalDrSidProgram(cls)` builds all 8 classes in `constexpr`
code. Examples:

- Kick: a triangle pitch drop from F4 (≈ 349 Hz) to E2 (≈ 82 Hz) over about 20 ms, with a
  hard-restart transient on step 0.
- Clap: three detuned noise bursts, then a noise tail.
- Closed hat: a short noise burst in the HiHat group.

**Compiler.** `DrSidKitCompiler` runs off the render thread. It validates each program,
computes a table-free CRC32 (IEEE) over the `(cycleOffset, regIndex, value)` trace and
stores it in the fingerprint. It then produces a trivially copyable `CompiledDrSidKit` with
a generation counter, so the render thread can detect a hand-off. CI pins audible behaviour
by fingerprint instead of by WAV files.

**Runner.** `DrSidWavetableProgramRunner`, one per SID voice:

- `trigger()` applies step 0 immediately.
- `tickSidCycles(n)` applies every step whose `cycleOffset ≤ cyclesSinceTrigger`. It is
  ticked on the same cycle boundary as the chip physics, including split intervals that
  complete a cycle (v903).
- A step sets frequency, PW, TEST, waveform, sync, ring, gate and ADSR. With
  `FilterRouteChange` it also sets cutoff, resonance, routing, mode and volume.
- `HardRestartTransient` pulses TEST high and then low.
- `abort()` implements choke.

### 8.4 SID-808 (`Sid808Engine`)

This is a separate, clean x0x-projection engine built on its **own**
`SingleSidThreeVoiceEngine`: one 8580 chip, `StealOldest`. It shares no state with DrSID.

**Fixed voice per family** (audit #42):

| Voice | Drums | Choke group |
|---|---|---|
| 0 | Kick | None |
| 0 | Tom | TomShared |
| 1 | Snare, Clap, Rim | NoiseShared |
| 2 | Closed hat, Open hat | HiHat |
| 2 | Cowbell | Cymbal |

Since v857 toms no longer choke cowbells on another voice.

**Default voice configs** (`Sid808VoiceConfig`, 12 bytes: freq, PW, `$D404` wave, AD, SR,
flags ring/sync/filter, level):

| Drum | Freq | PW | Wave | AD | SR | Flags | Level |
|---|---|---|---|---|---|---|---|
| Kick | `$0900` | 0 | TRI | `$04` | `$18` | | 0.98 |
| Snare | `$3000` | 800 | PUL+NOI | `$01` | `$06` | filter | 0.88 |
| Closed hat | `$7FFF` | 0 | NOI | `$00` | `$11` | | 0.68 |
| Open hat | `$7FFF` | 0 | NOI | `$00` | `$78` | filter | 0.72 |
| Clap | `$5200` | 0 | NOI | `$00` | `$35` | | 0.85 |
| Cowbell | `$4A00` | `$450` | PUL | `$00` | `$87` | filter | 0.78 |
| Tom | `$1600` | 0 | TRI | `$04` | `$79` | | 0.95 |

**Note-on:**

- Velocity 0 is a note-off.
- A velocity above the accent threshold boosts the level and tightens the decay.
- `midiNoteHint` shifts pitch by semitones from the canonical note.
- The config is first compensated for the chip model.

**Micro-stages.** Each hit schedules up to 4 timed stages (`Sid808MicroStage`: sample
delay, config, level, gate):

- the kick's high-to-low pitch drop through punch, body and tail;
- the clap's burst train;
- the cowbell's time-multiplexed pair of pulse partials, since one SID voice has only one
  oscillator;
- the open hat's ring tail.

`prepare()` clears all timers, so a sample-rate change cannot leave stages timed for the old
rate.

**GM projection** (`sid808_gm_projection.h`):

- Each GM note maps to a `Sid808PercProfile`. There are 33 profiles besides Default:
  toms, bongos, congas, timbales, cuicas, cymbals, whistles, guiros, shaker, tambourine,
  pedal hat, ride bell, agogos, triangle mute/open, vibraslap, claves and wood blocks.
- Each profile has a frequency ratio against its family's base voice. Examples: bongo high
  1.62, timbale high 1.84, ride 0.78, agogo high 1.92.
- The GM tune offset spans ±4 semitones.

`Sid808HitOverride` lets a KIT slot override any config field for one hit.

### 8.5 DIGI: 4-bit `$D418` stream (`DigiD418StreamEngine`)

This is the authentic C64 digi trick. Sample playback is done by **writing the SID volume
register** at a fixed PHI2-derived rate; it is not float mixing.

**Configuration (`DigiD418Config`):**

| Field | Default | Meaning |
|---|---|---|
| `authMode` | `StandaloneD418Layer` (0) | 0 = C64-bus-authentic (honours IO/open-bus); 1 = fast private preview; 2 = legacy float layer |
| `clockSource` | `Phi2FixedRate` | CIA-timer and VBlank values are reserved and sanitized to PHI2 |
| `digiRateHz` | 8000 | clamped 1000–32 000 |
| `respectIoBank` | true | writes are blocked, and counted, when IO is not visible at `$D000` |
| `driveOpenBus` | true | each write drives the open-bus latch |
| `preserveD418HighNibble` | true | the high nibble (filter mode, 3OFF) is kept |
| `useExternalD418HighNibble` | false | take the high nibble from the owning SID authority instead of a scratch bridge |

**Pipeline per block:**

1. **Schedule.** `phi2PerWrite = phi2Hz / digiRateHz`. Writes land at exact PHI2 cycles and
   are mapped to host frames with `digiRateHz / hostRate` writes per frame.
2. **Mix voices.** There are up to 8 slots (`kDigiActiveSlotCount`). Each voice advances
   with `inc = max(0.035, sourceRate / digiRate × pitch)`, supports loop and reverse, and
   carries a gain.
3. **Quantize.** `nibble = clamp(round((mix + 1) × 7.5), 0, 15)`.
4. **Emit.** `$D418 = (highNibbleSource & $F0) | nibble` goes through the C64 bus model.
   Every write can be logged as a `DigiD418ForensicEvent`: PHI2 cycle, host frame, old/new
   `$D418`, nibble, open bus before/after, IO visibility, whether the SID accepted it, and
   the full `C64BusEvent`. The last 64 events are kept.
5. **Render.** The kernel renders the writes through an **isolated** `SidRegisterEngine`, so
   a DIGI layer can never corrupt the PSID/RSID runtime's SID.

**Guards:**

- A timeline discontinuity is a block whose PHI2 start does not continue the previous one:
  a transport jump, an offline-bounce seek, or a changed host quantum. On one, the pending
  write schedule is reset and counted, so stale future writes are never pulled across.
- Changing the auth mode kills voices, so a stale sample cannot resume.

**Telemetry:** write counts (total, this block, accepted by the SID, blocked by IO bank,
queue overflow), collisions, open-bus drives, voice steals, MIDI trigger/ignore counts, last
nibble and last `$D418`, scope peak.

**Pattern model** (`digi_panel_model.h`):

- 8 slots × 32 steps; step velocity 0–127, default 100.
- Slot flags: Loop `0x01`, Reverse `0x02`.
- Sources: factory waveform recipes (quantized to the nearest nibble), PCM8 clips
  (`digiPcm8ToD418Nibble`), and user imports.
- The v2 blob is 360 bytes.

**Legacy sampler.** `DigiSamplerEngine` is the pre-D418 float sampler (`LegacyFloatLayer`).

- Additive layering mixes on top of the SID output; Exclusive zeroes the output first.
- It has sub-block trigger offsets and 128-sample scopes.
- It is kept only so old sessions load. It does not model the bus.

---

## 9. Post-processing

### 9.1 MIX panel and FX processors

**Model** (`MixPanelModel`, 1264 bytes, schema 1):

| Part | Contents | Size |
|---|---|---|
| 16 × `MixChannel` | enabled, solo, mute; volume (−inf..+6 dB); pan (128 = centre); delay send; reverb send; 5 FX slots | 72 B each |
| 2 × `MixSendBus` | enabled, return level, 2 FX slots | 32 B each |
| `MixMaster` | volume (255 = unity); limiter on, threshold (−24..0 dB), release (10–500 ms); stereo width (0–200 %, 128 = unity); dim (−10 dB) | 32 B |

A `MixFxSlot` is 12 bytes: type, bypass, 8 parameter bytes (`byte / 255` → 0..1).

**Processors** (`MixFxProcessor`):

- The processor is a tagged struct with a per-type switch: no vtable, no heap, fixed size
  (≤ 512 B).
- Coefficients are recomputed per `setParams()` call, i.e. at block rate.

| Type | Parameters (byte → value) | Implementation |
|---|---|---|
| Eq3Band | low shelf ±12 dB @ 50–1000 Hz; mid bell ±12 dB @ 200–8000 Hz, Q 0.3–8; high shelf ±12 dB @ 2–16 kHz (frequencies and Q log-mapped, 128 = 0 dB) | three RBJ Audio-EQ-Cookbook biquads, `y = b0x + b1x₁ + b2x₂ − a1y₁ − a2y₂` |
| Transient | attack gain ±12 dB; sustain gain ±12 dB | fast envelope (instant attack) against slow envelope. `tr = clamp((fast − slow)/slow, 0, 1)` blends the attack and sustain gains. |
| Compressor | threshold −40..0 dB; ratio 1:1–20:1 (log); attack 0.1–100 ms (log); release 10–500 ms (log); make-up 0..+24 dB | feed-forward VCA. Gain reduction uses one-pole ballistics, `coef = exp(−1/(sr·t))`. |
| Saturator | drive 0..+24 dB (smoothed per sample); character 0–84 tape, 85–170 tube, 171–255 transistor | tape: `tanh(x·d)/tanh(d)`; tube: tanh on the positive half, `xd/(1 + |xd/2|)/d` on the negative half; transistor: `(xd/√(1+xd²)) / (d/√(1+d²))` |
| Bitcrusher | bits 4–16 (255 = 16 = transparent); downsample 16×–1× sample-and-hold | |

### 9.2 Hi-Fi Transcendence (`SidHiFiTranscendence`)

This is an optional enhancement chain after the SID.

**Quality levels:**

| `HiFiQuality` | Behaviour |
|---|---|
| `PureEmulation` (0) | exact bypass; the SID output is untouched |
| `HighFidelity` (1) | transparent enhancement (scaled-down chain) |
| `Transcendence` (2) | full "super-hires" chain |

**Config (`SidHiFiConfig`, sanitized):**

| Field | Default | Range |
|---|---|---|
| oversampling | 8 | 1–16 |
| masterWidth | 1.22 | 0.5–2.0 |
| tapeSaturation | 0.45 | 0..1 |
| analogWarmth | 0.68 | 0..1 |
| psychoExciter | 0.82 | 0..1 |
| stereoDepth | 0.65 | 0..1 |
| voiceDiffuserAmount | 0.35 | 0..1 |

The flags `perVoiceDiffuser`, `dynamicResonanceEnhance` and `cabinetModeling` are all on by
default. The width parameter maps as `0.80 + n × 0.70`.

**Per sample, after a dry scan of the block:**

1. **Silence stays silent.** If the dry peak is below 1e−9 the block is zeroed and nothing is
   added. There is no free-running "air" in empty buffers.
2. **Voice diffusion.** A small per-channel diffuser, 12 diffuser states.
3. **Mid/side.** `side × (1 + (width − 1) × qualityScale)`.
4. **Psycho-exciter.** `tanh(high × 7.2) × 0.085 × amount` on the high band per channel.
5. **Tape saturation × warmth** on the mid channel. The temperature factor comes from the
   forensic junction temperature: `(Tj − 25) × 0.012`, clamped −0.25..0.85, or 0 when
   forensic is frozen.
6. **Cabinet modelling.** Warmth and body integrators plus a resonance follower through
   tanh.
7. **Air.** Signal-gated and anti-correlated between the channels.
8. **Safety gain.** Instant attack; release `+0.001 × (1 − g)` per sample (≈ 7 ms at
   44.1 kHz). Output is always finite and bounded.
9. **Mono.** A mono or aliased-L/R host folds back safely and keeps the tape, warmth, air
   and body changes.

All controls ramp sample by sample (`tau`-based coefficient per block), so a preset change
cannot zipper. Telemetry reports the dry, wet and delta peaks, the mono correlation and the
safety gain.

### 9.3 PostFX automation timeline

`SidPostFxAutomationState` holds:

- reverb mix;
- limiter enable, threshold (0.5–1.0), attack (`n × 20` ms) and release (`10 + n × 990` ms);
- the 9 Hi-Fi parameters, `kParamHiFiEnable`…`kParamHiFiVoiceDiffuser`.

`sidApplyPostFxAutomationEvent` applies `AutomationPoint` events at their exact sample
offset, so PostFX automation is as sample-accurate as synth automation.

### 9.4 Forensic model resolution

`buildRawForensicConfigFromParams` reads the FORENSIC tab parameters:

| Parameter | Law |
|---|---|
| Temperature | `20 + 40n` °C |
| Supply | `4.5 + n` V |
| Revision | `2 + round(3n)`: 6581 R2, 6581 R3, 6581 R4AR, 8580 R5 |
| Chip seed | `round(n × (2³² − 1))` |
| Clock jitter, supply ripple, thermal drift, voice crosstalk, external bleed | each an amount gated by its enable |
| Envelope TDM, `$D418` asymmetry, filter ohmic, system noise, motherboard, ADC bleed, bus collision, POT input | 0..1 |
| Startup randomization, 8580 digifix | booleans |

`resolveEffectiveForensicConfig(raw, variant, static)` then:

- clamps every field;
- forces the revision to agree with the chip family;
- derives a seed from the variant when the seed is 0;
- bounds the thermal network:
  - junction temperature 15–92 °C, case 15–68 °C, ambient 0–55 °C;
  - R_jc 1–80, R_ca 1–160;
  - C_j 0.001–1, C_c 0.001–4;
  - time constant 1–180 s (default 18 s);
- clamps ripple to 0–250 mV.

When the config is **frozen**, every stochastic or drifting term is zeroed and the three
temperatures collapse to the set point. The result is bit-reproducible "forensic but
deterministic" renders.

---

## 10. Parameter presentation (`sid_parameter_presentation.h`)

There are 512 parameters (`kNumParams`). Each has a `ParameterUnitDescriptor`:

- a `SidParameterUnit`: Normalized, Seconds, Milliseconds, Hertz, Cents, Bpm, Steps,
  Percent, Celsius, Volts, Byte, Index, IndexedLabel, Boolean or Seed;
- a display suffix.

Every wrapper uses this one layer:

- AU `parameterStringFromValue` / `valueFromString`;
- VST3 `getParamStringByValue` / `getParamValueByString`;
- the Cocoa knob captions and the VSTGUI value labels.

**Canonical laws** (`math_utils.h`). Wrappers must never re-derive these.

| Quantity | norm → value |
|---|---|
| Portamento | `n² × 5` s |
| Detune | `(n − 0.5) × 200` cents |
| LFO rate | `0.1 × 200ⁿ` Hz |
| Limiter attack | `n × 20` ms |
| Limiter release | `10 + n × 990` ms |
| Sequencer tempo | `20 + 280n` BPM |
| Sequencer steps | `1 + round(31n)` |

**Choice index** (`sidParameterChoiceIndex`):

| Parameter | Index law |
|---|---|
| waveforms, filter mode | `floor(min(n, 0.999999) × (steps + 1))`, the same binning the engine uses |
| arp octaves | `int(n × 3)` |
| everything else | `round(n × steps)` |

Stepped parameters get named choices ("Tri+Saw", "LP+BP", "Poly", …). Text entry parses
names as well as numbers. The unit (and so the AU unit) is unchanged by naming.

VST3 groups parameters into units (`IUnitInfo`, in `source/arpsid_controller.cpp`):

- one root unit, which owns the 180-slot program list, `Program` and `Bank Slot`;
- one unit per editor tab, taken from `EditorLayout::tabIndexForParam`
  (`source/gui/vstgui/arpsid_editor_layout.h`);
- a "Host MIDI / read-only" unit with 13 child units, one per host-controller block of 16
  channels.
