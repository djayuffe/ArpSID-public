# ArpSID 0.9.16

**A Commodore 64 SID synthesizer, drum machine, 4-bit sampler and C64 tune
player, as an audio plug-in for macOS, Windows and Linux.**

ArpSID is a large, SID-accurate instrument built around one realtime engine.
It emulates the MOS 6581 and 8580 SID chips, runs real `.sid` tunes on a
cycle-exact C64, plays drums through three drum engines, plays 4-bit samples
through the SID's `$D418` volume register, and adds an arpeggiator, a 32-step
sequencer, a mod matrix, a 16-channel mixer and post-effects. It ships as
AUv2 (five flavors), AUv3, VST3 and standalone apps for macOS, Windows and Linux, all on the same
engine, with the same 512 parameters and the same 180 factory patches.

---

## Contents

- [At a glance](#at-a-glance)
- [Screenshots](#screenshots): [macOS editor](#macos-editor-auv2-auv3-standalone-macos-vst3) ·
  [Windows / Linux editor](#windows--linux-vst3-editor) · [AU flavors](#au-flavors)
- [Installing](#installing)
- [Products and formats](#products-and-formats)
- [Features](#features)
  - [Sound engines](#sound-engines) · [SID chip model](#sid-chip-model-and-authenticity) ·
    [Voices and playing](#voices-and-playing) · [Filter](#filter) ·
    [Modulation](#modulation) · [Arpeggiator](#arpeggiator) · [Sequencer](#sequencer) ·
    [Drums](#drums-drsid-sid-808-and-kit) · [DIGI sampler](#digi-sampler) ·
    [C64 tune player](#c64-tune-player) · [Forensic model](#forensic-analog-model) ·
    [Mixer and effects](#mixer-and-effects) · [Presets, banks and files](#presets-banks-and-files) ·
    [MIDI](#midi) · [Host integration](#host-integration) · [Editors](#editors) ·
    [Standalone app](#standalone-app) · [State and recall](#state-and-recall) ·
    [Realtime engine](#realtime-engine) · [Telemetry and displays](#telemetry-and-displays)
- [Parameters](#parameters)
- [Building](#building) · [Testing](#testing) · [CI and releases](#ci-and-releases)
- [Documentation](#documentation) · [Project layout](#project-layout) ·
  [Status and limits](#status-and-validation-limits) · [Licensing](#licensing)

---

## At a glance

| | |
|---|---|
| **Formats** | AUv2 (5 flavors), AUv3, VST3, Standalone |
| **Platforms** | macOS (universal: Apple silicon + Intel), Windows x64, arm64 and x86 (32-bit hosts), Linux x86_64 and aarch64 |
| **Engines** | BitPerfect SID synth, authentic single SID, SID register synthesis, DrSID drums, SID-808 drums, DIGI `$D418` sampler, C64 PSID/RSID player |
| **SID chips** | MOS 6581 R2, R3, R4 and MOS 8580 R5; PAL and NTSC clocks |
| **Voices** | up to 24 (8 SIDs × 3) in the BitPerfect topology, or one authentic 3-voice SID |
| **Parameters** | 512 (289 automatable), identical in every format |
| **Factory patches** | 180: 80 melodic, 40 DrSID kits, 30 SID-808 kits, 30 DIGI kits |
| **Editor tabs** | 17, in both editors |
| **Tests** | about 480 CTest tests, plus the Steinberg VST3 validator (537 tests in extended mode) and strict `auval` on all five AU flavors |
| **License** | GPL-3.0-or-later (C64 ROMs not included) |

---

## Screenshots

### macOS editor (AUv2, AUv3, Standalone, macOS VST3)

Every Mac product shows the same native editor, 1280 × 752. These images are
rendered by `arpsid_au_editor_snapshot` from the installed AUv2 with the engine
running (a four-note chord held on factory patch 001). **A guide to every
tab, control and shortcut is in [docs/AU_EDITOR.md](docs/AU_EDITOR.md).**

![ArpSID macOS editor — MAIN](docs/screenshots/au-editor-main.jpg)

*MAIN — master section, the three SID voices (VCO 1–3), live filter trace, ADSR and output/FX. The header holds the chip/patch menu, render mode, voice mode, LEDs and output scope; the footer the 17 tabs, transport, keyboard and MIDI/CC status.*

| | |
|---|---|
| ![LFO / ARP](docs/screenshots/au-editor-lfo-arp.jpg) | ![SID REG](docs/screenshots/au-editor-sid-reg.jpg) |
| **LFO / ARP** — four LFOs drawn live behind their knobs; the arpeggiator with INT/HOST clock. | **SID REG** — every SID register as a colour-coded cell (click, type hex, nudge), per-voice scopes. |
| ![SEQ](docs/screenshots/au-editor-seq.jpg) | ![DRSID](docs/screenshots/au-editor-drsid.jpg) |
| **SEQ** — 16-step drum pattern pages, SID-808 voice pads, register scope and analog drum controls. | **DRSID** — the register-program drum engine: per-drum tune/decay/tone, sequencer bridge, triggers, scope. |
| ![FILTER](docs/screenshots/au-editor-filter.jpg) | ![MACRO](docs/screenshots/au-editor-macro.jpg) |
| **FILTER** — large live response over input/output spectra; cutoff, env, chip and analog controls. | **MACRO** — macro knobs and the 9-destination mod matrix. |
| ![FORENSIC](docs/screenshots/au-editor-forensic.jpg) | ![SIDCORE](docs/screenshots/au-editor-sidcore.jpg) |
| **FORENSIC** — analog imperfection model: clock, supply, crosstalk, bus; VCO scopes and bus trace. | **SIDCORE** — per-voice SID state (frequency, pulse, ADSR, gates), filter/mix, CPU/VIC/CIA and register readouts. |
| ![C64](docs/screenshots/au-editor-c64.jpg) | ![HI-FI](docs/screenshots/au-editor-hi-fi.jpg) |
| **C64** — the `.sid` player and the whole emulated machine: CPU, disassembly, VIC-II, CIAs, SID bus, raw memory. | **HI-FI** — the post-SID "Transcendence" chain: oversampling, width, warmth, tape, exciter, presets, safety. |
| ![BANK](docs/screenshots/au-editor-bank.jpg) | ![OPTIONS](docs/screenshots/au-editor-options.jpg) |
| **BANK** — all 180 factory slots by family, category filter, patch/bank/JSON load and save, drum kits. | **OPTIONS** — SID, drum, forensic and output options with the C64 control hub and PANIC. |
| ![SETTINGS](docs/screenshots/au-editor-settings.jpg) | ![MIX](docs/screenshots/au-editor-mix.jpg) |
| **SETTINGS** — engine topology, theme, language, host sync and MIDI policies. | **MIX** — 16 channel strips with sends, solo/mute and five insert FX, master and send buses. |
| ![KIT](docs/screenshots/au-editor-kit.jpg) | ![DIGI](docs/screenshots/au-editor-digi.jpg) |
| **KIT** — drum kit editor: nine drum classes, 32-step grid, engine target and per-class sound slots. | **DIGI** — 8-slot 4-bit sampler: import, record, pads, MIDI map, `$D418` route and bus telemetry. |

### Windows / Linux VST3 editor

The Windows and Linux VST3 has a resizable VSTGUI editor (1200 × 800, 0.5×–3×)
with the same 17 tabs. These images come from `arpsid_vst3_editor_check`,
which draws every tab offscreen with the engine running. **All 17 tabs are
explained in [docs/VST3_EDITOR.md](docs/VST3_EDITOR.md).**

![ArpSID VST3 editor — MAIN](docs/screenshots/vst3-editor-main.png)

*MAIN — master, three VCOs, filter with response curve, ADSR, output limiter and the output scope. The header shows the patch browser, status, host track and keyboard octave, and the output meters.*

| | |
|---|---|
| ![LFO / ARP](docs/screenshots/vst3-editor-lfo-arp.png) | ![SID REG](docs/screenshots/vst3-editor-sid-reg.png) |
| **LFO / ARP** | **SID REG** |
| ![SEQ](docs/screenshots/vst3-editor-seq.png) | ![DRSID](docs/screenshots/vst3-editor-drsid.png) |
| **SEQ** | **DRSID** |
| ![FILTER](docs/screenshots/vst3-editor-filter.png) | ![MACRO](docs/screenshots/vst3-editor-macro.png) |
| **FILTER** | **MACRO** |
| ![FORENSIC](docs/screenshots/vst3-editor-forensic.png) | ![SIDCORE](docs/screenshots/vst3-editor-sidcore.png) |
| **FORENSIC** | **SIDCORE** |
| ![C64](docs/screenshots/vst3-editor-c64.png) | ![HI-FI](docs/screenshots/vst3-editor-hi-fi.png) |
| **C64** | **HI-FI** |
| ![BANK](docs/screenshots/vst3-editor-bank.png) | ![OPTIONS](docs/screenshots/vst3-editor-options.png) |
| **BANK** | **OPTIONS** |
| ![SETTINGS](docs/screenshots/vst3-editor-settings.png) | ![MIX](docs/screenshots/vst3-editor-mix.png) |
| **SETTINGS** | **MIX** |
| ![KIT](docs/screenshots/vst3-editor-kit.png) | ![DIGI](docs/screenshots/vst3-editor-digi.png) |
| **KIT** | **DIGI** |

### AU flavors

The AUv2 component installs five instruments. Each opens on its own patch range
and names some tabs for its job ([details](docs/AU_EDITOR.md#au-flavors)).

| | | |
|---|---|---|
| ![Classic](docs/screenshots/au-flavor-classic.jpg) | ![Instrument](docs/screenshots/au-flavor-instrument.jpg) | ![Drum Machine](docs/screenshots/au-flavor-drum-machine.jpg) |
| **ArpSID** (`ArpS`) — everything | **ArpSID Instrument** (`ArIn`) — melodic synth | **DrSID** (`DrSD`) — drum machine |
| ![SID-808](docs/screenshots/au-flavor-sid-808.jpg) | ![C64 SID Player](docs/screenshots/au-flavor-c64-sid-player.jpg) | |
| **SID-808** (`S808`) — analog-style kits | **C64 SID Player** (`C64P`) — tune player | |

Regenerate the images with `scripts/update_au_screenshots.sh` (Mac) and
`scripts/update_editor_screenshots.sh build-vst3` (Linux/Windows), or run the
**Screenshots** workflow for the Mac ones.

---

## Installing

Download a release, or install it with one command. The command downloads
the build for your machine, verifies it against the release checksums and
runs the installer.

```bash
curl -fsSL https://raw.githubusercontent.com/djayuffe/ArpSID-public/main/scripts/install/get_arpsid.sh | bash   # Linux, macOS
```
```powershell
irm https://raw.githubusercontent.com/djayuffe/ArpSID-public/main/scripts/install/get_arpsid.ps1 | iex        # Windows
```

Every release zip also contains its installer (`install_macos.sh`,
`install.ps1` or `install.sh`) and an `INSTALL.txt`. The macOS installer clears
the download quarantine, registers the AUv3 and refreshes the Audio Unit cache;
the Windows installer unblocks the files and installs for all users or just
you; the Linux installer checks the shared libraries. [docs/INSTALL.md](docs/INSTALL.md)
covers each platform, the installer options, installing by hand,
uninstalling and troubleshooting.

---

## Products and formats

| Format | Platforms | Bundle | Notes |
|---|---|---|---|
| **AUv2** (`aumu`, manufacturer `ASID`) | macOS universal | `ArpSID.component` | Five flavors: `ArpS` ArpSID, `ArIn` ArpSID Instrument, `DrSD` DrSID, `S808` SID-808, `C64P` C64 SID Player. Strict `auval` passes on all five with no warnings. |
| **AUv3** | macOS universal | `ArpSID.app` (extension `arpsid_auv3.appex`) | Logic-compatible app extension; the app registers it. |
| **VST3** | macOS universal, Windows x64 + arm64 + x86 (32-bit), Linux x86_64 + aarch64 | `arpsid_vst3.vst3` | One instrument, category `Instrument\|Synth`. Native Cocoa editor on macOS, VSTGUI editor on Windows/Linux. Static C runtime on Windows (no Visual C++ Redistributable needed). |
| **Standalone** | macOS universal | `ArpSID Standalone.app` | Its own audio and MIDI I/O, menus, keyboard and transport. |
| **Standalone** | Windows x64 + arm64 + x86, Linux x86_64 + aarch64 | `ArpSID.exe` / `ArpSID` | The VST3's editor in its own window with audio, MIDI and clock controls (WASAPI/DirectSound; PulseAudio/ALSA). See [STANDALONE_APP.md](docs/STANDALONE_APP.md). |

Every [release](https://github.com/djayuffe/ArpSID-public/releases) ships each
of these as its own zip, plus the source and `SHA256SUMS.txt`, built and
validated by CI. The Mac bundles are ad-hoc signed (not notarized).

All formats run one engine, `ArpSIDDSPKernel`. The AU, AUv3, Standalone and
VST3 wrappers only adapt host events, state and the editor to it, so a sound
made in one format plays the same in the others.

---

## Features

### Sound engines

| Engine | What it is |
|---|---|
| **BitPerfect SID synth** (`CLASSIC`) | The main synthesizer. By default the "Poly-Illusion" topology: 8 SID chips × 3 voices, so up to 24 voices of polyphony, each voice a real SID oscillator with SID envelopes and filter. |
| **Authentic single SID** | One 3-voice SID, like a real C64 (SETTINGS → Topology). Voice stealing, filter sharing and envelope quirks behave as on the hardware. |
| **SID register synthesis** (`SYNTH / SID REG`) | Play by writing `$D400–$D41C` directly: the register image you edit is what the chip plays. Canonical voice tokens keep host note IDs; replayed and anonymous notes get stable synthetic identities. |
| **DrSID** | Drums as per-hit register micro-programs on the SID. Two machine models: the SID-authentic drum core and the Analog X0X-8. Kick, snare, closed and open hat, clap, cowbell, tom, rim. |
| **SID-808** | An x0x-style analog projection: staged kick pitch drops, tom pitch movement, hat ring and tail, clap burst trains, cowbell partials. |
| **DIGI sampler** | 8 sample slots played as 4-bit `$D418` volume writes, the way C64 digis are made. |
| **C64 tune player** | A cycle-exact C64 that runs PSID and RSID `.sid` files. |

The render mode (`CLASSIC`, `SYNTH / SID REG`, `DR SID`) and the voice mode
are chosen in the editor header or by parameter; the AU flavors start in the
mode that fits them.

### SID chip model and authenticity

- **Chip revisions**: MOS 6581 R2, R3, R4 and MOS 8580 R5, each with its own
  filter curve, combined-waveform behaviour and envelope timing.
- **Clock**: PAL (985 248 Hz) and NTSC, switched on sample boundaries.
- **Waveforms**: triangle, sawtooth, variable pulse, noise and the combined
  waveforms (`TRI+SAW`, `TRI+PUL`, `SAW+PUL`, `TRI+SAW+PUL`).
- **Oscillator features**: pulse width and PWM, hard sync and ring modulation,
  LF mode (oscillators at LFO rates).
- **Envelope**: SID ADSR rates, with the optional **6581 ADSR bug** (the
  envelope counter delay real chips have).
- **Fractional-cycle rendering**: register writes land at their exact cycle,
  not at block edges.
- **External RC filter** of the C64 output stage, optional **SID
  oversampling**, **8580 Digifix** (the `$D418` digi boost trick on 8580s),
  **Startup Random** (random chip state at power-on).
- **Pure SID 1Q1 output mode**: bypasses everything after the chip.

### Voices and playing

- **Voice modes**: `POLY`, `MONO`, `LEGATO` (glide without retrigger) and
  `UNISON` (stacked voices, spread by Voice Spread).
- **Portamento**: 0–5 s with four styles, `C64 SLIDE`, `C64 FIXED`, `LINEAR`
  and `SMOOTH`, and a C64 Glide Delta (register step per frame) for the C64
  styles. Glides are scheduled as SID register writes.
- **Master Tune** ±100 cents, **Master Volume**, per-voice **Detune** and
  **Level**.
- **Stuck-note safety net**: gated voices are reconciled with the held keys,
  with a two-block grace period.
- **Panic** and **All Notes Off** (channel-scoped, as the MIDI spec requires).

### Filter

- The SID multimode filter: `OFF`, `LOW-PASS`, `BAND-PASS`, `LP+BP`,
  `HIGH-PASS`, `NOTCH`, `BP+HP`, `ALL`, with per-voice routing.
- **Cutoff**, **Resonance**, **Drive**, **KeyTrack**, **Env Amount** and **LFO
  Amount**.
- **Filter Ohmic**: the resistor behaviour of the real filter in the forensic
  model. 6581 and 8580 filters differ as on hardware.
- Live response curves and input/output scopes in both editors.

### Modulation

- **4 LFOs**: Rate 0.1–20 Hz (exponential), Depth, Shape (`SINE`, `TRIANGLE`,
  `SAW`, `RAMP DOWN`, `SQUARE`, `S&H`, `RANDOM`) and host-tempo Sync.
- **8 macros**, for host automation and the matrix.
- **Mod matrix**: 9 destinations (VCF cutoff and resonance, VCO 1–3 frequency
  and pulse width, volume) × 21 sources (LFO 1–4, velocity, note, key follow,
  mod wheel, pitch bend, aftertouch, poly pressure, random, Macro 1–8,
  envelope), each with a depth.
- **PWM depth** per voice from the LFOs.
- Knobs show the live modulated value in the Mac editor; the VST3 editor shows
  live source meters.

### Arpeggiator

- Modes `UP`, `DOWN`, `UP/DOWN`, `DOWN/UP`, `RANDOM`, `PATTERN`, `CHORD`.
- Rate (free or host-synced), 1–4 octaves, gate length, swing, hold, latch,
  transpose ±24 semitones, random, pattern length 1–32, glide legato.
- Follows the host tempo and position when the host plays; an internal clock
  otherwise.
- Arpeggiated notes are written into the engine's event queue at exact sample
  offsets, so they stay tight at any block size.

### Sequencer

- 32 steps, each with note, velocity and gate.
- Modes `FORWARD`, `REVERSE`, `PING-PONG`, `RANDOM`; length 1–32; swing;
  internal tempo 20–300 BPM or host-following.
- Its step boundaries drive melodic notes, the KIT drum pattern and the DIGI
  pattern together, so all layers stay locked.
- Drum pattern pages (16 × 16 steps) on the Mac SEQ page, with STAMP and CLEAR.

### Drums: DrSID, SID-808 and KIT

- **DrSID**: Enable, machine model, volume, accent, drive; Kick Tune/Decay,
  Snare Tone/Snap, Hat Tune/Decay/Metal, Clap Decay/Spread, Tom Tune/Decay,
  Cowbell Tune/Decay; filter and limiter for the drum bus.
- **SID-808**: 30 factory kits (Classic, Punch, Lo-Fi, Hard, Wide × A–F),
  twelve voice pads with their GM notes, analog tune/decay/snap/noise/cutoff/
  resonance controls.
- **General-MIDI drums**: channel-10 notes reach DrSID, SID-808 or DIGI per the
  kit (Auto GM Drum Promotion), in every flavor that allows drums.
- **KIT editor**: nine drum classes (kick, snare, closed hat, open hat, clap,
  rim, tom, cowbell, crash) × 32 steps with soft steps and accents; the engine
  target (DrSID, SID-808, DIGI); the factory sound per class and engine; a
  per-class SID-808 voice override (waveform, ring, sync, filter, ADSR
  nibbles, 12-bit pulse width) and DIGI assignment (tune, start, length,
  sample, loop, reverse).
- **Drum kit files**: DrSID kits import and export as `.arpsidbank` / `.json`
  drum libraries.

### DIGI sampler

- **8 slots**, each with source (empty, one of 30 factory DIGI sounds, or a
  user sample), tune ±12 semitones, start, length, volume, loop and reverse,
  and a 32-step pattern.
- **Import** WAV (PCM 8/16/24/32-bit, float 32/64-bit, WAVE_FORMAT_EXTENSIBLE)
  on every platform, and any audio format macOS reads in the Mac editor.
- **Export** a slot from the Mac editor as a `.d418` nibble stream, raw data,
  WAV, or assembler source (`.asm` / `.s`) for use in a C64 program.
- **Record**: from a macOS input device in the Mac editor, or from the VST3's
  `DIGI Capture In` side-chain bus on every platform. Takes are normalised and
  can be trimmed.
- Every sample becomes the canonical **8 kHz 4-bit `$D418` stream**, up to
  60 000 frames (7.5 s); see [D418_NIBBLE_SPEC.md](docs/D418_NIBBLE_SPEC.md).
- **Two playback routes**: `AUTH C64-BUS D418` (writes on the emulated C64
  bus, PHI2-timed, honouring I/O banking and open bus) and `FAST PRIVATE
  D418` (a private register engine). Both use an isolated SID, so samples never
  disturb the main SID or a playing tune.
- **Pads** mapped to MIDI: a root note (pads are consecutive notes) and a
  channel (1–16 or omni), velocity presets, audition.
- **Bus telemetry**: writes accepted or blocked, the last nibble, triggers,
  voices.

### C64 tune player

- Loads **PSID and RSID** `.sid` files (up to 1 MB), with subtune stepping.
- A **cycle-exact PHI2 machine**: NMOS 6510 CPU (all 151 official opcodes and
  the stable illegal ones), VIC-II
  (raster, badlines, BA, IRQ), two CIAs (timers, TOD, ICR), the PLA and memory
  matrix, open bus, a SID bridge with readback.
- **BOOT, START, STOP, RESET, EJECT**, VIC-II and 6510 fast paths.
- **Rollback-safe render transactions**: a play call that would overrun rolls
  back CPU, CIA/VIC, RAM, colour RAM, the SID bridge and diagnostics together.
- Honest reporting of **strict RSID vs. compatible** execution and every
  downgrade.
- The whole machine is visible live: registers, disassembly, chip states, bus
  lanes, raw memory pages.
- In the VST3, the loaded tune and its subtune are **saved with the project**.
- RSID tunes need your own KERNAL, BASIC and CHARGEN ROM dumps; none are
  bundled. See [C64_EXACTNESS_BOUNDARIES.md](docs/C64_EXACTNESS_BOUNDARIES.md).

### Forensic analog model

Optional imperfections of real C64 hardware, off by default:

- clock jitter, supply ripple, thermal drift (each with enable and amount);
- chip temperature (°C), supply voltage, a per-chip variation seed;
- voice crosstalk and external bleed (enable and amount), envelope TDM;
- filter ohmic behaviour, `$D418` asymmetry, system noise, motherboard, ADC
  bleed, bus collision, POT input;
- a global intensity, and live readouts of every term.

### Mixer and effects

- **MIX**: 16 channel strips with volume, pan, delay and reverb sends, mute,
  solo, enable, and **five insert slots** each (3-band EQ, transient shaper,
  compressor, saturator, bitcrusher, eight parameters per slot).
- **Master**: volume, stereo width, limiter (threshold, release), Dim −10 dB;
  delay and reverb buses with return levels.
- **Output limiter** (threshold, attack, release) and **reverb**.
- **Hi-Fi Transcendence**: Quality `PURE` / `HI-FI` / `TRANSCENDENCE`,
  super-hires oversampling 4× / 8× / 16×, width, depth, voice diffuser,
  analog warmth, tape saturation, psycho-exciter, eight presets, and a delta
  monitor. The SID itself is never altered.
- Every post-effect parameter change applies on the exact sample timeline: a
  value that arrives at sample N never affects samples before N.

### Presets, banks and files

- **180 factory slots**: `001–080` melodic patches in General-MIDI order (from
  Acoustic Grand Piano to Ocarina, each a SID voice, not a sample), `081–120`
  DrSID kits, `121–150` SID-808 kits, `151–180` DIGI 4-bit kits.
- Hosts see them as a **program list** (VST3 `kIsProgramChange` Program
  parameter, AU factory presets); program changes and the editors load them.
- **Files**: `.arpsid` (one patch), `.arpsidbank` (a bank), JSON patch, bank
  and C64 exports, DrSID kit libraries (`.arpsidbank` / `.json`), `.sid` tunes,
  WAV and other audio files for DIGI, and DIGI exports as `.d418`, raw, WAV
  or `.asm`/`.s` source.
- **User bank** view in BANK; Standalone menu commands for next/previous,
  save, import, export, reset and random patch.

### MIDI

| MIDI | Effect |
|---|---|
| Notes on all 16 channels | Play; velocity and note ID are kept. |
| Channel 10 (GM drums) | DrSID / SID-808 / DIGI per the kit (Auto GM Drum Promotion). |
| Pitch bend, channel aftertouch, poly pressure | Per-channel host controllers, modulation sources. |
| CC1 mod wheel, CC2 breath | Per-channel controllers; breath also drives filter cutoff. |
| CC4 foot, CC11 expression | Expression, per channel. |
| CC7 volume | Master volume. |
| CC64 sustain, CC66 sostenuto | Per channel. |
| RPN / NRPN / data entry | Received per channel (MSB and LSB). |
| CC70–77 (e.g. AKAI MPK mini K1–K8) | Cutoff, Resonance, Drive, Env Amount, LFO Amount, Master Volume, Reverb Mix, Forensic Intensity. |
| CC120 / CC123 | All sound off / all notes off (channel-scoped). |
| DIGI pad notes | From the pad root note on the pad channel. |

The same mapping applies in AU, VST3 (`IMidiMapping`) and the Standalone app.

### Host integration

- **Same parameters everywhere**: VST3 parameter ID = AU parameter address.
  Host text comes from one presentation service, so displayed values match the
  sound: units (dB, Hz, ms, s, cents, semitones, BPM, °C, V) and named choices
  (`SAW`, `LOW-PASS`, `PING-PONG`, …). Typed values are parsed back, names
  included.
- **Sample-accurate automation** of every automatable parameter.
- **Host tempo, transport, position, loop** drive the arpeggiator, sequencer
  and C64 timing.
- **AU**: five flavors, factory presets, full state, strict `auval`, host
  preset apply without glitches, reusable editor across host window
  open/close, deferred build in Logic's out-of-process mode.
- **VST3**:
  - host **bypass** (`kIsBypass`, a 10 ms fade while the engine keeps running,
    saved with the project);
  - **32-bit and 64-bit** processing;
  - parameters grouped into **units** (one per tab, plus host MIDI groups);
  - the factory **program list** and program changes;
  - `IMidiMapping`, `IInfoListener` (track name and colour in the editor);
  - a **right-click parameter menu** from the host (automation, MIDI learn);
  - an optional **side-chain input** for DIGI recording;
  - the editor size and tab saved in the controller state;
  - passes the Steinberg validator (47 tests; 537 in extended mode).

### Editors

- **17 tabs**: MAIN, LFO/ARP, SID REG, SEQ, DRSID, FILTER, MACRO, FORENSIC,
  SIDCORE, C64, HI-FI, BANK, OPTIONS, SETTINGS, MIX, KIT, DIGI.
- **macOS** (AppKit, Core Animation, Metal): fixed 1280 × 752; chip/patch menu,
  render mode and voice mode in the header; knobs with value text, automation
  glow and modulation arcs; scroll-wheel, keyboard and reset gestures;
  ⌘1–⌘0 tab shortcuts and ⌘↑/⌘↓ octave; flavor-specific tab names.
- **Windows / Linux** (VSTGUI): resizable 0.5×–3× with HiDPI; header patch
  browser, status, track name, meters; computer-keyboard notes (A–L, W E T
  Y U O, Z/X octave); right-click host parameter menu; double-click reset.
- **Themes**: Dark, Light, C64 Classic, High Contrast. **Languages**: English,
  Norsk, Deutsch, Français, 日本語 (SETTINGS strings).
- **On-screen keyboard** in both editors, lighting every note the engine plays.
- Guides: [AU_EDITOR.md](docs/AU_EDITOR.md) and [VST3_EDITOR.md](docs/VST3_EDITOR.md).

### Standalone app

- **Windows and Linux** (`ArpSID.exe`, `ArpSID`): the full editor in a
  resizable window with a bar for the audio output, sample rate, buffer,
  capture input, MIDI input and channel, tempo, play/stop and panic; MIDI
  program change and Start/Stop; a virtual MIDI input on Linux; settings and
  the session restored on launch; presets shared with the VST3. Guide:
  [STANDALONE_APP.md](docs/STANDALONE_APP.md).
- **macOS** (`ArpSID Standalone.app`): runs ArpSID without a DAW: audio output, CoreMIDI input (a device or omni,
  a channel or omni, reconnect), on-screen keyboard, transport and tempo.
- Menus: Panic ⌘P, Next/Previous Preset ⌘] / ⌘[, Save User Preset ⌘S, Export
  ⌘E and Import ⌘I preset files, Reset All Parameters ⌘0, Random Patch ⌘R,
  every tab (⌘1–⌘9 and the View menu), full screen.

### State and recall

- The canonical state root holds every parameter and engine state; wrappers
  add the GUI models that are not parameters: SETTINGS, MIX, KIT, the DIGI
  model and its sample bank (the DIGI model and bank restore as a pair).
- The VST3 also saves the loaded `.sid` tune and subtune and the bypass
  state, in a tagged chunk format that older versions skip safely; every
  older VST3 project still loads.
- Presets and projects apply through an ownership mailbox on the next block,
  never mid-block.

### Realtime engine

- **One canonical render pipeline**: a single event ordering authority from
  host input to the chip; host sample offsets are never collapsed.
- **Lock-free and allocation-free** render path; heavy work (file parsing, state
  building, sample conversion) happens off the audio thread and is published
  through mailboxes.
- Blocks longer than 4096 frames are split without moving events; blocks
  without an output still advance the engine.
- Invalid host values (sample rate, block size, parameter values, NaN) are
  sanitized before they reach the engine; a NaN guard protects the output.

### Telemetry and displays

The engine publishes a telemetry snapshot every block: levels, voices,
transport, the SID register image, voice tokens, LFO values, drum, DIGI,
forensic and Hi-Fi meters, scopes (output, filter in/out, VCOs, DIGI, C64 bus)
and a full C64 machine snapshot. Editors only read these snapshots, and heavy
parts are copied only while a visible view needs them.

---

## Parameters

512 parameters, 289 automatable and 287 with an editor control. By tab:

| Tab | Main parameters |
|---|---|
| MAIN | Master Volume/Tune, Portamento and style, C64 Glide Delta, Voice Mode/Spread, VCO 1–3 (waveform, pulse width, PWM, detune, level, LF mode, sync, ring mod), filter, ADSR, limiter, reverb |
| LFO / ARP | LFO 1–4 rate/depth/shape/sync; arpeggiator enable, mode, rate, octaves, swing, gate, hold, latch, transpose, random, pattern length, glide legato |
| SID REG | Synth Mode, chip revision, external RC filter, oversampling, 6581 ADSR bug, `$D41D` system, `$D400–$D418` registers |
| SEQ | sequencer enable, tempo, swing, mode, length; 32 steps × note/velocity/gate; drum performance |
| DRSID | DrSID enable, model, volume, accent, drive, GM promotion, per-drum tune/decay/tone |
| FILTER | cutoff, resonance, mode, drive, keytrack, env and LFO amount, ADSR, chip, ohmic |
| MACRO | Macro 1–8; 9 × mod source and depth |
| FORENSIC | 29 forensic model controls |
| HI-FI | enable, quality, super-hires, width, depth, diffuser, warmth, tape, exciter |
| OPTIONS | a compact mirror of SID, drum, forensic and output controls |
| host-driven | per-channel mod wheel, breath, expression, sustain, sostenuto, pressure, pitch bend, RPN/NRPN, data entry (16 channels each), note ID and read-only values |

The complete list, with IDs, defaults, ranges, units and step counts, is
[docs/PARAMETER_REFERENCE.md](docs/PARAMETER_REFERENCE.md) (generated from the
code and checked by a test).

---

## Building

Requires a C++17 toolchain and CMake. On macOS, Xcode provides the
AU/AUv3/standalone toolchain; the VST3 needs the Steinberg VST3 SDK
(`--fetch-vst3-sdk` gets the pinned version).

```bash
# Configure + build (Linux-buildable core + tests)
./build.sh --build-dir build-release --config Release --parallel 4

# Full test suite
ctest --test-dir build-release --output-on-failure

# macOS: build, install and strict-validate the AUv2 component
./build.sh --install-auv2 --clear-au-cache --validate-auv2 --parallel 8

# Create a clean source release archive
./build.sh --package-release --parallel 4

# AddressSanitizer + UBSan build of the test suite (GCC/Clang)
./build.sh --sanitize --build-dir build-asan --config RelWithDebInfo

# VST3 (any platform, full editor everywhere). From scratch on Linux:
# install the build packages, fetch the pinned Steinberg VST3 SDK into
# .deps/vst3sdk, build, validate, run the host test and install to ~/.vst3:
./build.sh --install-deps --fetch-vst3-sdk --install-vst3

# ... or with an SDK checkout of your own, and a release-style zip:
./build.sh --no-tests --build-dir build-vst3 --vst3-sdk ~/vst3sdk --package-vst3
```

| `build.sh` option | Effect |
|---|---|
| `--vst3-sdk DIR` / `--fetch-vst3-sdk` | Build the VST3 with the SDK in `DIR`, or clone the pinned version (`scripts/fetch_vst3_sdk.sh`). The build runs the Steinberg validator and the host integration test (`arpsid_vst3_host_check`). |
| `--install-vst3` | Copy the bundle to `~/.vst3` (Linux) or the user VST3 folder (macOS); set another folder with `-DARPSID_USER_VST3_DIR=...`. |
| `--package-vst3` | Zip the bundle and its installer into `<build-dir>/dist/`. |
| `--no-vst3-editor` | Windows/Linux VST3 without the editor; hosts show their generic UI, and no X11/cairo/pango packages are needed. |
| `--install-deps` | Linux: install the build packages first (`scripts/linux/install_build_deps.sh`: apt, dnf, pacman or zypper). |

On Linux and Windows, `arpsid_vst3_editor_check` renders every editor tab
offscreen to `build-vst3/editor-snapshots/*.png` and checks the parameter
binding. If an editor package is missing, CMake stops and names every missing
one. The raw CMake steps are:

```bash
cmake -S . -B build-vst3 -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DARPSID_BUILD_VST3=ON -DARPSID_FETCH_VST3SDK=ON   # or -Dvst3sdk_SOURCE_DIR=$HOME/vst3sdk
cmake --build build-vst3 --target arpsid_vst3 arpsid_vst3_host_check
cmake --build build-vst3 --target arpsid_vst3_install_user        # ~/.vst3
sudo cmake --install build-vst3 --prefix /usr                     # /usr/lib/vst3
```

On Linux, `ccache` is used automatically when installed
(`-DARPSID_USE_CCACHE=OFF` to disable).

Repository guards (run before/after changes):

```bash
python3 scripts/check_version_coherence.py
python3 scripts/verify_source_tree.py
python3 scripts/check_audit_closure.py
```

## Testing

The CTest suite (about 480 tests) spans unit, runtime-behaviour, audio-shape,
timing/clock-conservation, state/persistence, GUI-wiring and source-contract
classes, and runs under AddressSanitizer + UBSan too:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DARPSID_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure -j4
```

Format-level checks:

| Check | What it covers |
|---|---|
| `arpsid_vst3_host_check` | Loads the built VST3 like a DAW: program list, units, MIDI mapping, state (v5 chunks, legacy v4), bypass, 64-bit processing, controller state, track info, notes and automation reaching the engine. |
| Steinberg `validator` | Run on every VST3 build (47 tests; 537 with `-e`). |
| `arpsid_vst3_editor_check` | Renders all 17 VST3 editor tabs offscreen and checks bindings, tab memory, right-click menus and keyboard notes. |
| `auval -strict` | All five AU flavors, no warnings allowed. |
| `arpsid_au_editor_snapshot` | Renders all 17 Cocoa tabs and the five flavor pages from the installed AUv2. |

## CI and releases

GitHub Actions (`.github/workflows/`) runs on every push to `main` and on pull
requests:

| Job | What it checks |
|---|---|
| `linux` | Repository guards, full build and full CTest suite with GCC and with Clang. |
| `linux-sanitizers` | Full CTest suite under AddressSanitizer + UndefinedBehaviorSanitizer. |
| `linux-vst3` | VST3 for x86_64 and aarch64 + Steinberg SDK validator + VST3 host integration test + offscreen render of every editor tab + install layout + installer smoke test. |
| `windows-vst3` | VST3 (MSVC) for x64, arm64 and x86 (32-bit) + Steinberg SDK validator + VST3 host integration test + offscreen editor render + static-runtime check + installer smoke test. |
| `macos` | AUv2 (all five flavors) + VST3 build, code-signature check, strict `auval`, VST3 host integration test. Public repository only. |
| `macos-apps` | Standalone app + AUv3 (Logic-compatible app): bundle/signature checks and a Standalone launch test. Public repository only. |
| `Screenshots` (manual) | Renders the Mac editor (every tab, every flavor) and publishes the images to the `ci-screenshots` branch. |

Every job fails on any ArpSID compiler or linker warning, and the macOS job
fails on any `auval` warning.

**Making a release** (public repository):

1. Bump `VERSION.txt`, `project(VERSION)` in `CMakeLists.txt`,
   `include/arpsid/version.h` and the README title, and add a
   `## [x.y.z]` entry to `CHANGELOG.md` (`python3 scripts/check_version_coherence.py`
   checks they agree).
2. Push to `main`. The **Release** workflow sees the `VERSION.txt` change,
   reruns the whole pipeline and publishes `vx.y.z` with every product
   variant (see the formats table above), the source zip and
   `SHA256SUMS.txt`. Pushing a `vx.y.z` tag works too, and a manual run
   rebuilds an existing release.

Releases are only published from the public repository and refuse any tree
that carries the real C64 ROMs (`scripts/ci/check_public_tree.sh`). From the
private repository, mirror a commit with
`scripts/sync_public_mirror.sh <path-to-ArpSID-public> --push`: it copies
everything except the ROM files and runs the same guard before committing.

## Documentation

| Document | Contents |
|---|---|
| [`CHANGELOG.md`](CHANGELOG.md) | Release notes. |
| [`docs/SYNTH_GUIDE.md`](docs/SYNTH_GUIDE.md) | Playing and programming CLASSIC, SYNTH / SID REG and DR SID: which mode to use, every control per mode, voice modes, filter, pitch and glide, register editing, recipes, troubleshooting. |
| [`docs/FEATURES.md`](docs/FEATURES.md) | Complete feature reference: every engine, control, range and behaviour, area by area. |
| [`docs/TECHNICAL_SPECIFICATIONS.md`](docs/TECHNICAL_SPECIFICATIONS.md) | Every number and limit: formats, I/O, SID and C64 timing, capacities, modulation, drums, DIGI, mixer, forensic ranges, state formats, editors. |
| [`docs/internals/`](docs/internals/README.md) | Low-level references: [SID chip core](docs/internals/SID_CHIP.md), [C64 machine](docs/internals/C64_MACHINE.md), [runtime and kernel](docs/internals/RUNTIME.md), [sound engines](docs/internals/ENGINES.md), [render modes, synth modes and projection engines](docs/internals/SYNTH_MODES.md). |
| [`docs/INSTALL.md`](docs/INSTALL.md) | Installing on macOS, Windows and Linux: one-line installers, installer options, manual steps, checksums, uninstalling, troubleshooting, building from source. |
| [`docs/AU_EDITOR.md`](docs/AU_EDITOR.md) | The macOS editor (AUv2, AUv3, Standalone, macOS VST3): a screenshot and guide for all 17 tabs, the five AU flavors, controls, shortcuts, the Standalone menus, and how the editor works. |
| [`docs/STANDALONE_APP.md`](docs/STANDALONE_APP.md) | The Windows/Linux standalone app: the audio/MIDI bar, patches and presets, saved settings and session, command line, troubleshooting, and how it is built. |
| [`docs/VST3_EDITOR.md`](docs/VST3_EDITOR.md) | The Windows/Linux VST3 editor: a screenshot and guide for all 17 tabs, the controls, and how the editor code works. |
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | System architecture: layers, kernel block pipeline, engines, parameters, state, GUI models and telemetry, wrappers. |
| [`docs/PARAMETER_REFERENCE.md`](docs/PARAMETER_REFERENCE.md) | Every parameter: ID, default, values, steps and editor tab (generated from the code and checked by a test). |
| [`docs/VST3_IMPLEMENTATION.md`](docs/VST3_IMPLEMENTATION.md) | VST3 internals: processor, bypass, 64-bit, kernel host, state format v5, controller, units, MIDI mapping, messages, threading, tests. |
| [`docs/REALTIME_OWNERSHIP.md`](docs/REALTIME_OWNERSHIP.md) | Which code runs on the render thread vs. the producer side. |
| [`docs/REALTIME_ROLLBACK_JOURNAL.md`](docs/REALTIME_ROLLBACK_JOURNAL.md) | Rollback-safe C64 render transactions. |
| [`docs/TAB_ARCHITECTURE.md`](docs/TAB_ARCHITECTURE.md) | The 17-tab production ring, persisted tab IDs and invariants. |
| [`docs/D418_NIBBLE_SPEC.md`](docs/D418_NIBBLE_SPEC.md) | DIGI `$D418` 4-bit sample format. |
| [`docs/SID_FILE_FORMAT_NOTES.md`](docs/SID_FILE_FORMAT_NOTES.md) | PSID/RSID handling. |
| [`docs/C64_EXACTNESS_BOUNDARIES.md`](docs/C64_EXACTNESS_BOUNDARIES.md) | What the C64 player does and does not claim. |
| [`packaging/macos/`](packaging/macos/) | Signing, notarization and release sign-off checklists. |

## Project layout

| Path | Contents |
|---|---|
| `include/arpsid/core/` | Canonical runtime, event queue, timing, SID chip/filter/envelope, C64 machine, post-FX, telemetry, parameter presentation, MIDI mapping, state codec. |
| `include/arpsid/engines/` | BitPerfect, single SID, SID register, arpeggiator, DrSID, SID-808, drum routing, DIGI sampler / `$D418`. |
| `include/arpsid/gui/`, `include/arpsid/patchbank/` | GUI models (SETTINGS, MIX, KIT, DIGI, SIDCORE, tabs, themes, languages); factory patch/kit banks. |
| `source/au2/`, `source/au3/` | AUv2 component, AUv3, DSP kernel, Cocoa view controller, Standalone app. |
| `source/vst3/`, `source/arpsid_controller.cpp`, `source/gui/` | VST3 processor, kernel host, controller; Cocoa bridge and cross-platform VSTGUI editor (`source/gui/vstgui/`). |
| `source/standalone/` | Windows/Linux standalone app: engine wrapper, RtAudio/RtMidi devices, toolbar and session, Win32 and X11 windows. |
| `source/tests/`, `scripts/` | Test suite and snapshot tools; build, install, validation and guard scripts. |

## Status and validation limits

Verified: the Linux-buildable core with a green test suite (also under
ASan/UBSan), the VST3 on all three platforms (validator and host test), and
strict `auval` on the AUv2 flavors. The following remain external sign-off
items and are **not** claimed as verified: Logic and other DAW host
validation beyond CI, notarization, a full third-party DAW host matrix, fuzz
coverage, and strict physical C64 exactness beyond the documented boundaries
([`docs/C64_EXACTNESS_BOUNDARIES.md`](docs/C64_EXACTNESS_BOUNDARIES.md)).

## Licensing

ArpSID is **Copyright (C) 2024-2026 Ulf Bertilsson** and is licensed under the
**GNU General Public License, version 3 or later (GPL-3.0-or-later)** — see the
[`LICENSE`](LICENSE) file. Some source files also carry earlier per-file
BSD-3-Clause / MIT SPDX headers; those permissive terms are GPL-compatible, and
the project as a whole is distributed under the GPL.

**C64 ROMs are not covered by this license.** The Commodore KERNAL, BASIC and
CHARGEN ROM images are copyrighted by their respective owners. Public source
releases do **not** bundle them (`c64_embedded_roms.h` ships zero-filled
placeholders); supply your own dumps at runtime. Do not redistribute the ROMs
without appropriate rights.
