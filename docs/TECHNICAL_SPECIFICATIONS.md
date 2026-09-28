# ArpSID technical specifications

This page lists ArpSID's numbers and limits in one place. Each value is taken from the
source. For how the pieces work, see the internals references:

- [SID_CHIP.md](internals/SID_CHIP.md)
- [C64_MACHINE.md](internals/C64_MACHINE.md)
- [RUNTIME.md](internals/RUNTIME.md)
- [ENGINES.md](internals/ENGINES.md)

The version described here is 0.9.10 (`VERSION.txt`).

---

## 1. Products, formats and platforms

| Format | Identity | Platforms | Minimum OS |
|---|---|---|---|
| AUv2 | type `aumu`, manufacturer `ASID`; subtypes `ArpS` (ArpSID), `ArIn` (Instrument), `DrSD` (DrSID), `S808` (SID-808), `C64P` (C64 SID Player) | macOS universal (arm64 + x86_64) | macOS 12.0 |
| AUv3 | app extension `arpsid_auv3.appex` inside `ArpSID.app` | macOS universal | macOS 12.0 |
| VST3 | processor FUID `A1B2C3D4-E5F60718-9A0B1C2D-3E4F5A6B`, controller FUID `B2C3D4E5-F6071829-A0B1C2D3-E4F5A6B7`, category `Instrument\|Synth` | macOS universal; Windows x64 and arm64; Linux x86_64 and aarch64 | macOS 12.0; Windows 10/11; any glibc Linux with XCB, Cairo and Pango |
| Standalone | `ArpSID Standalone.app` | macOS universal | macOS 12.0 |

**Build requirements:**

- C++17 and CMake ≥ 3.20.
- Steinberg VST3 SDK, pinned to `v3.8.1_build_84`.
- On Windows, MSVC with the static C runtime; no Visual C++ Redistributable is needed.
- On Linux, GCC or Clang.

There are 435 registered CTest tests. The Steinberg validator runs 47 tests (537 in
extended mode). Strict `auval` passes on all five AU flavors.

---

## 2. Audio I/O

| Item | Value |
|---|---|
| Output | one stereo main bus (mono accepted) |
| Input (VST3) | `DIGI Capture In`, an auxiliary stereo/mono side-chain bus, inactive by default |
| MIDI | 1 event bus, 16 channels |
| Sample formats | 32-bit float (engine), 64-bit float (VST3; converted at the edge through float scratch) |
| Host sample rate | any finite rate > 1 Hz is accepted. Host-cycle mapping clamps to 1–384 000 Hz; DrSID clamps to 1–384 000 Hz; DIGI engines accept 8 000–384 000 Hz. |
| Host block size | any. Blocks over **4096** frames (`kMaxFramesPerBlock`) are split into chunks, with events and transport sliced per chunk. |
| Reported latency | AUv2/AUv3: a fixed 5 ms (`latency = 0.005` s, independent of sample rate), matching the 5 ms look-ahead limiter. VST3: 0 samples (the SDK default). |
| Tail | infinite (VST3 `kInfiniteTail`) |
| Bypass (VST3) | parameter id 1024, `kIsBypass`, 10 ms crossfade, engine keeps running, saved (`BYPS`) |
| Output safety | per-sample NaN/Inf guard and ±1.0 clamp; TPDF dither at 2⁻²³ (24-bit LSB) |

---

## 3. SID chip model

