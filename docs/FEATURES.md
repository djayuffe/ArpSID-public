# ArpSID feature reference

This is the complete list of what ArpSID does. For each area it says what the feature is,
how it behaves, which controls drive it and their ranges, and where it lives in the editor.

- Every parameter ID, default and step count: [PARAMETER_REFERENCE.md](PARAMETER_REFERENCE.md)
- Numbers and limits: [TECHNICAL_SPECIFICATIONS.md](TECHNICAL_SPECIFICATIONS.md)
- The editors tab by tab: [AU_EDITOR.md](AU_EDITOR.md) (macOS) and
  [VST3_EDITOR.md](VST3_EDITOR.md) (Windows/Linux)
- How each feature is implemented: [internals/](internals/README.md)

## Contents

1. [One engine, every format](#1-one-engine-every-format)
2. [Render modes and flavors](#2-render-modes-and-flavors)
3. [The SID chip](#3-the-sid-chip)
4. [Voices and playing](#4-voices-and-playing)
5. [Oscillators](#5-oscillators)
6. [Envelope](#6-envelope)
7. [Filter](#7-filter)
8. [Modulation](#8-modulation)
9. [Arpeggiator](#9-arpeggiator)
10. [Step sequencer](#10-step-sequencer)
11. [SID register synthesis](#11-sid-register-synthesis)
12. [DrSID drum machine](#12-drsid-drum-machine)
13. [SID-808](#13-sid-808)
14. [KIT editor and GM drums](#14-kit-editor-and-gm-drums)
15. [DIGI sampler](#15-digi-sampler)
16. [C64 tune player](#16-c64-tune-player)
17. [Forensic analog model](#17-forensic-analog-model)
18. [Mixer, effects and output](#18-mixer-effects-and-output)
19. [Hi-Fi Transcendence](#19-hi-fi-transcendence)
20. [Presets, banks and files](#20-presets-banks-and-files)
21. [MIDI](#21-midi)
22. [Host integration](#22-host-integration)
23. [Editors](#23-editors)
24. [Standalone app](#24-standalone-app)
25. [State and recall](#25-state-and-recall)
26. [Telemetry and visualisation](#26-telemetry-and-visualisation)
27. [Reliability and realtime safety](#27-reliability-and-realtime-safety)

---

## 1. One engine, every format

- **One DSP kernel.** `ArpSIDDSPKernel` renders every format: AUv2, AUv3, Standalone and
  VST3 on macOS, Windows and Linux. The wrappers only translate host events, state and the
  editor, so a patch sounds the same in every format and on every OS.
- **One parameter space.** There are 512 parameters, and a parameter has the same number in
  every format (VST3 parameter ID = AU parameter address). Host automation, presets and
  projects therefore carry across.
- **One presentation layer.** Units, named choices and text parsing come from one service,
  so a host shows the same text in every format.

## 2. Render modes and flavors

**Render modes** are chosen in the editor header or by parameter:

| Mode | What plays |
|---|---|
| `CLASSIC` | the BitPerfect SID synthesizer (§4–§8) |
| `SYNTH / SID REG` | notes become timed SID register writes; the register image you edit is what the chip plays (§11; full detail in [internals/SYNTH_MODES.md](internals/SYNTH_MODES.md)) |
| `DR SID` | the DrSID drum machine (§12) |

**AU flavors.** Five components share one binary. Each starts in the mode that fits it and
shows flavor-specific tab names.

| Flavor | Subtype | Starts as |
|---|---|---|
| ArpSID | `ArpS` | the full instrument |
| ArpSID Instrument | `ArIn` | a melodic instrument |
| DrSID | `DrSD` | the drum machine |
| SID-808 | `S808` | the x0x drum machine |
| C64 SID Player | `C64P` | the tune player |

## 3. The SID chip

- **Chip revisions.** MOS 6581 R2, R3 and R4AR, and MOS 8580 R5. Each has its own:
  - filter cutoff curve (9 calibration anchors);
  - resonance law;
  - waveform DAC non-linearity;
  - combined-waveform behaviour;
  - output gain;
  - DC offset.
- **Clock.** PAL (985 248 Hz) or NTSC (1 022 727 Hz). Pitch is computed from the active
  clock, so NTSC is not 3.8 % sharp. A change applies on a sample boundary.
- **Cycle-exact core.**
  - The core steps every SID cycle. Each cycle is divided into 256 subphases, so a register
    write lands at its exact cycle and subphase rather than at a block edge.
  - One output sample is the average of every cycle it covers.
  - **Oversampling** (1×, 2×, 4×, 8×) combines sub-samples with a Hann window.
- **Authentic chip behaviours:**
  - the 24-bit accumulator;
  - the 23-bit noise LFSR (taps 22 and 17), including noise lock-up when combined with
    other waveforms;
  - the TEST bit;
  - hard sync and ring modulation with the real cyclic topology (V1←V3, V2←V1, V3←V2);
  - the hard restart (46-cycle gate-low window);
  - the optional **6581 ADSR bug** (the rate-counter delay real chips show).
- **Output stage:**
  - an optional **external RC filter** modelling the C64 output network;
  - **8580 Digifix**, the `$D418` DC bias trick that lets 8580 digis play;
  - **Startup Random**, a random chip state at power-on;
  - **Pure SID 1Q1** output mode, which bypasses everything after the chip.
- **Topology** (SETTINGS):
  - *Poly-Illusion* (default): 8 SIDs, up to 24 oscillators.
  - *Authentic single SID*: one 3-voice chip; the 4th note steals by policy.

## 4. Voices and playing

- **Voice modes:**

  | Mode | Behaviour |
  |---|---|
  | `POLY` | 8 voices; stealing when all are busy; release tails keep sounding after note-off |
  | `MONO` | last-note priority; every new note retriggers the envelope |
  | `LEGATO` | overlapping notes glide without retriggering |
  | `UNISON` | voices stacked on the top note, detuned symmetrically up to ±24 cents by **Voice Spread**. CLASSIC stacks 4 voices; SYNTH mode stacks 1–3 (from Voice Spread). |

  Switching mode never leaves stuck notes or stale glides behind: the held keys are carried
  across, and the old mode's voices are silenced first.
- **Portamento:**
  - time 0–5 s, quadratic knob law;
  - four styles:

    | Style | Behaviour |
    |---|---|
    | `C64 SLIDE` | even register steps spread over the glide time on the SID-cycle lattice, like a C64 player routine |
    | `C64 FIXED` | a fixed register delta per video frame (50/60 Hz), set by **C64 Glide Delta** (1–255) |
    | `LINEAR` | linear in semitones, interpolated per sample |
    | `SMOOTH` | the `C64 SLIDE` steps timed on host samples instead of SID cycles |

  - Glides are written as SID frequency-register steps and advance per sample, so large
    host blocks cause no staircasing.
- **Tuning:**
  - Master Tune ±100 cents;
  - per-oscillator Detune ±100 cents;
  - pitch bend per channel (14-bit) with a per-channel bend range of 0–24 semitones via RPN
    0, default 2.
- **Expression:** velocity → loudness with a square-root curve; per-channel aftertouch,
  poly pressure, mod wheel, breath and expression as mod sources.
- **Pedals:** sustain (CC64) and sostenuto (CC66), tracked per channel.
- **Note identity:**
  - Host note IDs (VST3) are kept end to end.
  - Anonymous repeated notes get separate voices.
  - Replayed and synthetic notes get stable identity tokens, so a note-off always releases
    the right voice.
- **Safety:**
  - a stuck-note reconciler with a two-block grace period;
  - Panic;
  - channel-scoped All Notes Off (CC123) and All Sound Off (CC120).

## 5. Oscillators

Each of VCO 1–3 has:

- **Waveform:** `TRI`, `SAW`, `PULSE`, `NOISE`, `TRI+SAW`, `TRI+PUL`, `SAW+PUL`,
  `TRI+SAW+PUL`.
- **Pulse width:** 12-bit, 0–4095.
- **PWM depth:** from the LFOs.
- **Detune:** ±100 cents.
- **Level:** 0..1.
- **LF mode:** runs the oscillator at LFO rates, for use as a modulation source or drone.
- **Sync** and **Ring mod:** use the previous voice in the SID's cyclic order as source.

The mod matrix can modulate detune and pulse width per oscillator.

## 6. Envelope

- **Controls.** SID ADSR: Attack, Decay and Release (16 rates each, from 2 ms to 8 s attack
  and 6 ms to 24 s decay/release) and Sustain (16 levels).
- **Rounding.** Knob positions round to the nearest hardware nibble.
- **Behaviour.** The envelope behaves like the real counter: exponential decay steps,
  retrigger from the current level, and the optional 6581 ADSR bug.
- **Filter envelope.** The FILTER tab has its own ADSR and envelope amount.

## 7. Filter

- **Modes:** the SID multimode filter with 8 settings: `OFF`, `LOW-PASS`, `BAND-PASS`,
  `LP+BP`, `HIGH-PASS`, `NOTCH`, `BP+HP`, `ALL`.
- **Routing:** per voice (V1, V2, V3) and EXT IN.
- **Controls:** Cutoff (11-bit), Resonance (4-bit), Drive, KeyTrack, Env Amount, LFO Amount
  and a filter ADSR.
- **Model:**
  - a per-revision cutoff curve and Q law;
  - 6581 distortion and bias, and the cleaner 8580 behaviour;
  - **Filter Ohmic** (forensic): the resistor behaviour of the real ladder.
- **Display:** a live response curve and input/output scopes in both editors.

## 8. Modulation

- **4 LFOs:**
  - Rate 0.1–20 Hz (exponential) or host-tempo Sync;
  - Depth;
  - Shape: `SINE`, `TRIANGLE`, `SAW`, `RAMP DOWN`, `SQUARE`, `S&H`, `RANDOM` (smooth);
  - phase offset and retrigger.
  - The LFOs run per sample and are seeded per instance.
- **8 macros** for automation and the matrix.
- **Mod matrix:**
  - up to 32 routes;
  - sources: LFO 1–4, velocity, note number, key follow, mod wheel, pitch bend, aftertouch,
    poly pressure, random, Macro 1–8, envelope;
  - targets: VCF cutoff and resonance, VCO 1–3 detune and pulse width, master volume,
    LFO 1–4 rate;
  - per-route depth ±1, bipolar or unipolar, and a transform (linear, squared, abs, invert).
  - Deltas sum per target and are clamped.
- **Live display.** Knobs show the live modulated value (macOS editor); the VST3 editor
  shows live source meters.

## 9. Arpeggiator

- **Modes:** `UP`, `DOWN`, `UP/DOWN`, `DOWN/UP` (ping-pong without repeating the end notes),
  `RANDOM` (unbiased), `PATTERN` (a 32-step index pattern), `CHORD` (the whole chord,
  rotated per step).
- **Rate:** free 0.1–50 Hz (exponential), or synced to host tempo by division.
- **Other controls:**

  | Control | Range |
  |---|---|
  | Octaves | 1–4 |
  | Gate | 10–100 % (shapes the real duty cycle at any rate) |
  | Swing | 0–75 % |
  | Transpose | ±24 semitones |
  | Random | musical ±1–2 semitone deviations |
  | Pattern length | 1–32 |

- **Hold** (keeps released notes) and **Latch** (keeps the chord until a new one is played).
- **Glide legato:** keeps the gate open between steps for the classic C64 arp slide.
- **Timing:**
  - The first step sounds immediately on a new chord.
  - Every note-on and note-off is placed at its exact sample.
  - A release is never lost, even when a block is full.
- **Transport:**
  - Follows host tempo and position.
  - Random arps are seeded from the song position, so every bounce is identical.

## 10. Step sequencer

- **Steps.** 32 steps, each with:
  - note, velocity and gate length (a gate of 0 is a rest);
  - tie (extend, no retrigger);
  - accent (+0.2 velocity);
  - ratchet (1–4 repeats);
  - probability;
  - active.
- **Traversal:** `FORWARD`, `REVERSE`, `PING-PONG`, `RANDOM`; length 1–32; swing.
- **Tempo:** internal 20–300 BPM or host. Restart on play, on loop wrap, or free-running.
- **Deterministic probability.** The same pattern gives the same result every cycle.
- **Mid-song start.** Starting playback mid-song lands on the correct step.
- **One clock for all layers.** The sequencer's step boundaries also drive the KIT drum
  pattern and the DIGI pattern, so melody, drums and samples stay locked.
- **Drum pattern pages.** On the macOS SEQ page: 16 × 16 steps with STAMP and CLEAR.

## 11. SID register synthesis

- **Direct register editing.** Edit `$D400–$D418` on the SID REG tab. The chip plays that
  register image directly.
- **Timed writes.** Notes become timed register writes (frequency, control, ADSR, delayed
  re-gates, hard restarts) at exact cycles.
- **Identity.** Canonical voice tokens keep host note identity; replayed and anonymous notes
  get stable synthetic tokens.
- **System register.** `$D41D` system register and chip options (revision, external RC,
  oversampling, ADSR bug).

## 12. DrSID drum machine

- **Real SID drums.** Drums are played on a real SID core as per-hit register
  micro-programs:
  - waveform switches;
  - pitch drops;
  - noise bursts;
  - hard restarts;
  - PWM movement;
  - ADSR re-arming;
  - filter-route changes.
- **8 drum classes:** kick, snare, closed hat, open hat, clap, cowbell, tom, rim, on three
  SID voices (kick; snare/clap/rim; hats/cowbell/tom).
- **Machine models:**
  - *SID Authentic*: register-program drums.
  - *Analog X0X-8*: SID core plus an analog overlay and drive.
- **Controls:**
  - Enable, Volume, Accent, Output Drive;
  - Kick Tune/Decay; Snare Tone/Snap; Hat Tune/Decay/Metal; Clap Decay/Spread;
    Tom Tune/Decay; Cowbell Tune/Decay;
  - a drum-bus filter and limiter.
- **Performance:** pitch bend (±24), mod wheel and pressure shape the accent.
- **Choke groups:** closed hat chokes open hat; clap, snare and hat can share the noise
  voice; toms choke toms.
- **Kit libraries:** import and export DrSID kits as `.arpsidbank` / `.json`.

## 13. SID-808

- **Engine.** A dedicated x0x analog-projection engine on its own 8580. The voices are
  fixed per family: kick and tom on voice 0; snare, clap and rim on voice 1; hats and
  cowbell on voice 2.
- **Sound design** with staged micro-programs:
  - the kick pitch drop through punch, body and tail;
  - tom pitch movement;
  - the hat ring and tail;
  - the clap burst train;
  - two alternating cowbell partials.
- **Factory kits.** 30 kits: Classic, Punch, Lo-Fi, Hard and Wide, each A–F.
- **Pads.** Twelve voice pads with their GM notes, and analog tune/decay/snap/noise/
  cutoff/resonance controls.
- **Dynamics.** Accent from velocity: louder, with a tighter decay.
- **GM percussion projection.** 33 GM percussion profiles (bongos, congas, timbales,
  cymbals, agogos, claves, wood blocks, guiro, cuica, whistles, triangle, and more) each get
  their own pitch ratio.

## 14. KIT editor and GM drums

- **Classes.** Nine drum classes (kick, snare, closed hat, open hat, clap, rim, tom,
  cowbell, crash).
- **Pattern.** 32 steps per class with soft steps and accents.
- **Per class:**
  - engine target: DrSID, SID-808 or DIGI;
  - factory sound per engine;
  - SID-808 voice override: waveform, ring, sync, filter, ADSR nibbles, 12-bit pulse width;
  - DIGI assignment: sample, tune, start, length, loop, reverse.
- **General MIDI drums.** Channel-10 notes 35–81 map to the kit's engines (Auto GM Drum
  Promotion), in every flavor that allows drums. Each GM note has its own velocity, tune and
  decay scaling.
- **Stem mixing.** Additive (all engines), replace with SID-808, or selected stems only.

## 15. DIGI sampler

- **Slots.** 8 slots, each with:
  - source: empty, one of 30 factory DIGI sounds, or a user sample;
  - tune ±12 semitones, start, length and volume;
  - loop and reverse;
  - a 32-step pattern.
- **Authentic playback.** Samples play as **4-bit writes to the SID volume register
  `$D418`**, the way C64 digis are made.
  - The default rate is 8 kHz; the runtime rate is 1–32 kHz.
  - The high nibble (filter mode, 3OFF) is preserved.
- **Two routes:**
  - `AUTH C64-BUS D418`: PHI2-timed writes on the emulated C64 bus. It honours I/O banking
    and drives the open-bus latch; blocked writes are counted.
  - `FAST PRIVATE D418`: a private register engine.

  Both use an isolated SID, so samples never disturb the main SID or a playing tune.
- **Import:** WAV (PCM 8/16/24/32-bit, float 32/64-bit, EXTENSIBLE) on every platform; any
  Core Audio format on macOS.
  - A sample is resampled to 8 kHz and quantized to 4 bits.
  - Up to 60 000 frames (7.5 s).
- **Record:** from a macOS input device (Mac editor) or from the VST3 `DIGI Capture In`
  side-chain on every platform. Takes are normalised and can be trimmed.
- **Export** (Mac editor): `.d418` nibble stream, raw, WAV, or C64 assembler (`.asm`/`.s`).
- **Pads:** MIDI root note (consecutive notes), channel (1–16 or omni), velocity presets,
  audition.
- **Telemetry:** writes accepted or blocked, the last nibble and `$D418` value, triggers,
  voices, collisions, and a 64-event bus log.

## 16. C64 tune player

- **Files.** Plays **PSID and RSID** `.sid` files (versions 1–4, up to 1 MB) with subtune
  stepping.
- **Cycle-exact φ2 machine:**
  - NMOS 6510 (all official opcodes and the stable illegal ones);
  - VIC-II with raster, badlines, BA/AEC and IRQ; PAL 63 × 312, NTSC 65 × 263;
  - two CIAs (timers, TOD, ICR);
  - PLA banking;
  - 64 KiB RAM and colour RAM;
  - open bus;
  - a SID bridge with readback;
  - up to 5 SIDs.
- **Controls:** BOOT, START, STOP, RESET, EJECT, and VIC-II and CPU fast paths.
- **Play model:**
  - PSID init and play run on the φ2 machine with the correct speed (VBI or CIA).
  - RSID runs physical frames from the reset vector.
- **Rollback-safe render transactions.** A play call that would overrun is rolled back
  atomically.
- **Exactness reporting.** The player reports strict-RSID versus compatible execution
  honestly and lists every downgrade.
- **Live inspection:** registers, disassembly, chip states, bus lanes, memory pages.
- **Saved with the project.** The VST3 saves the loaded tune and its subtune with the
  project.
- **ROMs.** No ROMs are bundled. RSID needs your own KERNAL, BASIC and CHARGEN dumps.

## 17. Forensic analog model

These are optional imperfections of real hardware, off by default. They are on the FORENSIC
tab, with a compact mirror in OPTIONS.

- a global **enable** and **intensity**;
- die **temperature** 20–60 °C (with a junction/case/ambient thermal network), **supply
  voltage** 4.5–5.5 V, **revision**, 32-bit **chip seed** (every chip instance differs like
  a real part);
- **clock jitter**, **supply ripple**, **thermal drift**, **voice crosstalk** and
  **external bleed**, each with an enable and an amount;
- **envelope TDM**, **`$D418` asymmetry**, **filter ohmic**, **system noise**,
  **motherboard**, **ADC bleed**, **bus collision** and **POT input**;
- **startup randomization** and **8580 digifix**;
- **frozen mode**: all stochastic terms off, for bit-reproducible renders;
- live readouts of every term.

## 18. Mixer, effects and output

- **MIX tab:**
  - 16 channel strips: enable, solo, mute, volume (−inf to +6 dB), pan, delay send and
    reverb send;
  - **five insert slots** per strip, 8 parameters each:

    | FX | Parameters |
    |---|---|
    | 3-band EQ | low shelf, mid bell with Q, high shelf; ±12 dB |
    | Transient shaper | attack gain, sustain gain; ±12 dB |
    | Compressor | threshold, ratio up to 20:1, attack, release, make-up |
    | Saturator | drive up to +24 dB; tape, tube and transistor characters |
    | Bitcrusher | 4–16 bits, up to 16× sample hold |

  - 2 send buses (delay, reverb), each with return level and 2 FX slots;
  - master: volume, stereo width 0–200 %, limiter (threshold, release), Dim −10 dB.
- **Output limiter:** threshold, attack 0–20 ms, release 10–1000 ms.
- **Reverb:** mix.
- **Sample-accurate post effects.** Every post-effect parameter change takes effect at its
  exact sample.

## 19. Hi-Fi Transcendence

- **What it is.** An optional enhancement chain after the SID. The SID itself is never
  altered.
- **Quality levels:**
  - `PURE`: exact bypass;
  - `HI-FI`: transparent;
  - `TRANSCENDENCE`: the full chain.
- **Controls:** super-hires oversampling (4×, 8×, 16×), width, stereo depth, voice diffuser,
  analog warmth, tape saturation, psycho-exciter, eight presets. The chain also includes
  per-voice diffusion, dynamic resonance enhancement and cabinet modelling.
- **Delta monitor.** Lets you hear only what the chain adds.
- **Guarantees:**
  - silence stays silent;
  - parameters are de-zippered sample by sample;
  - output is always finite and bounded;
  - mono hosts are handled safely.

## 20. Presets, banks and files

- **180 factory slots:**
  - `001–080`: melodic patches in General MIDI order, from Acoustic Grand Piano to Ocarina,
    each a SID voice rather than a sample;
  - `081–120`: DrSID kits;
  - `121–150`: SID-808 kits;
  - `151–180`: DIGI 4-bit kits.
- **Host access.** A VST3 program list with program change, and AU factory presets.
  Selecting a slot loads it on the next block.
- **Files:**
  - `.arpsid` (one patch), `.arpsidbank` (a bank);
  - JSON patch, bank and C64 exports;
  - DrSID kit libraries;
  - `.sid` tunes;
  - WAV and audio for DIGI;
  - DIGI exports (`.d418`, raw, WAV, `.asm`/`.s`).
- **BANK tab:** factory and user banks, next/previous, save, import, export, reset, random
  patch.

## 21. MIDI

| Input | Effect |
|---|---|
| Notes, all 16 channels | play; velocity and note ID kept |
| Channel 10 notes 35–81 | GM drums to DrSID / SID-808 / DIGI per the kit |
| Pitch bend (14-bit), channel aftertouch, poly pressure | per channel |
| CC1 mod wheel, CC2 breath, CC4 foot, CC11 expression | per channel (breath also drives cutoff) |
| CC7 | master volume |
| CC64 sustain, CC66 sostenuto | per channel |
| RPN / NRPN / data entry (MSB + LSB) | per channel; RPN 0 sets the bend range |
| CC70–77 | Cutoff, Resonance, Drive, Env Amount, LFO Amount, Master Volume, Reverb Mix, Forensic Intensity (for example AKAI MPK mini K1–K8) |
| CC120 / CC123 | all sound off / all notes off, channel-scoped |
| Velocity 0 note-on | note-off |
| DIGI pad notes | from the pad root note on the pad channel |

The same mapping applies in AU, VST3 (`IMidiMapping`) and the Standalone app.

## 22. Host integration

- **Automation.** Sample-accurate automation of the 289 automatable parameters.
- **Host text.** Units (dB, Hz, ms, s, cents, semitones, BPM, °C, V) and named choices;
  typed values, names included, are parsed back.
- **Transport.** Host tempo, play state, position and loop drive the arpeggiator, sequencer
  and C64 timing.
- **AU:**
  - five flavors;
  - factory presets;
  - full state;
  - strict `auval` with no warnings;
  - glitch-free host preset apply;
  - an editor that survives host window close/reopen;
  - deferred build in Logic's out-of-process mode;
  - a fixed 5 ms latency report.
- **VST3:**
  - soft host **bypass** (10 ms fade, saved);
  - **32-bit and 64-bit** processing;
  - **units** (one per tab, plus host MIDI groups);
  - the **program list**;
  - `IMidiMapping`;
  - `IInfoListener` (track name and colour shown in the editor);
  - a host **right-click parameter menu** (automation, MIDI learn);
  - the **DIGI Capture In** side-chain;
  - editor size and tab saved in the controller state;
  - passes the Steinberg validator.

## 23. Editors

- **17 tabs:** MAIN, LFO/ARP, SID REG, SEQ, DRSID, FILTER, MACRO, FORENSIC, SIDCORE, C64,
  HI-FI, BANK, OPTIONS, SETTINGS, MIX, KIT, DIGI.
- **macOS** (AppKit, Core Animation, Metal):
  - fixed 1280 × 752;
  - header with chip/patch menu, render mode and voice mode;
  - knobs with value text, automation glow and modulation arcs;
  - scroll wheel, keyboard and reset gestures;
  - ⌘1–⌘0 tabs, ⌘↑/⌘↓ octave;
  - flavor-specific tab names.
- **Windows / Linux** (VSTGUI):
  - resizable 0.5×–3× with a 3:2 aspect, HiDPI;
  - header patch browser, status, track name and meters;
  - computer-keyboard notes (A–L, W E T Y U O, Z/X octave);
  - the host parameter menu;
  - double-click reset.
- **Themes:** Dark, Light, C64 Classic, High Contrast.
- **Languages:** English, Norsk, Deutsch, Français, 日本語.
- **On-screen keyboard** that lights every note the engine plays.

## 24. Standalone app

- **Runs without a DAW:**
  - audio output;
  - CoreMIDI input (a device or omni, a channel or omni, Reconnect All Sources ⇧⌘R);
  - on-screen keyboard;
  - transport and tempo.
- **Menus:** Panic ⌘P, Next/Previous Preset ⌘] / ⌘[, Save User Preset ⌘S, Export ⌘E,
  Import ⌘I, Reset All Parameters ⌘0, Random Patch ⌘R, tabs ⌘1–⌘9 and the View menu,
  full screen.

## 25. State and recall

- **What is saved.** Every parameter and the engine state go in one canonical state root.
  Wrappers add the GUI models that are not parameters: SETTINGS, MIX, KIT, and the DIGI
  model with its sample bank (restored as a pair).
- **VST3 extras.** The VST3 also saves the loaded `.sid` tune and subtune, the pure-SID
  output mode, the DIGI runtime policy and bypass. It uses tagged chunks that older versions
  skip safely; every earlier project version still loads.
- **When it applies.** Presets and project states are applied through an ownership mailbox
  at the start of the next block, never mid-block.

## 26. Telemetry and visualisation

- **Per-block snapshot:**
  - output levels;
  - active voices;
  - transport;
  - the SID register image;
  - voice tokens;
  - LFO values;
  - drum and DIGI meters;
  - forensic terms;
  - Hi-Fi dry/wet/delta peaks and mono correlation;
  - limiter and overflow counters.
- **Scopes:** output, filter in/out, each VCO, each voice, DIGI and the C64 bus.
- **C64 machine snapshot:** CPU, VIC-II, CIAs, memory and bus.
- **Cost.** Editors only read snapshots. Heavy parts are copied only while a visible view
  needs them.

## 27. Reliability and realtime safety

- **Realtime rules.** The render path is lock-free and allocation-free. File parsing, state
  building, sample conversion and kit compilation run off the audio thread.
- **One event authority.** A single event ordering authority runs from host input to the
  chip, and host sample offsets are never collapsed.
- **Block handling.** Blocks longer than 4096 frames are split without moving events.
  Blocks without an output still advance the engine.
- **Input sanitizing.** Invalid host values (sample rate, block size, parameter values,
  NaN, transport) are sanitized. A NaN guard and a clamp protect the output.
- **Overflow policy.** Every queue has a release-critical reserve and counted, observable
  overflow, so a note-off is never dropped silently.
- **Determinism.** Random elements (arp, sequencer probability, LFO S&H, forensic noise) are
  seeded per instance and from song position, so offline bounces reproduce.
