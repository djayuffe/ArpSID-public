# ArpSID VST3 editor (Windows and Linux)

The Windows and Linux VST3 plug-in has a full ArpSID editor built with VSTGUI.
It has the same 17 tabs as the macOS editor, a control for every user
parameter, the live displays, and the BANK, SETTINGS, MIX, KIT, DIGI and C64
player panels. On macOS the VST3 uses the native Cocoa editor shared with the AU
(see [VST3_IMPLEMENTATION.md](VST3_IMPLEMENTATION.md#editors)).

Every image on this page was rendered by `arpsid_vst3_editor_snapshot`. It
draws each tab offscreen with the real engine running: factory patch 001 is
loaded and a C3–G3–C4–E4 chord is held, which is why four keys are lit on the
keyboard. Regenerate the images with `scripts/update_editor_screenshots.sh
<vst3-build-dir>`.

- [The editor window](#the-editor-window)
- [Working with controls](#working-with-controls)
- [Tabs](#tabs): [MAIN](#main) · [LFO / ARP](#lfo--arp) · [SID REG](#sid-reg) ·
  [SEQ](#seq) · [DRSID](#drsid) · [FILTER](#filter) · [MACRO](#macro) ·
  [FORENSIC](#forensic) · [SIDCORE](#sidcore) · [C64](#c64) · [HI-FI](#hi-fi) ·
  [BANK](#bank) · [OPTIONS](#options) · [SETTINGS](#settings) · [MIX](#mix) ·
  [KIT](#kit) · [DIGI](#digi)
- [How the editor works](#how-the-editor-works)
- [Tests](#tests)
- [Limits](#limits)

Every parameter, its ID, default, range and tab is listed in
[PARAMETER_REFERENCE.md](PARAMETER_REFERENCE.md).

---

## The editor window

The editor is designed at 1200 × 800 and scales as one piece.

- **Resizing.** Drag the host window's corner to resize it, from half size
  (600 × 400) to three times (3600 × 2400); the 3:2 shape is kept.
- **HiDPI.** The host's content scale factor multiplies your chosen size, so
  the editor keeps its size relative to the screen.
- **Remembered size and tab.** The editor reopens at the size and on the tab
  you left it. Both are saved with the project (in the controller state), so
  they survive closing and reopening the project too.

**Header**

| Element | What it does |
|---|---|
| `ArpSID` / version | Product name and the plug-in version. |
| `<` `>` | Previous / next factory patch. |
| Patch menu | All 180 factory patches (`001 Acoustic Grand Piano` … `180 Digi 4-bit Kit 30`). Picking one goes the same way as a host program change: the controller asks the processor to load the patch and then mirrors its parameter values back to the host. |
| Status line | `BYPASSED` while the host bypass is on, then the engine mode (`CLASSIC`, `SYNTH`, `DRSID`, `C64`, or `C64 PLAYER` while a .sid tune plays), active voices, host tempo, `PLAY`/`STOP`, and `ARP` / `SEQ` when they are on. |
| Track line | The host track's name, in the track's colour, when the host shares it (VST3 channel context), then the computer-keyboard octave (`keys A-L  octave C4 (Z/X)`). |
| Output meters | Left and right output peak, with a falling peak mark. They turn red above 0.95. |

**Tab strip.** There are 17 tabs in the canonical order (`tab_architecture.h`,
`kProductionVisibleTabs`). A tab's page is built the first time it is opened.

**Keyboard.** Five octaves, C2–C7. Click a key to play it. Velocity follows
where you click: about 40 at the top of the key, 127 at the bottom. Dragging
across keys plays a glissando. The notes go to the processor as MIDI (channel 1)
through the host's message channel. Every note the engine is sounding is lit,
whether it came from the host, the keyboard or a held chord.

### Playing from the computer keyboard

While the editor has keyboard focus, the letter keys play notes, laid out like
a piano:

| Keys | Notes |
|---|---|
| `A S D F G H J K L` | white keys C, D, E, F, G, A, B, C, D |
| `W E T Y U O` | black keys C♯, D♯, F♯, G♯, A♯, C♯ |
| `Z` / `X` | octave down / up (C0–C8; starts at C4, MIDI 60) |

Notes use velocity 100 on MIDI channel 1. Key repeat is ignored, so holding a
key holds the note. Keys pressed with Ctrl, Alt or Cmd, and every other key
(space, arrows, numbers), go to the host, so transport shortcuts keep working.
Some hosts keep all keys for themselves; there, use the on-screen keyboard.

### Parameter menu (right-click)

Right-click any knob, switch or menu to open the host's menu for that
parameter. What it offers depends on the host: typically automation lanes,
MIDI learn or remote-control assignment. ArpSID adds **Reset to Default** at
the end. Double-click (or Ctrl-click) a knob also resets it.

### Bypass

The host's bypass button uses ArpSID's `Bypass` parameter. Bypass fades the
output out over 10 ms and back in the same way, so it never clicks. The
engine keeps running while bypassed: held notes, the arpeggiator, the
sequencer and a playing .sid tune stay in time. The state is saved with the
project, and the header shows `BYPASSED`.

---

## Working with controls

Controls are chosen by the parameter's step count, which comes from
`normalizedParamStepCount`:

| Control | Used for | Mouse |
|---|---|---|
| **Knob** | continuous parameters, and stepped ones with more than 32 steps | Drag up/down; horizontal drag counts a quarter. A full drag of 180 px covers the range; **Shift** makes it about 7× finer (1200 px). **Wheel**: 2 % per notch, 0.2 % with Shift. **Double-click** or **Ctrl-click** resets to the default. The arc shows the value; it lights while you drag. |
| **Toggle** | on/off parameters | Click to switch. The LED and frame light when it is on. |
| **Menu** | stepped parameters with 2–32 steps | Click to choose. Entries are named after what the engine plays (see [decode law](#stepped-values-and-the-decode-law)). |
| **Byte knob** (MIX, KIT, DIGI) | model values (0–255, semitones, nibbles) | Drag: 180 px covers the range, **Shift** is 6× finer. **Wheel**: ±1. |
| **Grid** (SEQ, KIT, DIGI, BANK) | steps, pads and patch slots | See each tab. |

Every edit is sent to the host as a normal automation gesture (begin, perform,
end), so hosts record automation and mark the project as changed. Edits on the
MIX, KIT, DIGI and SETTINGS tabs are not parameters. They change the state the
plug-in saves with the project, and the editor tells the host the project has
changed (`IComponentHandler2::setDirty`).

When a host changes a parameter (automation, a preset, another editor
instance), the control follows within one refresh, about 33 ms. A control you
are dragging is not moved under your mouse.

---

## Tabs

### MAIN

![MAIN tab](screenshots/vst3-editor-main.png)

The main synthesizer page.

- **MASTER**: Master Volume, Master Tune (±100 cents), Portamento time (0–5 s)
  and style (`C64 SLIDE`, `C64 FIXED`, `LINEAR`, `SMOOTH`), C64 Glide Delta
  (register step per frame for the C64 glide styles), Voice Mode (`POLY`,
  `MONO`, `LEGATO`, `UNISON`) and Voice Spread.
- **OUTPUT**: the main output oscilloscope (the last 512 output samples).
- **VCO 1 / 2 / 3**: the three SID voices. Waveform (`TRI`, `SAW`, `PULSE`,
  `NOISE` and the combined `TRI+SAW`, `TRI+PUL`, `SAW+PUL`, `TRI+SAW+PUL`),
  Pulse Width, PWM Depth, Detune, Level, LF Mode (low-frequency oscillator
  range), Sync and RingMod.
- **FILTER**: Cutoff, Resonance, Mode (`OFF`, `LOW-PASS`, `BAND-PASS`, `LP+BP`,
  `HIGH-PASS`, `NOTCH`, `BP+HP`, `ALL`; the index is the SID's LP/BP/HP bits),
  Drive and KeyTrack. Next to them is a response curve with the cutoff
  frequency and Q. The curve is an approximate display (cutoff mapped
  30 Hz – 12 kHz); the audio path is the SID filter model.
- **ADSR**: the amplitude envelope.
- **OUTPUT / FX**: Output Limiter on/off, limiter Threshold, Attack and Release,
  and Reverb Mix.

### LFO / ARP

![LFO / ARP tab](screenshots/vst3-editor-lfo-arp.png)

- **LFO 1–4**: Rate (0.1–20 Hz), Depth, Shape (`SINE`, `TRIANGLE`, `SAW`,
  `RAMP DOWN`, `SQUARE`, `S&H`, `RANDOM`) and Sync (to host tempo).
- **LFO WAVES**: each LFO's shape scaled by its depth, with a cursor at the
  live phase and the current output value.
- **ARPEGGIATOR**: Enable, Mode (`UP`, `DOWN`, `UP/DOWN`, `DOWN/UP`, `RANDOM`,
  `PATTERN`, `CHORD`), Rate, Octaves (1–4), Swing, Gate, Hold, Latch,
  Transpose (±24 semitones), Random, Pattern Length (1–32 steps) and Arp Glide
  Legato. With the host playing, the arpeggiator follows the host tempo and
  position.

### SID REG

![SID REG tab](screenshots/vst3-editor-sid-reg.png)

Direct access to the SID registers, for register-driven synthesis (Synth Mode).

- **SYNTH MODE**: Synth Mode (SID Reg) on/off, SID Chip Revision (MOS 6581 R2,
  R3, R4 or 8580 R5), External RC Filter, SID Oversampling, 6581 ADSR Bug,
  and the `$D41D` system pseudo-register (chip model and clock bits).
- **LIVE REGISTERS**: what the engine is writing to the chip right now. It
  shows each voice's frequency, pulse width, control byte (waveform names and
  `GATE`), AD and SR; the filter cutoff, resonance, routing, mode and volume;
  POTX/POTY/OSC3/ENV3; and the raw `$D400–$D41C` image, 16 bytes per line.
- **VOICE 1 / 2 / 3**, **FILTER / VOLUME**: one knob per writable register,
  `$D400–$D418`. Captions read `D400 FREQ LO` (the section names the voice)
  and values read `$XX`. POTX, POTY, OSC3 and ENV3 (`$D419–$D41C`) are
  read-only and appear only in the readout.

### SEQ

![SEQ tab](screenshots/vst3-editor-seq.png)

- **SEQUENCER**: Enable, Tempo (20–300 BPM when not following the host), Swing,
  Mode (`FORWARD`, `REVERSE`, `PING-PONG`, `RANDOM`) and Length (1–32 steps).
- **DRUM PERFORMANCE**: the DrSID drum controls you reach for while sequencing:
  Kick Tune and Decay, Snare Snap and Tone, Drum Accent, Drum Drive and DrSID
  Volume.
- **STEPS**: all 32 steps, with three rows.
  - **NOTE**: left click raises the note by a semitone, right click lowers it,
    and holding Shift moves an octave.
  - **VEL** and **GATE**: click or drag to set the level from where you click
    (bottom = 0, top = full). Right click sets it to 0.

  Steps past the sequence length are drawn faded. While the sequencer runs,
  the playing step has an outline.

### DRSID

![DRSID tab](screenshots/vst3-editor-drsid.png)

DrSID is the drum engine that plays register micro-programs on the SID.

- **DRSID ENGINE**: DrSID Enable, Drum Machine Model (the SID-authentic drum
  core or the Analog X0X-8), DrSID Volume, Drum Accent, Drum Drive, and Auto
  GM Drum Promotion. With promotion on, General-MIDI drum notes on channel 10
  go to DrSID.
- **DRUM ACTIVITY**: level meters for kick, snare, closed hat, open hat, clap,
  tom, cowbell and rim, plus the last hit (note and velocity).
- **KICK / SNARE**, **HATS / CLAP**, **TOM / COWBELL**: tune, decay and tone for
  each drum voice (Snare Snap, Hat Metal, Clap Spread and so on).

### FILTER

![FILTER tab](screenshots/vst3-editor-filter.png)

- **FILTER** with its **RESPONSE** curve: the same controls as on MAIN.
- **ENV / MOTION**: filter Env Amount and LFO Amount, and the ADSR envelope
  that drives them.
- **CHIP / ANALOG**: SID Chip Revision, External RC Filter, SID Oversampling,
  6581 ADSR Bug, and Ohmic (the filter's resistive behaviour in the forensic
  model).
- **FILTER IN / OUT**: scopes of the signal going into the filter (dim) and
  coming out of it.

### MACRO

![MACRO tab](screenshots/vst3-editor-macro.png)

- **MACROS**: eight macro knobs. They are modulation sources in the matrix and
  good targets for host automation.
- **MODULATION SOURCES**: live meters. LFO 1–4 and pitch bend are bipolar and
  fill from the centre; mod wheel, pressure and the random source fill from
  the left.
- **MOD MATRIX**: nine destinations (filter cutoff and resonance, VCO 1–3
  frequency and pulse width, master volume). Each has a Source (`None`, the
  LFOs, velocity, note, key follow, mod wheel, pitch bend, aftertouch, poly
  pressure, random, the eight macros, the envelope) and a Depth.

### FORENSIC

![FORENSIC tab](screenshots/vst3-editor-forensic.png)

The analog-forensic model adds the imperfections of real C64 hardware.

- **FORENSIC GLOBAL**: Enable and Intensity; SID Chip Revision, External RC
  Filter and SID Oversampling; Startup Random (a random chip state at power-on);
  8580 Digifix; Revision (a legacy mirror kept for old projects); and Chip
  Variation (a per-chip seed).
- **ACTIVITY**: the model's live values: activity, intensity, clock jitter,
  supply ripple, thermal drift, voice crosstalk, external bleed, system noise
  and Digifix.
- **CLOCK / SUPPLY**: Chip Temperature (°C), Supply Voltage, Clock Jitter,
  Supply Ripple and Thermal Drift. Each of the last three has an enable switch
  and an amount.
- **VOICE / FILTER**: Voice Crosstalk and External Bleed (each with enable and
  amount), Envelope TDM, and Ohmic.
- **ADC / BUS / BOARD**: D418 Asymmetry, System Noise, Motherboard, ADC Bleed,
  Bus Collision and POT Input.
- **VCO SCOPES**: one lane per SID voice, showing the oscillator output.

### SIDCORE

![SIDCORE tab](screenshots/vst3-editor-sidcore.png)

A read-only view of the SID as the engine drives it.

- **SID REGISTERS**: the same live readout as on SID REG.
- **VCO SCOPES**: one lane per voice.
- **SID BUS TIMELINE**: the last 128 C64 bus samples the engine published, one
  lane each for the register index, the value written, the write strobe, the
  PHI2 phase and IRQ/DMA. The newest sample is on the right. The lanes stay
  flat and low while no C64 program is running.

### C64

![C64 tab](screenshots/vst3-editor-c64.png)

- **SID PLAYER**
  - **LOAD .SID...** opens a PSID or RSID file (up to 1 MB) and starts
    subtune 1. **EJECT** unloads it.
  - The loaded tune and the current subtune are saved with the project and
    come back when it is opened.
  - **< SONG** and **SONG >** step through the subtunes, also for a tune
    restored with a project.
  - **BOOT**, **START**, **STOP** and **RESET** drive the emulated C64.
  - **VIC-II FAST** and **6510 FAST** switch the VIC-II and CPU fast paths.

  With a tune loaded, the panel shows title, author, release, the current
  subtune, the load, init and play addresses, PAL/NTSC, the SID model, play
  calls and the play rate.
- **MACHINE**: the emulated machine, live.
  - The 6510 registers (PC, A, X, Y, SP, P) with JAM/IRQ/NMI flags, and three
    disassembled lines at PC.
  - The VIC-II raster line, cycle, badline/BA/IRQ, bank and frame.
  - Timers A and B (value and latch), ICR and time-of-day for both CIAs.
  - The last SID write, the open-bus value and the chip count.
  - The PHI2 cycle, the render block, running or halted, and whether a full
    user ROM set is loaded.

  RSID tunes need your own KERNAL, BASIC and CHARGEN ROM dumps; none are
  bundled with ArpSID.

### HI-FI

![HI-FI tab](screenshots/vst3-editor-hi-fi.png)

The "Hi-Fi Transcendence" post-processing chain, after the SID.

- **MODE**: Enable, Quality (`PURE`, `HI-FI`, `TRANSCENDENCE`) and Super-Hires
  oversampling (4×, 8×, 16×).
- **STEREO**: Width, Depth and Voice Diffuser.
- **COLOUR**: Analog Warmth, Tape Saturation and Psycho Exciter.
- **DELTA MONITOR**: dry, wet and difference meters, the mono correlation and
  the safety gain, so you can see what the chain changes.

### BANK

![BANK tab](screenshots/vst3-editor-bank.png)

The patch browser.

- **FACTORY** shows all 180 factory patches in six columns:
  - `001–080`: melodic patches, in General-MIDI order;
  - `081–120`: DrSID drum kits;
  - `121–150`: SID-808 kits;
  - `151–180`: DIGI 4-bit kits.

  Click a patch to load it. The current patch has an outline.
- **USER BANK** shows the patches of the last `.arpsidbank` you loaded; click
  one to load it.
- **LOAD PATCH** / **SAVE PATCH** load or save one `.arpsid` file (the full
  patch).
- **LOAD BANK** reads an `.arpsidbank` into the user view. **SAVE BANK** writes
  a bank of all 180 factory patches, with the current patch in its own slot.

### OPTIONS

![OPTIONS tab](screenshots/vst3-editor-options.png)

A compact page for runtime options.

- **SID AUTH / CLOCK**: chip revision, External RC Filter, oversampling and the
  6581 ADSR bug.
- **OUTPUT / SAFETY**: the output limiter and reverb mix.
- **PERFORMANCE**: **PANIC** resets all voices. **ALL NOTES OFF** sends
  CC 123 on all 16 channels.
- **DRSID LOW END / KIT** and **FORENSIC / BOARD**: the drum and forensic
  controls you use most.

### SETTINGS

![SETTINGS tab](screenshots/vst3-editor-settings.png)

Global preferences. They are saved with the project, and are not host
parameters.

- **AUDIO ENGINE / Topology mode**: `BitPerfect` (legacy 8-chip topology) or
  `Single SID` (one authentic 3-voice chip).
- **THEME / Color scheme**: Dark, Light, C64 Classic or High Contrast. It
  applies at once to the whole editor.
- **LANGUAGE**: English, Norsk, Deutsch, Français or 日本語. It is used by the
  SETTINGS strings; the rest of the editor stays in English.
- **ADVANCED / Diagnostic dashboard**: Off or On.

The panel on the right shows fixed policies (host tempo drives ARP, SEQ and C64
timing; the GM drum map is on channel 10; the signal routing) and live engine
state (render mode, SID model, voices, host tempo, beat and telemetry frame).

### MIX

![MIX tab](screenshots/vst3-editor-mix.png)

The mixer, which sits after the instruments.

- **16 channel strips**: VOL, PAN (`C`, or L/R with a percentage), DLY and REV
  sends, **M** (mute), **S** (solo), **ON** (channel enabled), and **FX**, which
  selects the channel whose FX chain is shown below.
- **SENDS / MASTER**: delay and reverb return levels, master volume, stereo
  width, limiter threshold and release, **LIMITER** on/off, **DIM -10 dB**
  (monitor dim), and **DLY ON** / **REV ON** for the send buses.
- **FX CHAIN**: five insert slots for the selected channel. Each slot has a
  type (`none`, `EQ 3-band`, `transient`, `compressor`, `saturator`,
  `bitcrusher`) and eight parameters, P1–P8. What P1–P8 mean depends on the
  type: EQ low/mid/high, compressor threshold/ratio/attack/release, and so on.

### KIT

![KIT tab](screenshots/vst3-editor-kit.png)

The drum-kit editor for all three drum engines.

- **ENGINE TARGET**: which engine the kit plays: DrSID, SID-808 or DIGI.
- **Step grid**: 9 drum classes by 32 steps. The classes are kick (GM 36),
  snare (38), closed hat (42), open hat (46), clap (39), rim (37), tom (47),
  cowbell (56) and crash (49).
  - Click a step to set it (velocity 100) or clear it; right click sets a soft
    step (velocity 60).
  - Shift-click on a set step toggles its accent.
  - Clicking a row also selects that drum class.
- For the selected class:
  - **DRUM CLASS** selects it.
  - **DrSID SLOT**, **SID-808 SLOT** and **DIGI SLOT** choose the factory
    sound the class uses on each engine.
  - **SID-808 VOICE** overrides the class's SID-808 voice: waveform buttons
    (TRI, SAW, PUL, NOI), RING, SYNC and FILTER, the ADSR nibbles, and the
    12-bit pulse width. Only the fields you touch are overridden.
  - **DIGI ASSIGNMENT** sets tune (±12 semitones), start, length (`full` or
    a scale), the sample number, LOOP and REVERSE.

### DIGI

![DIGI tab](screenshots/vst3-editor-digi.png)

The DIGI sampler. It plays 4-bit samples through the SID's `$D418` volume
register, the way C64 digis do.

- **PAD 1–8**: click to audition a slot (velocity 110); this also selects it.
- **REC**: record into the selected slot from the plug-in's audio input
  `DIGI Capture In`, a side-chain bus. Route a track or input to it in your
  host; hosts keep it off until you do.
  - Press **REC** to start and **STOP** to keep the take.
  - The line below the info panel shows the take's length and peak level, or
    a reminder when the input receives no audio.
  - The take is normalised, resampled to the canonical 8 kHz 4-bit `$D418`
    stream, cut at 7.5 s, and becomes the slot's source ("capture 1",
    "capture 2", …).
  - Up to about 22 s at 48 kHz can be recorded; a full buffer ends the take by
    itself.
- **IMPORT WAV...**: load a WAV file into the selected slot. It accepts PCM 8,
  16, 24 and 32-bit and float 32/64-bit, including WAVE_FORMAT_EXTENSIBLE,
  mixes it to mono, and converts it to the canonical 8 kHz 4-bit `$D418`
  stream. Longer files are cut at 60 000 frames (7.5 s). The scope next to the button shows the DIGI
  output.
- **Step grid**: 8 slots by 32 steps.
  - Click a step to set it (velocity 100) or clear it; right click sets 60,
    and Shift sets 127.
  - Drag to paint steps.
  - The playing step has an outline while the sequencer runs.
- For the selected slot:
  - **SOURCE**: empty, one of the 30 factory DIGI sounds, or the imported
    user sample.
  - **TUNE** (±12 st), **START**, **LENGTH** and **VOLUME**.
  - **LOOP**, **REVERSE** and **CLEAR ROW**.
- **$D418 MODE**:
  - `AUTH C64-BUS D418`: 4-bit writes to `$D418` on the emulated C64 bus,
    PHI2-synchronous. They honour I/O banking and open bus, like a real C64.
  - `FAST PRIVATE D418`: the same 4-bit stream through a private SID
    register engine, off the bus. This is a quick preview mode.

  Both render the digi through an isolated SID, so a drum or sample layer
  cannot disturb the main SID or a playing .sid tune.
- **PAD ROOT** and **PAD CH**: which MIDI note plays pad 1 (pads are
  consecutive notes from there) and which channel they listen on (1–16 or
  OMNI).
- The info panel counts playing voices, triggers, `$D418` writes (accepted by
  the SID, or blocked because I/O is banked out), and shows the last nibble.

---

## How the editor works

### Source files

| File | Contents |
|---|---|
| `source/gui/vstgui/arpsid_vstgui_plugview.cpp` | The VST3 `IPlugView` (`VSTGUIEditor` + `IPlugViewContentScaleSupport`). It opens a `CFrame` in the host window (X11 on Linux, HWND on Windows), adds the `EditorView` and runs the 33 ms refresh timer. `ControllerBackend` adapts the edit controller to `EditorBackend`. |
| `source/gui/vstgui/arpsid_editor_backend.h` | `EditorBackend`: everything the editor needs from its host. It covers parameter read, begin/perform/end edit, parameter text, factory patch select, MIDI, the kernel host and the dirty flag. The plug-in and the test implement it. |
| `source/gui/vstgui/arpsid_editor_layout.h` | The tab tables: for each tab, rows of sections with a parameter list or a display. It has no UI types, so a plain unit test can check it. |
| `source/gui/vstgui/arpsid_editor_view.{h,cpp}` | `EditorView`: header, tab strip, pages, keyboard, the page layout, parameter binding and the refresh loop. |
| `source/gui/vstgui/arpsid_editor_pages.cpp` | Every display (scopes, meters, readouts, the SEQ grid) and the model panels (BANK, SETTINGS, MIX, KIT, DIGI, C64 player). |
| `source/gui/vstgui/arpsid_editor_widgets.{h,cpp}` | Widgets and theme: `ParamKnob`, `ParamToggle`, `ParamMenu`, `ByteKnob`, `ChoiceMenu`, `ActionButton`, `TabStrip`, `ScopeView`, `MeterView`, `TextGrid`, `KeyboardView`, `CellGrid`, `FilterCurveView`, `LfoWaveView`, `SectionPanel`, `Label`. |
| `source/gui/vstgui/arpsid_editor_labels.h` | Choice names and value text, and the decode law for stepped parameters (`stepIndexForParam`). |
| `source/gui/vstgui/arpsid_wav_reader.h` | WAV parser for DIGI import. |

### Data flow

```
 host automation ─┐                                   ┌─ Vst3KernelHost (processor)
                  ▼                                   │   telemetry, GUI models,
 EditController ◄── ControllerBackend ◄── EditorView ─┤   SID file, C64 hub
 (parameters)       (EditorBackend)       │  refresh  │
        │                                 │  33 ms    └─ read directly; same
        │ IMessage: LoadFactoryPatch,     │              process only
        ▼          UiMidi                 ▼
 ArpSIDVst3Processor              pages, displays, widgets
```

- **Parameters** go through the edit controller only, as `beginEdit` /
  `performEdit` / `endEdit`. The host forwards them to the processor
  sample-accurately. The editor never writes parameters into the engine
  directly, so automation recording and undo work as for any plug-in.
- **Everything else** is read or edited on the processor's `Vst3KernelHost`:
  telemetry (meters, scopes, register images, C64 machine state), the SETTINGS,
  MIX, KIT and DIGI models, the SID player and the C64 control hub. The editor
  gets the host's address through a processor → controller message, which is
  accepted only if both run in the same process (a pid check). Hosts that run
  the processor in another process still get a working editor for every
  parameter. The panels that need the engine then show "Needs the ArpSID
  engine in this process".
- **Refresh** runs every 33 ms on the UI thread:
  1. Poll the kernel host's non-realtime work.
  2. Read telemetry. Scopes and the C64 snapshot are copied only if the
     visible tab shows them.
  3. Mirror every parameter into its controls, skipping any control being
     dragged.
  4. Update the header.
  5. Refresh the visible tab's displays.

  The model panels re-read their model only when its generation counter
  changes. For DIGI this avoids copying the ~480 KB sample bank on every
  refresh.

### Page layout

`EditorLayout` gives each tab up to four rows, and each row up to five
sections with width weights. `EditorView::buildPage_` sizes them as follows.

- **Width.** Each section gets its share of the row width.
- **Controls.** Inside a section, controls flow left to right:
  - knobs take 66 × 76 px cells;
  - toggles (27 px) and menus (37 px) stack in 112 px cells, as many as fit in
    a knob's height.

  If the section also has a display, the controls get the left 56 % and the
  display gets the rest.
- **Height.** Rows of plain controls get exactly their natural height (the
  same flow run without creating anything). Rows with a display or model
  panel share the rest by their height weights, never below their natural
  height. If the natural heights do not fit, every row falls back to its
  weight. Knob rows stay compact, and scopes and grids get the space.

### Stepped values and the decode law

A stepped parameter is still a normalized 0..1 value on the host side. Hosts
can automate values between the grid points, so the editor must name the entry
the engine will actually play. `stepIndexForParam` uses each parameter's engine
law:

| Parameters | Engine decode | Engine source |
|---|---|---|
| VCO 1–3 Waveform | `floor(min(v, 0.999999) × 8)` | `bitperfect_engine.h`, `valueToWaveform` |
| Filter Mode | `int(v × 8)`, clamped to 0..7 | `bitperfect_engine.h`, `setFilterMode` |
| Arp Octaves | `int(v × 3)` (then 1 + that many octaves) | `arpeggiator.h`, `setOctaves` |
| every other stepped parameter | `round(v × steps)` | |

Menus, labels, the filter curve, the LFO shape display and the SEQ grid all use
it. A menu always writes the on-grid value `index / steps`, which decodes back
to the same index under every law. `EditorLayoutCoverageTests` pins the table
above against the formulas. For example, waveform 0.42 is labelled `NOISE`
because the engine computes floor(3.36) = 3.

### Platform notes

- **Linux.** VSTGUI draws with cairo and pango on X11/xcb and uses the host's
  `IRunLoop` for timers and events (`vstgui_linux_runloop_support.cpp`).
  VSTGUI's cairo backend passes `CDrawContext::drawArc` angles to `cairo_arc`
  as radians, although the API takes degrees. The editor therefore strokes
  every arc through a graphics path, which converts the angles correctly on
  all platforms.
- **Windows.** VSTGUI draws with Direct2D and DirectWrite. The offscreen test
  initialises COM, because Direct2D and WIC need it (a host always provides
  it).
- **Build.** VSTGUI is added directly, not through the SDK helper, which would
  pull in the GTK standalone toolkit. It is built without deprecated methods
  (this must match the editor sources, or the class layouts differ), without
  OpenGL, and as position-independent code for the plug-in module. The Linux
  build packages are listed in the README.

---

## Tests

| Test | What it proves |
|---|---|
| `EditorLayoutCoverageTests` (`source/tests/editor_layout_coverage_tests.cpp`) | The tab order matches the production tab ring. Every user parameter has a control (287), and no exempt parameter is placed on a tab. The stepped decode law matches the engine formulas, and every stepped index round-trips. |
| `arpsid_vst3_editor_check` / `Vst3EditorSnapshotTests` (`source/tests/vst3_editor_snapshot_tests.cpp`) | With the real kernel running, every tab builds and draws, and each rendered tab has real content. A host-side parameter change reaches the control, and an editor edit reaches the kernel. It writes one PNG per tab. CI runs it on Linux and Windows and keeps the Linux PNGs as an artifact. |
| `ParameterReferenceDocTests` | [PARAMETER_REFERENCE.md](PARAMETER_REFERENCE.md) matches the parameter table, the value text and the editor layout. |

---

## Limits

- The layout is fixed and scales as a whole (0.5× to 3×). Resizing does not
  reflow the controls.
- The language setting applies to the SETTINGS strings only; tab and control
  captions are English.
- DIGI recording needs the host to route audio to `DIGI Capture In`. Hosts
  differ in how they expose instrument side-chain inputs. In REAPER, use the
  track's plug-in pin connector; in Bitwig, the device's side-chain selector.