| Item | Value |
|---|---|
| Models | MOS 6581 (revisions R2, R3, R4AR) and MOS 8580 (R5) |
| PAL clock (φ2) | 985 248 Hz |
| NTSC clock (φ2) | 1 022 727 Hz |
| Accepted clock range | > 1 Hz and ≤ 10 MHz (`sidClockFrequencySupported`); invalid values are rejected, not replaced |
| Sub-cycle resolution | 256 subphases per SID cycle (`kSidSubcycleResolution`) |
| Oscillator | 24-bit phase accumulator; 16-bit frequency register (`f = reg × clock / 2²⁴`) |
| Frequency resolution | PAL 0.0587 Hz per step; NTSC 0.0610 Hz per step |
| Waveform DAC | 12-bit, with per-revision non-linear DAC laws |
| Pulse width | 12-bit (0–4095) |
| Noise | 23-bit LFSR, feedback bits 22 ⊕ 17 |
| Combined waveforms | TRI+SAW, TRI+PUL, SAW+PUL, TRI+SAW+PUL, from the 12-bit charge-sharing model |
| Hard sync / ring modulation | cyclic sources V1←V3, V2←V1, V3←V2 |
| Envelope | 4-bit A/D/R rate indices, 4-bit sustain; 15-bit rate counter; 8-bit envelope counter with exponential decay steps |
| Hard restart | 46-cycle gate-low window (`kSidHardRestartCycles`); register re-latch at 45 cycles |
| Filter cutoff | 11-bit (0–2047), mapped by 9 per-revision calibration anchors, for example 18–19 800 Hz on the 8580 R5 and 34–9 300 Hz on the 6581 R2 |
| Filter resonance | 4-bit |
| Filter modes | LP, BP, HP and all combinations (8 settings including OFF), per-voice routing plus EXT IN |
| Master volume | 4-bit `$D418` DAC (the DIGI channel) |
| Oversampling | 1×, 2×, 4×, 8× (Hann-weighted sub-sample combine) |
| External RC | optional C64 output-stage filter (per-revision corner 13.2–18.5 kHz) |
| Voice count | 3 per chip. BitPerfect Poly-Illusion uses 8 chips, i.e. 24 oscillators. |
| Register space | `$D400–$D41C` (29 registers); `$D400–$D418` writable, `$D419–$D41C` read-only; pseudo register `$D41D` |

---

## 4. C64 machine (tune player)

| Item | Value |
|---|---|
| CPU | NMOS 6510, cycle-exact microsequencer; all 151 official opcodes plus the stable illegal opcodes |
| VIC-II | PAL 63 cycles × 312 lines = 19 656 cycles/frame (≈ 50.12 Hz); NTSC 65 × 263 = 17 095 cycles/frame (≈ 59.83 Hz). Badlines, BA/AEC, raster IRQ. |
| CIAs | 2 × 6526: timers A/B, TOD, ICR, serial; IRQ (CIA1) and NMI (CIA2) wiring |
| Memory | 64 KiB RAM, 1 KiB colour RAM (4-bit), PLA banking through `$01`, open bus |
| SID bridge | timed writes with readback; up to 5 SIDs (PSID v3/v4 extra addresses) |
| φ2 cycle order | CIA → VIC → IRQ wiring → BA → RDY/AEC → CPU |
| PSID/RSID files | magic `PSID`/`RSID`, versions 1–4; files up to **1 MB** kept in memory (and in VST3 state) |
| Bootstrap | default page `$0334`, moved automatically if the tune overlaps it |
| Play budget | 16 384 instructions per play call; init budget `maxInstructions × 12` |
| ROMs | none bundled. RSID needs user-supplied KERNAL, BASIC and CHARGEN dumps. |
| Render transactions | an overrunning play call is rolled back (CPU, CIA/VIC, RAM, colour RAM, SID bridge and diagnostics together) |

---

## 5. Voices and note handling

| Item | Value |
|---|---|
| Poly voices (BitPerfect) | 8 (`VoiceManager::MAX_VOICES`) × 3 oscillators |
| Authentic topology | 1 chip × 3 voices; stealing policies StealOldest, StealQuietest, StealReleasingFirst, Refuse |
| Voice modes | Poly, Mono, Legato, Unison (`1 + int(Voice Spread × 7)` voices: CLASSIC 1–8, SYNTH capped to 3; detune ±24 cents × spread) |
| Held-note tracking | per-channel sustain (CC64) and sostenuto (CC66); host note IDs kept |
| Pitch bend | 14-bit, per channel, range 0–48 semitones via RPN 0 (default 2; one limit in every engine) |
| Master tune | ±100 cents |
| VCO detune | ±100 cents per oscillator |
| Portamento | 0–5 s (`n² × 5`), 4 styles (C64 SLIDE, C64 FIXED, LINEAR, SMOOTH); C64 glide delta 1–255 register units per frame (50 Hz PAL / 60 Hz NTSC) |
| Velocity law | `sqrt(velocity)` |

