# ArpSID editor on macOS (AU, AUv3, Standalone, VST3)

On macOS every ArpSID product shows the same native Cocoa editor:

| Product | Where the editor appears |
|---|---|
| **AUv2** (`ArpSID.component`, five flavors) | Logic Pro, GarageBand, MainStage, Ableton Live, Reaper, … when you open the plug-in window. |
| **AUv3** (`ArpSID.app` → `arpsid_auv3.appex`) | Hosts that load AUv3 instruments (Logic, GarageBand, AUM on Mac, …). |
| **Standalone** (`ArpSID Standalone.app`) | Its own window, with a menu bar for presets, MIDI input and tabs. |
| **VST3** (`arpsid_vst3.vst3` on macOS) | Any VST3 host; the Cocoa editor runs on top of the VST3 engine through `arpsid_vst_cocoa_bridge.mm`. |

The Windows and Linux VST3 has its own VSTGUI editor with the same 17 tabs
and the same parameters; see [VST3_EDITOR.md](VST3_EDITOR.md). The
[differences](#differences-from-the-windows--linux-editor) are listed at the
end of this page.

Every image on this page was rendered by `arpsid_au_editor_snapshot`
(`source/tests/arpsid_au_editor_snapshot.mm`). It loads the installed AUv2
like a host, holds a C3-G3-C4-E4 chord on factory patch 001, renders audio
between captures, selects each tab and writes it at the editor's design size
of 1280 × 752. Regenerate them with `scripts/update_au_screenshots.sh` on a
Mac, or run the **Screenshots** workflow (`.github/workflows/screenshots.yml`),
which publishes them to the `ci-screenshots` branch. The offscreen capture
cannot draw AppKit's translucent materials, so the header pop-up buttons look
paler in these images than on screen.

Every parameter, its ID, default, range and tab is listed in
[PARAMETER_REFERENCE.md](PARAMETER_REFERENCE.md).

---

## Contents

- [The window](#the-window)
- [Working with controls](#working-with-controls)
- [Keyboard shortcuts](#keyboard-shortcuts)
- [AU flavors](#au-flavors)
- [Tabs](#tabs): [MAIN](#main) · [LFO / ARP](#lfo--arp) · [SID REG](#sid-reg) ·
  [SEQ](#seq) · [DRSID](#drsid) · [FILTER](#filter) · [MACRO](#macro) ·
  [FORENSIC](#forensic) · [SIDCORE](#sidcore) · [C64](#c64) · [HI-FI](#hi-fi) ·
  [BANK](#bank) · [OPTIONS](#options) · [SETTINGS](#settings) · [MIX](#mix) ·
  [KIT](#kit) · [DIGI](#digi)
- [Standalone app](#standalone-app)
- [How the editor works](#how-the-editor-works)
- [Differences from the Windows / Linux editor](#differences-from-the-windows--linux-editor)

---

## The window

The editor is 1280 × 752 points. The header and footer stay the same on every
tab; the tab content fills the middle.

![MAIN tab with the header and footer](screenshots/au-editor-main.jpg)

**Header (top row)**

| Element | What it does |
|---|---|
| **☰ SID CHIP / PATCH** | A pull-down menu. **SID Chip** picks the chip revision (MOS 6581 R2, R3, R4, 8580 R5). **Patch** lists the factory patches and kits that belong to this product (a drum flavor lists drum kits, the C64 player its own range). Works in every host, because a plug-in cannot add items to the host's menu bar. |
| **C64 badge** | The C64 logo with the engine state (`READY`, the SID/VIC activity). It is on every tab. |
| **MIDI / ARP / SID LEDs** | MIDI flashes on incoming notes and controllers, ARP lights while the arpeggiator runs, SID while the chip is producing sound. |
| **Output scope and PK/RMS** | The last output samples as a trace, and the peak/RMS level of the output with the forensic activity figure. |
| **Render mode** (`SYNTH / SID…`) | The top-level engine: `CLASSIC SID Player` (the BitPerfect engine, by default the 8-chip × 3-voice "Poly-Illusion" topology, up to 24 voices; switch the topology to a single authentic SID on SETTINGS), `SYNTH / SID REG` (direct register authority: the register image you set on SID REG is what plays) or `DR SID Drums` (the drum engines only; melodic voices are silent). The hover text explains each. |
| **POLY · MONO · LEGATO · UNISON** | Voice allocation. `POLY` gives each note its own voice, `MONO` one voice with retrigger, `LEGATO` one voice that glides without retriggering, `UNISON` stacks voices on one note (spread by Voice Spread). Same as the Voice Mode parameter. |

**Footer (bottom rows)**

| Element | What it does |
|---|---|
| **◀ tab strip ▶** | The 17 tabs. Click a tab, or step with the arrows. Tab names depend on the [flavor](#au-flavors). Hover a tab for a description. The strip shrinks its font (11 to 8 pt) so every name fits. |
| **▶ transport, BPM, bar:beat** | Play/stop of the internal clock (the Standalone's transport; in a host the host transport rules), the tempo in use (host tempo when the host plays) and the musical position. |
| **Channel strip** (`CH1 LIVE`, …) | The MIDI channels that are active, the notes they play and which one has focus. |
| **HOST / CONTEXT** | Where the editor runs and what it plays, for example `SID REG · S001 / P001`: render mode, factory slot and patch. In the Standalone it names the MIDI input. |
| **C64 badge and ▾** | A second C64 badge with a quick menu. |
| **Piano keyboard** | Three octaves (C3–C5 by default). Click to play; velocity follows the click height. ▲ / ▼ at the ends shift the octave. Keys the engine is sounding are lit. |
| **MIDI INPUT** | The MIDI LED, the input status (`No MIDI input`, or the device name in the Standalone) and the **CC hub**: the live values of the eight mapped controller knobs (`K1 Cutoff 55%  K2 Resonance 16%  K3 Drive  K4 Env …`), the AKAI MPK mini K1–K8 map (CC70–77). |

---

## Working with controls

| Control | How to use it |
|---|---|
| **Knob** | Drag up/down or left/right. **Shift** drags finer. The **scroll wheel** turns it (Shift: 4× finer). **Arrow keys** step it by 1 % when it has focus (Shift: 0.2 %), **Page Up/Down** by 10 %. **Home**, **Option-click**, **double-click** or **right-click** resets it to the default. The caption above shows the parameter name (shrunk to fit), the text below its value in the same words the host shows (`0.7800`, `+0.0 ct`, `POLY`, `1.60 ms`, `SAW`, `LOW-PASS`). |
| **Value arc and glow** | The arc shows the value. A knob glows briefly when the host automates it, and a second, thinner arc shows the live modulated value when the mod matrix or a macro moves it. |
| **Pop-up menus** | Stepped choices (waveforms, filter modes, LFO shapes, sources). |
| **Segmented controls** | Modes such as POLY/MONO/LEGATO/UNISON, INT/HOST sync, the BANK category filter. |
| **Grids** | Step sequencers, pads, register cells and bank slots; see each tab. |

All parameter edits go to the host as normal automation gestures, so hosts
record automation and mark the project changed. Edits to MIX, KIT, DIGI and
SETTINGS change the state the plug-in saves with the project.

The editor follows the theme chosen on SETTINGS (Dark, Light, C64 Classic,
High Contrast). Standard controls (buttons, pop-ups, segmented controls) take
the matching appearance, dark or light, whatever the system's Light/Dark
setting is, so their text stays readable.

---

## Keyboard shortcuts

With the editor focused (inside a host these work when the host passes keys to
the plug-in window):

| Keys | Action |
|---|---|
| ⌘1 … ⌘9, ⌘0 | MAIN, LFO/ARP, SID REG, SEQ, FILTER, MACRO, FORENSIC, SIDCORE, BANK, OPTIONS |
| ⌘F | FILTER |
| ⌘↑ / ⌘↓ | Keyboard octave up / down |
| Arrow keys, Page Up/Down, Home | Adjust or reset the focused knob |
| Arrow keys / scroll on SID REG | Nudge the selected register |

The Standalone app adds menu shortcuts; see [Standalone app](#standalone-app).

---

## AU flavors

The AUv2 component registers five instruments. They run the same engine; the
flavor chooses the starting patch range, the drum routing and the tab names.

| Flavor | AU subtype | Host name | What it is for | Tab names that change |
|---|---|---|---|---|
| **Classic** | `ArpS` | ArpSID | Everything: synth, drums, DIGI, C64 player. | — |
| **Instrument** | `ArIn` | ArpSID Instrument | Melodic SID synth. DrSID is not armed by GM channel 10. | MAIN → `INSTR`, MACRO → `PERF`, BANK → `INST BANK` |
| **Drum Machine** | `DrSD` | DrSID | DrSID drum machine; drums always on. | LFO/ARP → `MOD`, SID REG → `SID BUS`, SEQ → `KIT`, MACRO → `MATRIX`, BANK → `KIT BANK` |
| **SID-808** | `S808` | SID-808 | The analog x0x-style SID-808 kits. | LFO/ARP → `GROOVE`, SID REG → `SID BUS`, SEQ → `SID-808`, MACRO → `MATRIX`, BANK → `808 BANK` |
| **C64 SID Player** | `C64P` | C64 SID Player | Plays `.sid` tunes on the emulated C64. | — |

| | |
|---|---|
| ![Classic flavor](screenshots/au-flavor-classic.jpg) | ![Instrument flavor](screenshots/au-flavor-instrument.jpg) |
| *Classic (`ArpS`)* | *Instrument (`ArIn`)* |
| ![Drum Machine flavor](screenshots/au-flavor-drum-machine.jpg) | ![SID-808 flavor](screenshots/au-flavor-sid-808.jpg) |
| *Drum Machine (`DrSD`): drum patch `S046`, `MONO` voice mode, drum tab names* | *SID-808 (`S808`)* |
| ![C64 SID Player flavor](screenshots/au-flavor-c64-sid-player.jpg) | |
| *C64 SID Player (`C64P`): `CLASSIC` render mode* | |

---

## Tabs

The tab order is the production ring in `include/arpsid/gui/tab_architecture.h`
(`kProductionVisibleTabs`), shared with the Windows/Linux editor. The editor
builds the MAIN page first and the others the first time you open them, so a
host opens the plug-in window quickly.

### MAIN

![MAIN](screenshots/au-editor-main.jpg)

The synthesizer at a glance.

- **MASTER**: Master Volume, Master Tune (±100 cents), Portamento (glide time,
  0–5 s), Voice Mode (`POLY`, `MONO`, `LEGATO`, `UNISON`) and Voice Spread
  (detune between unison voices). Portamento Style and C64 Glide Delta are on
  SID REG and OPTIONS.
- **VCO 1 / 2 / 3**: the three SID voices, eight knobs each. Waveform (`TRI`,
  `SAW`, `PULSE`, `NOISE`, and the combined waveforms), Pulse Width, PWM Depth
  (pulse-width modulation from the LFOs), Detune, Level, LF Mode (runs the
  oscillator at LFO rates), Sync (hard sync to the previous voice) and RingMod
  (ring modulation with the previous voice, on triangle).
- **FILTER TRACE**: the filter's live response curve over the output
  spectrum, with the cutoff marker and the current settings (`LP CUT 55.0%
  RES 16.0% DRV …`). The full filter controls are on [FILTER](#filter).
- **ADSR**: Attack, Decay, Sustain, Release of the amplitude envelope, with
  the SID's own envelope rates.
- **OUTPUT / FX**: Output Limiter on/off, Limiter Threshold, Attack and
  Release, and Reverb Mix.

*How to use it*: pick a waveform per voice, set Level to mix the voices,
shape the sound with the envelope, and use the render mode and voice mode in
the header to choose how notes are played.

### LFO / ARP

![LFO / ARP](screenshots/au-editor-lfo-arp.jpg)

- **LFO 1–4**: Rate (0.1–20 Hz, exponential), Depth, Shape (`SINE`,
  `TRIANGLE`, `SAW`, `RAMP DOWN`, `SQUARE`, `S&H`, `RANDOM`) and Sync (lock
  to host tempo). The panel background draws each LFO's wave live, with its
  phase cursor and the current `FREE`/synced state, rate and depth.
- **ARPEGGIATOR**: Enable, Mode (`UP`, `DOWN`, `UP/DOWN`, `DOWN/UP`, `RANDOM`,
  `PATTERN`, `CHORD`), Rate, Octaves (1–4), Swing, Gate, Hold, Latch,
  Transpose (±24 st), Random, Pattern length (1–32) and Arp Glide Legato. The
  **INT / HOST** switch picks the internal clock or the host's tempo and
  position.

*How it works*: arpeggiator notes are generated inside the engine at exact
sample positions, so they stay in time with the host even at small block
sizes. The LFOs feed the mod matrix ([MACRO](#macro)) and PWM.

### SID REG

![SID REG](screenshots/au-editor-sid-reg.jpg)

The SID register page, for synthesis by writing registers directly (Synth
Mode, the `SYNTH / SID REG` render mode).

- **SYNTH MODE** strip: Synth Mode on/off, SID Chip Revision, External RC
  Filter, SID Oversampling, 6581 ADSR Bug.
- **SID V1 / V2 / V3** scopes: the output of each voice, with peak and RMS.
- **Register grid**: one cell per register, coloured by voice (V1 cyan, V2
  yellow, V3 green, filter/volume magenta, read-only brown). Each cell shows
  the register name and its current hex value: `LO1 HI1 PW1L PW1H CR1 A1D1
  S1R1` for each voice, then `FCLO FCHI FLTV MVOL` (cutoff, resonance/routing,
  mode/volume), the read-only `POTX POTY OSC3 ENV3`, and `SYS` (`$D41D`, the
  chip model and clock).
  - **Click** a cell to select it and write it.
  - Type **hex** to enter a value directly.
  - **Arrow keys** or **scroll** nudge the value.
- The status line at the bottom decodes the SID state (`MOS 8580 R5 · CLOCK
  PAL · EXT RC ON · OS 1X · ADSR OFF · MODE SID REG …`).

### SEQ

![SEQ](screenshots/au-editor-seq.jpg)

The step sequencer and drum performance surface. It adapts to the mode: in
the drum flavors it is the kit's pattern editor.

- **Header**: the kit name and engine (`DrSID register/wavetable SID drum
  core`), the pattern page (1–16), tempo and the pad that played last, with
  SYNC/MIDI/SID/RT LEDs, the clock (`PAL 985248 Hz`) and the chip.
- **SID REGISTER SCOPE**: the live `$D400–$D418` activity of the drum voices.
- **16 STEP PATTERN**: a 16-step grid for BD, SD, CP, CH (kick, snare, clap,
  closed hat) with the kit pop-up (`USER · Standard`, factory kits), the page
  selector (`1-16`), **INT/HOST** clock, **STAMP** (copy the selected kit's
  pattern to this page) and **CLEAR**. Click steps to toggle them.
- **SID-808 VOICES**: twelve pads (KICK, SNARE, CLAP, RIM, CH, OH, TOM L,
  TOM H, COW, CONGA, CYM, MAR) with their General-MIDI notes. Click to play.
- **ANALOG SID CONTROLS**: TUNE, DECAY, SNAP, NOISE, CUTOFF, RESO for the
  selected voice, in two rows.
- **PLAY / STOP / REC / BANK / SAVE / INIT** and the **OUTPUT** meter.
- The Sequencer parameters (Enable, Tempo 20–300 BPM, Swing, Mode `FORWARD`,
  `REVERSE`, `PING-PONG`, `RANDOM`, Length 1–32) drive the melodic step
  sequencer; its 32 steps (note, velocity, gate) are listed in
  [PARAMETER_REFERENCE.md](PARAMETER_REFERENCE.md#sequencer-steps).

### DRSID

![DRSID](screenshots/au-editor-drsid.jpg)

DrSID is the drum engine that plays per-hit register micro-programs on the
SID.

- **ENGINE**: DrSID Enable, 6581 ADSR Bug. **MACHINE**: Drum Machine Model
  (`SID Drum Core`, the SID-authentic core, or the Analog X0X-8).
  **DRSID VOL**, **ACCENT / DRIVE**.
- **KICK** (Tune, Decay), **SNARE** (Tone, Snap), **HAT** (Tune, Decay,
  Metal), **CLAP** (Decay, Spread), **COWBELL** (Tune, Decay), **TOM** (Tune,
  Decay), **FILTER** (Cutoff, Resonance, Drive), **LIMITER** (on/off,
  threshold).
- **SEQUENCER BRIDGE**: the sequencer's Enable, Tempo, Swing, Mode and
  Length, so a drum pattern can be run from here.
- **Voice triggers**: KICK 36, SNARE 38, C-HAT 42, O-HAT 46, CLAP 39, COW 56,
  TOM-L 41, TOM-H 50 (the GM notes they answer to). Click to audition.
- **Scope**: the DrSID output with peak and RMS.
- Status line: patch, authority (`AUTH SIDREG`), tempo, the last hit and the
  peak.

Auto GM Drum Promotion (on OPTIONS) sends General-MIDI drum notes on channel
10 to DrSID.

### FILTER

![FILTER](screenshots/au-editor-filter.jpg)

- **LIVE FILTER SURFACE**: a large view of the filter response over the live
  input and output spectra, with the cutoff band, and the SID model and
  levels (`MOS 8580 R5 · LP · CUT 55.0% RES 16.0% … IN 12.5% OUT 12.5%`).
- **CUTOFF / RES / MODE**: Cutoff, Resonance, Mode (`OFF`, `LOW-PASS`,
  `BAND-PASS`, `LP+BP`, `HIGH-PASS`, `NOTCH`, `BP+HP`, `ALL`), Drive and
  KeyTrack.
- **ENV / MOTION**: Filter Env Amount, Filter LFO Amount and the ADSR.
- **CHIP / ANALOG**: SID Chip Revision, SID Clock System (legacy mirror), C64
  External RC Filter, SID Oversampling, 6581 ADSR Bug and Filter Ohmic (the
  filter's resistor behaviour in the forensic model).

The 6581 and 8580 filters sound different: the 6581's is darker and
distorts; the 8580's is cleaner. The chip revision changes the filter curve
here.

### MACRO

![MACRO](screenshots/au-editor-macro.jpg)

- **M1–M4**: large macro knobs (Macro 1–4). Macros 5–8 are parameters too, for
  host automation and the matrix.
- **MOD MATRIX**: nine destinations, each with a **Source** and a **Depth**
  knob: VCF Cutoff, VCF Resonance, VCO1/2/3 Frequency, VCO1/2/3 Pulse Width
  and Volume. Sources are `None`, LFO 1–4, velocity, note, key follow, mod
  wheel, pitch bend, aftertouch, poly pressure, random, Macro 1–8 and the
  envelope.

*Example*: set `Mod: VCF Cutoff Source` to `Macro 1` and its depth to 60 %,
then automate Macro 1 in the host for a filter sweep.

### FORENSIC

![FORENSIC](screenshots/au-editor-forensic.jpg)

The analog-forensic model adds the imperfections of real C64 hardware. It is
off by default (Forensic Enable).

- **FORENSIC GLOBAL**: Enable, Intensity, SID Chip Revision, External RC
  Filter, Oversampling, Startup Random (random chip state at power-on), 8580
  Digifix, Forensic Revision (legacy mirror) and Chip Variation (a per-chip
  seed, shown as a number).
- **CLOCK / SUPPLY**: Chip Temperature (°C), Supply Voltage, Clock Jitter,
  Supply Ripple and Thermal Drift, each with an enable switch and an amount.
- **VOICE / FILTER**: Voice Crosstalk and External Bleed (enable and amount),
  Envelope TDM and Filter Ohmic.
- **ADC / BUS / BOARD**: D418 Asymmetry, System Noise, Motherboard, ADC Bleed,
  Bus Collision and POT Input.
- **FORENSIC VCO SCOPES**: one scope per voice, with peak, RMS and the
  forensic activity.
- **FILTER / BUS TRACE** with a readout of every model value (`FORENSIC ON ACT
  … CLOCK JITTER … VOICE CROSSTALK … BUS NOISE …`).

### SIDCORE

![SIDCORE](screenshots/au-editor-sidcore.jpg)

A read-only view of the SID as the engine drives it, drawn with Metal where
available.

- **Overlay scopes** V1 MATRIX, V2 CYAN, V3 RAINBOW: each voice's output.
- **Voice 1–3 LIVE** columns: the waveform name, frequency, pulse width and
  ADSR nibbles; an envelope bar; F P A D S R bars (frequency, pulse, attack,
  decay, sustain, release); and TRI, SAW, PULSE, NOISE, SYNC, GATE lights.
- **FILTER / MIX**: filter type, cutoff, resonance and volume bars, a small
  3D register view, and the `FC`, `RES/FLT`, `MODE/VOL` registers in hex.
- A text overlay with voice mode, patch, transport, peaks and flags.
- Four text panels: **MATRIX VOICE / GATES**, **FILTER / OPEN BUS**,
  **6510 / VIC / CIA / DISASM** (the C64 CPU, PC and disassembly, VIC/CIA
  state, the SID bus route) and **FORENSIC / REGS / GPU** (master volume,
  limiter, reverb, forensic state and the raw `$D400–$D418` image).

### C64

![C64](screenshots/au-editor-c64.jpg)

The C64 tune player and a live view of the emulated machine.

- **Buttons**: **LOAD .SID** (PSID/RSID), **◂ SUB** / **SUB ▸** (previous /
  next subtune), **BOOT**, **START**, **STOP**, **RESET**, **EJECT**,
  **UNLOAD**, **FORENSIC ON**, **REAL 8580**, **VIC:FAST** and **6510:FAST**.
- **Status**: the player line (tune, subtune, ROM state), the strict-RSID
  path and any downgrades, bus error counters (`SID READ APPROX`, `SID OPEN
  BUS`, `INVALID CHIP R/W`, `SID HOLE`, `RMW`, `INIT BRK`) and the debug
  timeline.
- **SID BUS 3D / OPEN BUS / PHI2**: a realtime open-bus scope with register,
  value, write, PHI2 and IRQ lanes, and CIA latch activity.
- **CPU / CLOCK / RUNTIME LINES** and **6510 DISASSEMBLY / CALL SURFACE**:
  PHI2 counter, block, play rate, ROM checksums, the CPU registers and the
  disassembly around PC.
- **ALL CHIP STATES / BUS LINES**: 6510 microcore, VIC-II (raster, cycle,
  badline, IRQ, bank, sprite DMA), clock.
- **CIA / IEC / TAPE / SID REGISTERS**: both CIAs' timers and ICR, the IEC
  and tape lines, and the SID register mirror with read/write statistics.
- **REALTIME RAW C64 MEMORY**: hex dumps of zero page, stack, the page at PC,
  screen, ROM, I/O, SID and colour RAM, with change highlighting. These are
  published by the render thread and copied under a sequence lock.

*Using it*: load a `.sid` file, it starts on its default subtune; step with
◂ SUB / SUB ▸. PSID tunes play with the built-in driver. RSID tunes need your
own KERNAL, BASIC and CHARGEN ROM images; ArpSID ships none. See
[C64_EXACTNESS_BOUNDARIES.md](C64_EXACTNESS_BOUNDARIES.md) and
[SID_FILE_FORMAT_NOTES.md](SID_FILE_FORMAT_NOTES.md).

### HI-FI

![HI-FI](screenshots/au-editor-hi-fi.jpg)

The "Hi-Fi Transcendence" chain after the SID. The SID itself is not changed.

- **HI-FI TRANSCENDENCE MODE**: the source chain (`SID CORE / REGISTER ENGINE
  → HI-FI Enhancement Layer → Transcendence Layer`) and the realtime safety
  indicators: `SAFE`, `NO ALLOCS`, `NO LOCKS`, `NO HOST CALLS`,
  `PREALLOCATED`, `LATENCY OK`.
- **QUALITY ENGINE / OVERSAMPLING**: HI-FI Enable, Quality (`PURE`, `HI-FI`,
  `TRANSCENDENCE`) and Super-Hires oversampling (4× ECO, 8× STUDIO,
  16× INSANE). Pure emulation bypasses the enhancement.
- **TRANSCENDENCE MACROS**: WIDTH / DEPTH, WARMTH / TAPE, AIR / EXCITER and
  DIFFUSE / VOICE LANES, with a stereo-field view (mono safety).
- **SIGNAL FLOW / VOICE LUXURY MATRIX / PRESETS**: the chain (`SID CORE → 12
  VOICE MIXER → OVERSAMPLING → PER-VOICE DIFFUSER → TAPE/WARMTH →
  AIR/EXCITER → WIDTH/DEPTH → CABINET/BODY → OUTPUT`), per-voice activity and
  eight presets: PURE SID, CLEAN HI-FI, WARM 6581, WIDE CINEMA,
  TRANSCENDENCE, TAPE DREAM, MONO SAFE, OFFLINE 16x.
- **TELEMETRY / SOURCE / OUTPUT / SAFETY**: source and output meters and the
  enhancement delta (width, mono, CPU, allocations, locks, host calls).

### BANK

![BANK](screenshots/au-editor-bank.jpg)

Patches, banks and files.

- **Buttons**: **Save Patch…**, **Load Patch…** (`.arpsid`), **Save Bank…**,
  **Load Bank…** (`.arpsidbank`), **Patch JSON…**, **Bank JSON…**,
  **Import JSON…**, **C64 JSON…**, **Export All…** and **Load Factory**.
- **Category filter**: KEYS, CHRM (chromatic percussion), BASS, ORCH, LEAD, DRSID, SID808, DIGI.
- **Drum kits**: the saved-kit menu, **RESCAN**, **KIT EXP** and **KIT IMP**
  (DrSID kits as `.arpsidbank` / `.json` drum libraries).
- **Slot grid**: all 180 factory slots, 16 per row, coloured by family:
  `000–079` melodic patches in General-MIDI order (Acoustic Grand Piano …
  Ocarina), `080–119` DrSID kits (Kick/Snare/Hat/Open Hat/Clap-Rim/Tom/
  Cowbell/Cymbal grids 01–05), `120–149` SID-808 kits (Classic, Punch, Lo-Fi,
  Hard, Wide × A–F) and `150–179` DIGI 4-bit kits 01–30. Click a slot to load
  it; the loaded slot is highlighted.

### OPTIONS

![OPTIONS](screenshots/au-editor-options.jpg)

A compact page of the options you change most, and the C64 control hub.

- **SID AUTH / CLOCK**: SID Chip Revision, SID Clock System, C64 External RC
  Filter, SID Oversampling, 6581 ADSR Bug.
- **DRSID LOW END / KIT**: DrSID Enable, Drum Machine Model, DrSID Volume,
  Drum Accent, Kick Tune/Decay, Tom Tune/Decay, Hat Metal, Clap Spread, Drum
  Drive.
- **FORENSIC / BOARD**: Forensic Enable, Startup Random, 8580 Digifix,
  Forensic Revision, Chip Variation, Forensic Intensity.
- **OUTPUT / SAFETY**: Output Limiter, Threshold, Attack, Release, Reverb Mix.
- **LIVE OPTIONS HUD**: **MIDI PREFS**, **LOAD .SID**, ◂ / ▸ subtune,
  **EJECT**, **UNLOAD**, **BOOTSTRAP**, **C64 BOOT / START / STOP / RESET**,
  **ARM DRSID**, **MPK MAP** and **PANIC** (all voices off), with a status
  line of the whole engine (mode, chip, clock, filter, oversampling, ADSR bug,
  ARP and SEQ clocks, drum core, volume, accent, drive, limiter, reverb,
  octave, peak).

### SETTINGS

![SETTINGS](screenshots/au-editor-settings.jpg)

Global preferences, saved with the project (not host parameters).

- **AUDIO ENGINE / Topology mode**: `BitPerfect (legacy 8-chip)` or
  `Authentic Single SID` (one 3-voice chip, like a real C64).
- **HOST SYNC**: host tempo is the transport authority for ARP, SEQ and the
  C64 player.
- **MIDI MAPPING**: the GM / MPK mini map is live (channel 10 drums, CC70–77).
- **THEME / Color scheme**: Dark, Light, C64 Classic, High Contrast. The
  editor recolours at once.
- **LANGUAGE / Plugin language**: English, Norsk, Deutsch, Français, 日本語.
- **ADVANCED**: canonical drum routing (owned by the flavor) and the
  diagnostics (C64 and SIDCORE live telemetry).
- Footer: schema version, engine, theme and language.

### MIX

![MIX](screenshots/au-editor-mix.jpg)

The mixer after the instruments.

- **CH1–CH16**: a volume fader, a pan slider, **D** (delay send) and **R**
  (reverb send), **S** (solo), **M** (mute) and five insert-FX pop-ups
  (`EQ 3-band`, transient, compressor, saturator, bitcrusher).
- **MASTER**: Volume, Limiter on/off, Threshold, Release, Width, **Dim -10 dB**.
- **DELAY BUS** and **REVERB BUS**: Enable and Return level.

### KIT

![KIT](screenshots/au-editor-kit.jpg)

The drum-kit editor for DrSID, SID-808 and DIGI (tab name `KIT EDIT`).

- **Drum classes** (left): KICK, SNARE, CHAT (closed hat), OHAT (open hat),
  CLAP, RIM, TOM, CBELL (cowbell), CRASH. Select one to edit it.
- **Engine target**: DrSID, SID-808 or DIGI: which engine plays the kit.
- **Step grid**: 32 steps for the selected class. Click to set or clear.
- **Slot strip** (bottom): the factory sound this class uses on the target
  engine (`DrSID #00` …), scrollable.
- Per class, the SID-808 voice override (waveform, ring, sync, filter, ADSR
  nibbles, pulse width) and the DIGI assignment (tune, start, length, sample,
  loop, reverse) are stored in the kit and saved with the project.

### DIGI

![DIGI](screenshots/au-editor-digi.jpg)

The DIGI sampler: 4-bit samples played through the SID's `$D418` volume
register, the way C64 digis are.

- **SLOT 1–8**: select a slot.
- **SRC** (source: empty, one of the 30 factory DIGI sounds or the user
  sample), **SLOT**, **TUNE** (±12 st).
- **D418 RX**: the `$D418` path and its options (AUTO, FAST, AUTH ON, CLR
  REC, …) and the route pop-up (`AUTH C64-BUS D418` writes through the
  emulated C64 bus; `FAST PRIVATE D418` uses a private register engine),
  **RATE** (8000 Hz canonical) and **DIGI PANIC**.
- **MIDI MAP**: the root note of pad 1 (`ROOT 60 / C4`), the note range
  (`SLOTS C4..G4`), the channel (OMNI or 1–16) and **MAP C4**.
- **AUDITION**: eight pads and **PAD VEL** (velocity presets).
- **START**, **LEN**, **VOL** sliders, **LOOP** and **REV**.
- **User sample**: **IMPORT** an audio file (any format macOS reads: WAV,
  AIFF, CAF, MP3, AAC, …), or **REC** from a macOS audio
  input (the device pop-up, `System Default Input`). The conversion mode
  (LINEAR / LOUD …), **NORM** (normalise) and **TRIM** (cut silence) apply to
  the take, which is resampled to the canonical 8 kHz 4-bit stream (at most
  7.5 s). **KIT OUT** / **KIT IN** export and import the DIGI kit.
- **AUTH BUS** telemetry: the nibble route, writes accepted or dropped, pad
  state, root/channel, bus and SID status.
- **Step grid**: 32 steps for the selected slot.

The DIGI layer plays through its own isolated SID, so samples never disturb
the main SID or a playing tune. The `$D418` format is described in
[D418_NIBBLE_SPEC.md](D418_NIBBLE_SPEC.md).

---

## Standalone app

`ArpSID Standalone.app` hosts the same editor with its own audio and MIDI.

| Menu | Items |
|---|---|
| **ArpSID** | About, **Panic (All Notes Off)** ⌘P, **Next Preset** ⌘], **Previous Preset** ⌘[, **Save User Preset…** ⌘S, **Export Preset to File…** ⌘E, **Import Preset from File…** ⌘I, **Reset All Parameters** ⌘0, **Random Patch** ⌘R, Quit ⌘Q |
| **MIDI** | **Input Device** (a device or All Devices (Omni)), **MIDI Channel** (1–16 or All Channels (Omni)), **Reconnect All Sources** ⇧⌘R |
| **View** | MAIN ⌘1, LFO/ARP ⌘2, SID REG ⌘3, SEQ ⌘4, FILTER ⌘5, MACRO ⌘6, FORENSIC ⌘7, SIDCORE ⌘8, BANK ⌘9; OPTS, C64, HI-FI, DRSID; SETTINGS, MIX, KIT EDIT, DIGI; Toggle Full Screen ⌃⌘F |

The footer's HOST / CONTEXT line names the MIDI input, and the transport plays
the internal clock at the tempo shown.

---

## How the editor works

- **One view controller.** `ArpSIDViewController`
  (`source/au3/ArpSIDViewController.mm`) builds the whole editor in code (no
  XIB). The AUv2 Cocoa view factory, the AUv3 extension, the Standalone app and
  the macOS VST3 bridge all create this controller; only the adapter it talks
  to differs (`ArpSIDDSPKernelAdapter` for AU, `ArpSIDVSTDebugAdapter` over the
  VST3 kernel host).
- **Lazy pages.** The header, footer and MAIN are built first; every other tab
  is built the first time it is shown, then kept. In Logic's out-of-process
  AUv2 the first build is deferred behind a boot cover so the host is never
  blocked.
- **Parameters.** Knobs and menus are bound to parameter IDs. Edits go to the
  host's parameter tree (AU) or controller (VST3). Host changes and
  automation come back through the parameter observer and move the controls;
  automated knobs glow. Value text comes from `SidParameterPresentation`, the
  same service that formats host text, so the editor and the host always
  agree.
- **Telemetry.** A single display-link tick drives all live views. It reads
  snapshots the render thread publishes (scopes, meters, registers, C64
  memory, forensic values); it never reads live render state, and the render
  thread never waits for the editor. Idle views stop redrawing.
- **Models.** SETTINGS, MIX, KIT and DIGI are models held by the adapter and
  published to the render thread through mailboxes; the editor edits the
  model and the project state includes it.
- **Host lifecycle.** Closing the plug-in window pauses the editor and keeps it
  for reuse; only final disposal tears down observers, capture and Metal
  state (`pauseEditorViewForHostDetach`, `prepareForFinalEditorDisposal`).

---

## Differences from the Windows / Linux editor

Both editors have the same 17 tabs in the same order, the same parameters and
the same value text. They differ in layout and extras:

| | macOS (this page) | Windows / Linux VST3 |
|---|---|---|
| Size | 1280 × 752, fixed | 1200 × 800, resizable 0.5×–3× |
| Toolkit | AppKit + Core Animation / Metal | VSTGUI |
| Patch selection | ☰ SID CHIP / PATCH menu, BANK grid | header patch menu with ◀ ▶, BANK grid |
| Render mode, voice mode | header pop-up and segmented control | Voice Mode parameter on MAIN |
| Keyboard | 3 octaves with octave buttons, ⌘↑/⌘↓ | 5 octaves; computer keys A–L play notes |
| DIGI recording | from a macOS audio input device | from the plug-in's `DIGI Capture In` side-chain bus |
| Right-click on a knob | resets it | opens the host's parameter menu |
| Extra views | SIDCORE Metal matrix, C64 memory dumps, HI-FI presets, forensic readouts | compact equivalents |
| Flavors | five AU flavors with their own tab names | one VST3 instrument |