---

## 6. Event system and capacities

| Item | Value |
|---|---|
| Timed event queue | 4096 events (`ARPSID_RUNTIME_TIMED_EVENT_CAPACITY`), with a reserve of `max(32, capacity/16)` for release-critical events |
| MIDI ingress ring | 8192 (bounded MPSC, lock-free) |
| Parameter-intent ring | 8192 |
| C64 SID timed writes per block | 4096 |
| SID register write queue | 65 536 per queue (coalesce, then retire the oldest on overflow; counted) |
| Subphase writes per span | 256 |
| Arpeggiator timed events per call | 512 |
| Sequencer | 32 steps, 1–4 ratchets, probability, tie, accent |
| Sample accuracy | every event, automation point, arp and sequencer note carries its host sample offset. Register writes carry (cycle, subphase). |

---

## 7. Modulation

| Item | Value |
|---|---|
| LFOs | 4; 7 shapes; 0.1–20 Hz (`0.1 × 200ⁿ`), engine clamp 0.01–50 Hz; tempo sync; phase offset; retrigger; per-sample update |
| Macros | 8 |
| Mod matrix | up to 32 routes; 21 sources; 13 targets; 4 transforms (linear, squared, abs, invert); depth ±1 |
| Arpeggiator | 7 modes; 0.1–50 Hz free (`0.1 × 500ⁿ`) or host-synced; 1–4 octaves; gate 10–100 %; swing 0–75 %; transpose ±24; pattern 1–32 steps; 128 held notes |
| Sequencer tempo | 20–300 BPM internal (`20 + 280n`) or host |

---

## 8. Drums and DIGI

| Item | Value |
|---|---|
| DrSID drum classes | 8: kick, snare, closed hat, open hat, clap, cowbell, tom, rim |
| DrSID programs | ≤ 32 register steps × 16 bytes each; schema v1; CRC32 register-trace fingerprints |
| GM drum map | notes 35–81 (47 notes), MIDI channel 10 |
| SID-808 | 1 × 8580, fixed voice per family, up to 4 micro-stages per hit, 33 GM percussion profiles |
| KIT | 9 classes × 32 steps |
| DIGI slots | 8 × 32-step patterns |
| DIGI stream | 4-bit `$D418` nibbles; default 8 000 Hz, runtime 1 000–32 000 Hz |
| DIGI user sample | ≤ 60 000 frames (7.5 s at 8 kHz) per slot |
| DIGI import | WAV PCM 8/16/24/32-bit, float 32/64-bit, WAVE_FORMAT_EXTENSIBLE. On macOS, any Core Audio format. |
| DIGI export | `.d418`, raw, WAV, `.asm`/`.s` |
| DIGI forensic log | the last 64 `$D418` bus events |

---

## 9. Mixer and post-processing

| Item | Value |
|---|---|
| MIX channels | 16 strips × 5 insert slots × 8 parameters |
| Send buses | 2 (delay, reverb) × 2 FX slots |
| Insert FX | 3-band EQ (±12 dB; shelves 50–1000 Hz and 2–16 kHz; bell 200–8000 Hz, Q 0.3–8), transient shaper (±12 dB), compressor (−40–0 dB, 1:1–20:1, 0.1–100 ms, 10–500 ms, 0 to +24 dB make-up), saturator (0 to +24 dB drive, 3 characters), bitcrusher (4–16 bits, 1–16× hold) |
| Master | width 0–200 %, limiter −24–0 dB and 10–500 ms, dim −10 dB |
| Output limiter | threshold 0.5–1.0, attack 0–20 ms, release 10–1000 ms |
| Register-engine limiter | 5 ms look-ahead, ceiling 0.9885531, release time constant 50 ms |
| Hi-Fi Transcendence | 3 quality levels; oversampling 1–16; width 0.5–2.0; 5 amount controls; 8 presets |

---

## 10. Forensic analog model

| Item | Range |
|---|---|
| Die temperature | 20–60 °C (thermal network: junction 15–92 °C, case 15–68 °C, ambient 0–55 °C) |
| Supply voltage | 4.5–5.5 V; ripple 0–250 mV |
| Revision | 6581 R2, R3, R4AR, 8580 R5 |
| Chip seed | 32-bit |
| Amount terms | clock jitter, supply ripple, thermal drift, voice crosstalk, external bleed, envelope TDM, `$D418` asymmetry, filter ohmic, system noise, motherboard, ADC bleed, bus collision, POT input (each 0..1) |
| Frozen mode | every stochastic term is zeroed, so renders are bit-reproducible |

---

## 11. Parameters, presets and state

| Item | Value |
|---|---|
| Parameters | 512 (IDs 0–511); 289 automatable; 287 with an editor control |
| Parameter identity | VST3 parameter ID = AU parameter address |
| VST3 units | 32 (root, 17 tabs, host MIDI, 13 host-controller blocks) |
| Factory bank | 180 slots: 001–080 melodic (GM order), 081–120 DrSID kits, 121–150 SID-808 kits, 151–180 DIGI kits |
| Editor tabs | 17 |
| State root | `SidStateRootV1` (magic `ASR1`), binary codec, magics `ASSD` / `ASPC` / `ASPR` |
| VST3 state | version 5: `u32` version, then tagged chunks `ROOT`, `SETS` (32 B), `MIX ` (1264 B), `KIT ` (1676 B), `DIGM` (360 B), `DIGB` (480 392 B), `DIGR` (6 B), `OUTM` (1 B), `SIDF` (subtune + tune), `BYPS` (1 B). Versions 1–4 still load. A factory `.vstpreset` holds a patch-only state: `PRST` (0 B) + `ROOT`, about 5 KB, which changes only the patch. |
| VST3 controller state | magic `ASEC`, version 1, zoom `f64` (0.25–4), tab `i32` |
| Semantic JSON caps | 4096 parameters, 4096 semantic entries per import |
| File types | `.arpsid`, `.arpsidbank`, JSON patch/bank/C64 exports, DrSID kit libraries, `.sid`, WAV/audio, `.d418` |

---

## 12. Editors

| Item | macOS (Cocoa) | Windows/Linux (VSTGUI) |
|---|---|---|
| Size | fixed 1280 × 752 | 1200 × 800 base, resizable 0.5×–3× (aspect 3:2, HiDPI) |
| Rendering | AppKit + Core Animation + Metal | VSTGUI 4 (Direct2D / Cairo) |
| Themes | Dark, Light, C64 Classic, High Contrast | same set |
| Languages | English, Norsk, Deutsch, Français, 日本語 | same set |
| Keyboard | ⌘1–⌘0 tabs, ⌘↑/⌘↓ octave | A–L white keys, W E T Y U O black keys, Z/X octave |
| Scopes | 256-sample voice, oscillator and filter taps; 128-sample DIGI | same data |

---

## 13. Realtime guarantees

- The render path does no heap allocation, takes no locks, makes no system calls and does
  no file I/O.
- State, presets and models are published through ownership mailboxes and applied at the
  start of the next block. They are never applied mid-block.
- Heavy work runs off the audio thread: file parsing, state building, sample conversion
  and kit compilation.
- Every host-supplied value is sanitized before it reaches the engine: sample rate, block
  size, parameter values, NaN/Inf and transport.
- Telemetry is copied through triple buffers. Scope and C64 snapshots are copied only while
  a visible view asks for them.
