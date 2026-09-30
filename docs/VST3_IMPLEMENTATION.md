# ArpSID VST3 implementation

How the VST3 plug-in is built: the processor, the kernel host, the edit
controller, how they talk, what the saved state contains, and how each
platform's editor attaches. For the editor itself see
[VST3_EDITOR.md](VST3_EDITOR.md); for every parameter see
[PARAMETER_REFERENCE.md](PARAMETER_REFERENCE.md).

- [Overview](#overview)
- [Plug-in identity and buses](#plug-in-identity-and-buses)
- [Processor](#processor)
- [Kernel host](#kernel-host)
- [State format](#state-format)
- [Edit controller](#edit-controller)
- [Processor ↔ controller messages](#processor--controller-messages)
- [Editors](#editors)
- [Threading](#threading)
- [Building and installing](#building-and-installing)
- [Tests](#tests)

---

## Overview

The VST3 runs the same DSP kernel as AUv2, AUv3 and the Standalone app,
`ArpSIDDSPKernel` (`source/au3/ArpSIDDSPKernel.hpp`, portable C++). The wrapper
has three parts:

```
 host ──process()──► ArpSIDVst3Processor ──TimedEvent[] + TransportState──► Vst3KernelHost ──► ArpSIDDSPKernel
                          ▲    │ getState/setState (v5 chunks)                    ▲  models (SETTINGS/MIX/KIT/DIGI)
        IMessage          │    ▼                                                  │  telemetry, SID file, C64 hub
 host ──param edits──► ArpSID controller ─────── kernel host address (same process) ─┘
                          │  512 parameters, program list, MIDI mapping
                          └──► editor (Cocoa on macOS, VSTGUI on Windows/Linux)
```

| File | Class | Role |
|---|---|---|
| `source/vst3/arpsid_vst3_processor.{h,cpp}` | `ArpSIDVst3Processor` | `AudioEffect`. Buses, process data → kernel events, transport, state I/O, messages. |
| `source/vst3/arpsid_vst3_kernel_host.{h,cpp}` | `Vst3KernelHost` | Owns the kernel and the GUI models that are not parameters. Handles render chunking, the state codec, telemetry, the SID player and the C64 hub. It is the platform-neutral twin of the AU `ArpSIDDSPKernelAdapter`. |
| `source/arpsid_controller.cpp` | `ArpSIDControllerPhase3` | `EditController` (with `IEditController2` knob mode) + `IMidiMapping` + `IUnitInfo` (program list) + `IInfoListener`. It is compiled into `factory.cpp`. |
| `source/arpsid_vst_messages.h` | — | Message IDs and attribute names between processor and controller. |
| `source/factory.cpp`, `source/plugin_ids.{h,cpp}` | — | The factory and the two class IDs. |
| `source/au3/ArpSIDKernelTelemetryFill.h` | — | Kernel → `ArpSIDTelemetry` projection, shared with the AU adapter. |

---

## Plug-in identity and buses

| | Value |
|---|---|
| Processor class | `ArpSID`, FUID `A1B2C3D4-E5F60718-9A0B1C2D-3E4F5A6B`, category `ARPSID_PLUGIN_CATEGORY` (`include/arpsid/version.h`) |
| Controller class | `ArpSID Controller`, FUID `B2C3D4E5-F6071829-A0B1C2D3-E4F5A6B7` |
| Audio out | one main output bus, stereo (mono accepted) |
| Audio in | `DIGI Capture In`: auxiliary (side-chain), stereo or mono, **inactive by default**. It is only read while a DIGI recording is armed. |
| Events | one input bus, `MIDI In`, 16 channels |
| Sample size | 32-bit and 64-bit float (the engine renders 32-bit; 64-bit buses are converted at the edge) |
| Bypass | `Bypass` parameter (id 1024, `kIsBypass`), soft: see [Host bypass](#host-bypass) |
| Tail | `kInfiniteTail` (release, reverb and delay can ring on) |
| Process context | tempo, transport state, project time (music), cycle, bar position, time signature |
| Browser image | `Contents/Resources/Snapshots/A1B2C3D4E5F607189A0B1C2D3E4F5A6B_snapshot.png` (the editor's MAIN page, 1200 × 800, from `resources/vst3/`), listed in `moduleinfo.json` for host plug-in browsers |

The class IDs are unchanged since the first VST3 release, so projects saved
with any earlier version open with the current engine.

---

## Processor

### `initialize`

The processor adds the buses and the process-context requirements, then
queues factory patch 0. That makes a fresh instance start from a known patch;
the patch is applied at the first rendered block.

### `setupProcessing` / `setActive`

`setupProcessing` calls `Vst3KernelHost::setup(sampleRate, maxSamplesPerBlock)`
and sizes the 64-bit scratch buffers (two output and two input channels of
`maxSamplesPerBlock` floats) and the bypass fade step.
A re-setup (new sample rate or block size) keeps the audible state: the host
snapshots every parameter and the sticky preset slot, sets the kernel up again,
restores the snapshot and republishes the GUI models. `setActive(false)` resets
the kernel (voices, effect tails). Activation also snaps the bypass fade to
its target, so a bypassed instance starts silent without a fade.

### `process`

0. **Bypass.** The last point of the `Bypass` queue (if any) sets the host's
   bypass flag; see [Host bypass](#host-bypass).
1. **Parameter flush.** With `numSamples == 0` there is no audio. The last
   value of each changed parameter is staged through the kernel's non-realtime
   parameter intent.
2. **Events** (`collectEvents_`) go into a preallocated `TimedEvent` array
   (`kMaxTimedEvents`, 4096). `process` never allocates; the host test checks
   this with an allocation probe around `process()`.
   - **Notes first.** Note on and note off become `NoteOn` / `NoteOff` with
     channel, pitch, velocity and the host note ID (a note-on with velocity 0
     is a note-off); poly pressure becomes `PolyPressure`. Notes are collected
     before automation, so a flood of automation can never push a note-off
     out of the block (up to 0.9.10 that could leave a stuck note).
   - **Automation.** Each point of each parameter queue becomes an
     `EventKind::ParameterSet` at its sample offset, so automation is
     sample-accurate. Program and BankSlot are skipped: the controller turns
     those into a patch load (see below). The points share a budget with the
     notes, `kParamPointBudget` (256 events per block), which keeps the block
     inside the kernel's event lanes (2048 ingress, 512 per merge lane). Past
     the budget each queue is thinned to evenly spaced points that always
     include its last one, so every parameter still ends the block on the
     host's value. Hosts normally send a few points per parameter per block;
     the budget matters only for extreme automation.
   - Events are clamped to the block, given a monotonic arrival order, and
     sorted with the kernel's total order (`TimedEvent::before`: sample
     offset, then event priority, then arrival) by `std::sort`, which unlike
     `std::stable_sort` never allocates a temporary buffer.
3. **DIGI capture input.** If the host feeds `DIGI Capture In`, the processor
   reports that the input is active and passes the block to
   `captureDigiInput`. That call writes only while a recording is armed.
4. **Transport** (`readTransport_`) maps `ProcessContext` to the kernel's
   `TransportState`: tempo, musical position, playing, cycle active, and loop
   start/end. The render sample rate always comes from `setupProcessing`.
5. **Render** (`renderSlice_`). The output buffers are cleared, then
   `Vst3KernelHost::render` runs, then the bypass fade is applied.
   - A 32-bit block is one slice, whatever its size: the kernel splits blocks
     larger than its 4096-frame chunk itself (see [Rendering](#rendering)).
   - With 64-bit buses the capture input is converted to float scratch first,
     the kernel renders into float scratch (sized by `maxSamplesPerBlock` in
     `setupProcessing`), and the result is widened into the host's `double`
     buffers. A 64-bit block larger than `maxSamplesPerBlock` (a host error,
     seen in some offline renders) is rendered in scratch-sized slices: each
     slice gets its events rebased and the musical position advanced by
     `start × bpm / (sampleRate × 60)`. Up to 0.9.10 such a block was
     answered with silence. The host test checks that one oversized 64-bit
     block renders the same audio as the announced block size.
6. **Silence flags.** A block whose peak is at or below −120 dBFS
   (`kSilenceGate`, 1e-6) is written as exact zero and flagged silent on
   every output channel, so hosts can skip downstream processing. That
   covers the engine's 24-bit TPDF dither and settling residue once the
   voices have released (up to 0.9.11 only exact zeros counted, so a block
   holding only dither was never flagged). Most factory patches carry a
   chip/board profile with a modelled analogue floor near −80 dBFS even with
   no voice sounding; that is sound and is not flagged. (The SDK validator
   treats anything under −77.6 dBFS as silence and reports an info for it.)

### Host bypass

`Bypass` (parameter id **1024**, flags `kCanAutomate | kIsBypass`, on/off, in
the root unit) is VST3-only. It sits outside the shared 0–511 parameter space,
so AU and Standalone ids are unchanged. Hosts use it for their bypass button.

- Bypass is **soft**: the engine keeps running (notes, arpeggiator, sequencer
  and the C64 player stay in time), only the output fades to silence over
  10 ms (`kBypassRampSeconds`), and back in the same way. Switching never
  clicks.
- Fully bypassed blocks are exact zeros, so the silence flags are set.
- The flag is saved in the processor state (`BYPS` chunk). The controller
  reads it in `setComponentState` (`Vst3KernelHost::decodeBypass`), so a
  reopened project shows the right bypass state. States without the chunk
  (all earlier versions) load un-bypassed.
- The editor header shows `BYPASSED` while it is on.

### `getState` / `setState`

These forward to `Vst3KernelHost::saveState` / `loadState` (see
[State format](#state-format)). `setState` reads the stream in 64 KB pieces
and refuses streams above 64 MB.

---

## Kernel host

`Vst3KernelHost` wraps one `ArpSIDDSPKernel` and the GUI models the AU adapter
keeps next to its kernel:

| Model | Type | Size |
|---|---|---:|
| SETTINGS | `GUI::SettingsPanelModel` (topology, theme, language, diagnostics) | 32 B serialized |
| MIX | `GUI::MixPanelModel` (16 channels, 2 send buses, master, 5 FX slots × 8 params per channel) | 1264 B |
| KIT | `GUI::KitStateBlob` (panel model, 9 × 32 step grid, per-class SID-808 voice and DIGI assignment) | 1676 B |
| DIGI model | `GUI::DigiPanelModel` (8 slots × 32 steps, per-slot source, tune, start, length, volume, flags) | 360 B |
| DIGI sample bank | `GUI::DigiSampleBankBlob` (8 user clips, ≤ 60 000 4-bit frames each) | 480 392 B |

Model access is mutex-protected. Every change goes through
`publishModelsLocked_`, which sanitizes the models, repairs DIGI user-sample
references, hands them to the render thread through the kernel's ownership
mailboxes (`publishGuiRealtimeModels`), and increments `modelGeneration()`.
The editor watches that counter to know when to reload a panel.

### Rendering

`render(outputs, channels, frames, events, n, transport)` runs on the audio
thread:

- The whole host block is one `processBlock` call (stereo),
  `processBlockMono` (mono) or an output-less call (no bus).
- The kernel splits a block larger than its `kMaxFramesPerBlock` (4096)
  itself: per chunk it rebases event offsets, advances the musical position
  by `start × bpm / (sampleRate × 60)` and sorts the chunk's events. It also
  drains the editor's queued MIDI and parameter intents once against the
  whole block, so their timing is right. Up to 0.9.10 the kernel host split
  large blocks itself, which bypassed that and could shift an on-screen
  keyboard note into the wrong chunk.

### Patches

`loadFactorySlot(slot)` builds the canonical state root for a factory patch
(`makeFactoryPatchStateRootForSlot`) and schedules it. `scheduleStateRoot`
takes any root, for example a user patch file. Both call the kernel's
`schedulePendingStateRestore`: an RT-safe mailbox that the next `processBlock`
applies before it dispatches any event.

**Pending-root rule.** A scheduled root counts as applied only after a block
has rendered it. `scheduledSeq_` and `renderedSeq_` track this. Until then,
`currentStateRoot` (and so `saveState`) returns the pending root, not the
kernel's shadow. A host that loads a preset and saves while transport is
stopped therefore saves the new patch.

### Other services

| Method | Purpose |
|---|---|
| `setParameterNonRealtime`, `parameter` | Stage a parameter value (UI thread), and read the kernel's current value. |
| `settings/mix/kit/digi` and their setters | Read and replace a model (then publish). |
| `setDigiUserSample(slot, samples, frames, rate, name)` | Store an imported mono sample in a DIGI slot: resampled to 8 kHz (nearest sample), quantized to 4-bit `$D418` nibbles, and cut at 60 000 frames (7.5 s) (`digiBuildUserSampleClipFromFloatMono`). |
| `setDigiD418RuntimeMode`, `setDigiMidiPadMapping`, `triggerDigiPad` | DIGI runtime policy (0 = AUTH C64-bus `$D418`, 1 = FAST private `$D418`; rate 1–32 kHz), the pad root note and channel, and pad audition. |
| `loadSidFile`, `unloadSidFile`, `isSidFileLoaded`, `selectSidSubtune`, `sidSubtune` | The PSID/RSID player. The host keeps a copy of the file (at most 1 MB), so the tune and its subtune are saved with the project (`SIDF` chunk) and the subtune can change at any time. |
| `armDigiCapture(slot)`, `stopDigiCapture(name)`, `cancelDigiCapture`, `captureDigiInput`, `digiCaptureStatus` | DIGI recording from `DIGI Capture In` (see [DIGI capture](#digi-capture)). |
| `c64ControlHubCommand(n)` | 1 boot, 2 start, 3 stop, 4 reset, 5 load projection bootstrap, 6/7 VIC-II fast on/off, 8/9 CPU fast on/off. |
| `setPureSid1Q1OutputMode` | Pure-SID output mode (saved in the `OUTM` chunk). |
| `injectMidi` | Queue a MIDI message from the editor keyboard. |
| `readTelemetry(out, scopes, c64)` | Fill `ArpSIDTelemetry`. The scope and C64 snapshot sections are copied only on request. |
| `pollNonRealtime` | Perform drum-bridge factory-slot loads that render deferred (called from the editor timer). |

### DIGI capture

1. **Arm.** `armDigiCapture(slot)` runs on the UI thread. It allocates a mono
   buffer of `GUI::kDigiRecordCaptureMaxFrames` (1 048 576) samples,
   allocating only if it does not have one yet. Then it clears the counters
   and sets `captureArmed_`.
2. **Record.** On the audio thread, `captureDigiInput` mixes the input to mono
   and appends it to the buffer. It never allocates, and stops at the end of
   the buffer.
3. **Stop.** `stopDigiCapture` clears `captureArmed_`, then waits while
   `captureBusy_` is set. The audio thread sets `captureBusy_` around each
   write and checks the armed flag again after setting it. All four accesses
   are `seq_cst`, so either the audio block sees the capture disarmed, or the
   UI thread sees the block busy and waits for it (at most one block). Stop
   then normalises the take and hands it to `setDigiUserSample`: it is
   resampled to 8 kHz 4-bit `$D418` and becomes the slot's source.

The editor stops a take on its own when the buffer is full.

---

## State format

`getState` writes version 5: a little-endian `u32` version, then tagged chunks
(`u32 tag`, `u32 length`, payload). A reader skips unknown tags, so later
versions can add chunks without breaking older readers.

| Tag | Payload | Notes |
|---|---|---|
| `ROOT` | canonical `SidStateRootV1`, binary codec (`kSidBinaryStateMagic`) | All parameters and engine state. Always first. |
| `SETS` | 32-byte serialized `SettingsPanelModel` | Also re-applies the topology (BitPerfect / single SID). |
| `MIX ` | `MixPanelModel` (1264 B) | Loaded only if the size matches, then sanitized. |
| `KIT ` | `KitStateBlob` (1676 B) | Decoded with `kitStateBlobDeserialize`, then sanitized. |
| `DIGM` | `DigiPanelModel` | |
| `DIGB` | `DigiSampleBankBlob` (480 392 B) | Restored only together with `DIGM`, the same rule as the AU. |
| `DIGR` | 6 bytes: `$D418` mode, rate (24-bit), pad root note, pad channel | |
| `OUTM` | 1 byte: pure-SID 1Q1 output mode | |
| `SIDF` | `u16` subtune (little-endian) + the loaded `.sid` file | Written only while a tune is loaded. Loading a state without it unloads any tune left from before, so a restore is deterministic. Added in 0.9.8; older versions skip it. |
| `BYPS` | 1 byte: host bypass | Always written. A state without it loads un-bypassed. Added in 0.9.9; older versions skip it. |
| `PRST` | none (length 0) | Never written by `getState`. It marks a **patch-only state**, the kind stored in the factory `.vstpreset` files: `PRST` + `ROOT` and nothing else. See [Preset states](#preset-states). Added in 0.9.10; older versions skip it. |

**Reading.**

- A truncated chunk stops reading. The chunks read before the cut are kept
  and published, and the load succeeds if a root was found.
- Versions 1–4 are the pre-0.9.5 Phase2 layout: a `u32` version followed by
  one state-root blob. Version 4 uses the state magic; 1–3 use the patch magic.
  They load as a root only; the models keep their defaults.
- `decodeStateRoot` extracts only the root from any version. The controller
  uses it in `setComponentState` to mirror parameters.

### Preset states

A state with the `PRST` chunk (`Vst3KernelHost::isPresetState`) is a patch,
not a project. `loadState` schedules its `ROOT` like a program selection and
returns before the steps that follow a project load, so it:

- keeps the MIX, KIT, DIGI and SETTINGS models (it has none and does not
  republish the current ones);
- keeps the host bypass (`BYPS` absent does not mean "not bypassed" here);
- keeps a loaded `.sid` tune (`SIDF` absent does not unload it).

`setComponentState` in the controller mirrors the root and, for a preset
state, leaves the Bypass parameter alone. The public constants
`kVst3StateVersion`, `kVst3StateTagRoot` and `kVst3StateTagPreset` are in
`source/vst3/arpsid_vst3_kernel_host.h`.

---

## Edit controller

### Parameters

The controller registers all 512 parameters (`kNumParams`) as `RangeParameter`
0..1, in the root unit.

- **Step count** is `normalizedParamStepCount(id)`, the same cardinality
  contract used by AUv2, state repair and the DSP boundary.
- **Units and text** come from `SidParameterPresentation`:
  `getParamStringByValue` formats and `getParamValueByString` parses, with the
  same laws the engine uses (exponential LFO rate, quadratic portamento,
  limiter ms laws, sequencer tempo 20 + 280 × norm, enum and boolean labels).
- **Named values.** Stepped parameters show names, e.g. waveform `PULSE`,
  filter `LOW-PASS`, voice `UNISON`, LFO `S&H`, arp `UP/DOWN`, `2 OCT`, seq
  `PING-PONG`, Hi-Fi `TRANSCENDENCE`. Arp transpose reads `+3 st` and pattern
  length `16 steps`. Hosts accept these names when you type a value. The
  names follow the engine's decode laws (see
  [VST3_EDITOR.md](VST3_EDITOR.md#stepped-values-and-the-decode-law)).
- **Units (groups).**
  - The root unit `ArpSID` holds the factory program list and the Program and
    BankSlot selectors.
  - Below it there is one unit per editor tab (`MAIN` … `DIGI`), in tab order.
    A parameter goes into the first tab that shows it.
  - Last comes `Host MIDI / read-only`, with 13 sub-units, one per MIDI
    controller kind (mod wheel, breath, expression, sustain, sostenuto,
    channel pressure, pitch bend, RPN/NRPN and data entry MSB/LSB), each with
    16 channels. Their titles carry the channel (`Host MIDI Sustain Ch 5`),
    so every title is unique.

  That is 32 units in all. Hosts that show units group the parameters the way
  the editor does.
- **Flags**:
  - Program is `kIsProgramChange | kIsList`.
  - Panic, Virtual Gate and Bank Command are hidden.
  - The 208 host MIDI parameters are hidden but writable (`kIsHidden`, not
    `kIsReadOnly`): hosts write them through the MIDI mapping below, and
    `kIsReadOnly` would tell a host that only the plug-in may change them.
    Up to 0.9.11 they were read-only.
  - Every parameter whose table entry is automatable can be automated.
  - Toggles of the shared set are never marked `kIsBypass`. That flag
    belongs only to the separate `Bypass` parameter (id 1024), which is the
    host's bypass; see [Host bypass](#host-bypass).

### Factory patches

There is one program list (ID 1) with the 180 canonical factory patches,
attached to the root unit.

- **Picking a patch.** A program-list selection, a Program Change mapped by
  the host, a BankSlot write, or a pick in the editor all end up in
  `setParamNormalized(Program|BankSlot)`. The controller then:
  1. sends `LoadFactoryPatch(slot)` to the processor;
  2. builds the same state root locally and mirrors its parameter values
     into the controller (`restartComponent(kParamValuesChanged)`), so the
     host's parameter view follows.

  A guard flag keeps that mirror from starting a second load.
- **Project load.** `setComponentState` decodes the root from the processor's
  state (any version) and mirrors it the same way, without notifying the
  host. It also reads the `BYPS` chunk into the Bypass parameter.
- **Program attributes.** `getProgramInfo` reports `PlugInCategory` =
  `Instrument|Synth` and `PlugInName` = `ArpSID` for every factory program.
- **Preset files.** Hosts whose preset browser reads files never see the
  program list, so the build also writes every factory patch as a
  `.vstpreset` (`arpsid_vst3_presets`, see below) and the release zips and
  installers ship them into the standard VST3 preset folders
  (`<preset root>/Uber Sound Solutions/ArpSID/<Role>/<Patch>.vstpreset`; the
  roots per OS are in [INSTALL.md](INSTALL.md#factory-presets-vst3)).
  - Each file holds a patch-only state (`PRST` + `ROOT`, about 5 KB) as its
    `Comp` chunk, no `Cont` chunk (so loading one does not reset the editor's
    size or tab), and an `Info` chunk with `MediaType`, `PlugInName`,
    `PlugInCategory`, `Name`, `MusicalCategory`/`MusicalInstrument` (from the
    patch role, e.g. `Synth|Bass`, `Drum&Perc`) and `Comment` (the patch
    description).
  - The folders are the patch roles: `Bass`, `Bell`, `Drums`, `Keys`
    (chord role), `Lead`, `Metallic`, `Pad`.
  - `arpsid_vst3_preset_export` (`source/vst3/arpsid_vst3_preset_export.cpp`)
    loads the built bundle through the SDK hosting layer, selects each program
    on the controller, cuts the processor state down to `PRST` + `ROOT`,
    writes it with `PresetFile::savePreset`, then loads every file into a
    fresh instance with `PresetFile::loadPreset` and fails unless the bank
    slot and every persistent parameter match.
- **Parameter text for Bypass.** `getParamValueByString` accepts `On`/`Off`
  and `1`/`0` for the Bypass parameter, so the text the host shows parses
  back (the SDK validator checks this).


**Drum note names.** For the drum programs (DrSID / SID-808 kits: slot 47
and slots 80–149, the programs whose patch enables DrSID),
`hasProgramPitchNames` is true and `getProgramPitchName` returns the General
MIDI drum name of notes 35–81 (`Bass Drum 1`, `Closed Hi-Hat`, …), from
`sid_gm_drum_kit.h`, the table the drum engines play from. Host drum editors
and piano rolls show them. Synth programs name no notes.

### Controller state (`getState` / `setState`)

The controller saves its own small state next to the processor state. It holds
editor settings that are not part of the sound:

| Field | Type | Meaning |
|---|---|---|
| magic | `u32` | `ASEC` (0x41534543) |
| version | `u32` | 1 |
| zoom | `f64` | editor size (1.0 = 1200 × 800 at host scale 1; clamped 0.25–4) |
| tab | `i32` | the tab the editor was left on |

All fields are little-endian. A missing, short or foreign stream leaves the
defaults (size 1.0, MAIN tab), so older projects open normally.

### Track information (`IInfoListener`)

Hosts that support channel context (Cubase, Nuendo, Studio One, Reaper and
others) call `setChannelContextInfos` with the track name and colour. The
controller keeps both, and the editor header shows the name in the track's
colour (darkened host colours are lightened so they stay readable).

### MIDI mapping (`IMidiMapping`)

The mapping applies per channel on MIDI bus 0:

| MIDI | Parameter |
|---|---|
| CC1 mod wheel | Host Ctrl Mod Wheel, per channel |
| CC2 breath | Host Ctrl Breath, per channel |
| CC11 expression, CC4 foot | Host Ctrl Expression, per channel (same law as the AU raw-MIDI path) |
| CC7 volume | Master Volume |
| CC64 sustain, CC66 sostenuto | Host Ctrl Sustain / Sostenuto, per channel |
| channel aftertouch, pitch bend | Host Ctrl Channel Pressure / Pitch Bend, per channel |
| CC101/100 RPN select, CC99/98 NRPN select, CC6/38 data entry | Host Ctrl RPN / NRPN / Data Entry MSB and LSB, per channel |
| CC70–77 (e.g. AKAI MPK mini K1–K8) | Filter Cutoff, Resonance, Drive, Env Amount, LFO Amount, Master Volume, Reverb Mix, Forensic Intensity (`sid_midi_cc_mapping.h`, the same targets as AU and Standalone) |

**RPN 0 (pitch-bend range).** The RPN / NRPN / Data Entry parameters run
the MIDI RPN state machine, shared with the raw-MIDI path of AU and
Standalone (`sid_runtime_parameter_services.h`, `sid_runtime_render_surface.h`):

- selecting an RPN (CC101/100) changes nothing by itself;
- Data Entry (CC6 semitones, CC38 cents) writes the selected RPN; with RPN 0
  selected it sets that channel's pitch-bend range (0–48 semitones), which
  is how MPE zones and most DAWs set bend ranges;
- selecting an NRPN (CC99/98) deselects the RPN, so Data Entry meant for an
  NRPN never changes the bend range; Reset All Controllers (CC121) deselects
  it too (RP-015).

Up to 0.9.11 the VST3 mapping did not route these CCs at all, the raw-MIDI
path ignored them, and the parameter path took the channel from the low four
bits of the parameter id; the host-control blocks start at id 286, which is
not a multiple of 16, so a bend range sent on one channel landed on another.
`RpnPitchBendRangeTests` covers both paths.

The per-channel host-controller parameters are the "host-driven" block in
[PARAMETER_REFERENCE.md](PARAMETER_REFERENCE.md#host-driven-and-read-only-parameters).

---

## Processor ↔ controller messages

Messages travel as VST3 `IMessage` over `IConnectionPoint`. Hosts deliver them
on the main thread, never inside `process()`.

| ID | Direction | Attributes | Effect |
|---|---|---|---|
| `ArpSID.LoadFactoryPatch` | controller → processor | `slot` | `Vst3KernelHost::loadFactorySlot` |
| `ArpSID.UiMidi` | controller → processor | `status`, `data1`, `data2` | `injectMidi` (editor keyboard, ALL NOTES OFF) |
| `ArpSID.KernelHost` | processor → controller | `ptr`, `pid` | The address of the processor's `Vst3KernelHost`. The controller accepts it only if `pid` is its own process ID. `ptr` 0 (sent from the processor's `disconnect` and `terminate`) withdraws it: the controller and editor stop using the engine before it goes away, whatever order the host tears down in. The editor looks the pointer up on every use and never keeps it. |
| `ArpSID.RequestKernelHost` | controller → processor | — | Resend `KernelHost` (sent from `connect`, in case the processor connected first). |

With the kernel host, the editor can read telemetry and edit the non-parameter
models directly, as the AU editor does through its adapter. If the host runs
the processor in another process, the pid check fails. The editor then works
on parameters only, and says so on the panels that need the engine.

---

## Editors

`createView("editor")`:

- **macOS** (`ARPSID_VSTGUI_EDITOR` not defined): `ArpSIDVSTGUIEditor`
  (`source/gui/arpsid_vstgui_editor.cpp`) hosts the native Cocoa GUI that the
  AU uses (`ArpSIDViewController`). `source/gui/arpsid_vst_cocoa_bridge.mm`
  implements the selectors the view controller calls on its AU adapter, on
  top of the processor's `Vst3KernelHost` (`ArpSIDVSTDebugAdapter`). That
  covers:
  - SETTINGS, MIX, KIT and DIGI, including user samples, `$D418` policy and
    pad mapping;
  - SIDCORE and diagnostics, and pure SID capture;
  - the C64 hub and the SID file;
  - bank import and export, and factory preset selection.

  So every Cocoa tab works in the VST3.
- **Windows and Linux** (`ARPSID_VSTGUI_EDITOR=1`):
  `arpsidCreateCrossPlatformEditor` (`source/gui/vstgui/arpsid_vstgui_plugview.cpp`)
  creates the VSTGUI editor described in [VST3_EDITOR.md](VST3_EDITOR.md).
  - **Sizing.** `canResize` is true. `checkSizeConstraint` fits the largest
    3:2 size into the offered rect, clamped to 0.5×–3× of 1200 × 800.
    `onSize` sets the frame zoom. The host's content scale multiplies the
    user's size.
  - **Remembered size and tab.** The controller keeps the size
    (`arpsidControllerEditorZoom`) and the tab (`arpsidControllerEditorTab`)
    and saves both in its state, so a reopened editor, and a reopened
    project, come back at the same size on the same tab.
  - **Parameter menu.** Right-clicking a parameter control asks the host for
    its menu (`IComponentHandler3::createContextMenu`: automation, MIDI
    learn and so on, depending on the host) and adds `Reset to Default`. The
    menu opens at the click, scaled by the editor zoom. Hosts without
    `IComponentHandler3` get no menu; the click is passed on.
  - **Parameter under the mouse (`IParameterFinder`).** The view answers
    `findParameter` with the parameter of the control under the given point
    (scaled by the editor zoom), so host "learn" functions (quick controls,
    "last touched" parameter) work.
  - **Knob mode (`IEditController2::setKnobMode`).** The host's knob mode
    preference sets how knobs follow the mouse: circular (jump to the mouse
    angle around the knob), relative circular (turn by the angle moved) or
    linear (vertical drag). The editor stays linear until a host sets a
    mode; Shift always gives the fine linear drag. `openHelp` and
    `openAboutBox` are not supported (`kResultFalse`).
  - **Computer keyboard.** The editor view is the frame's keyboard hook.
    Letter keys play notes (see [VST3_EDITOR.md](VST3_EDITOR.md#playing-from-the-computer-keyboard));
    keys with Ctrl, Alt or Cmd go to the host.
  - **Linux event loop.** VSTGUI on Linux has no event loop of its own; its X
    connection and timers run on the host's `Linux::IRunLoop`, which hosts
    serve from the `IPlugFrame`. The editor:
    1. offers `kPlatformTypeX11EmbedWindowID` only (VSTGUI is built without
       Wayland, so hosts use X11/XWayland);
    2. on `open`, points a module-wide forwarding run loop (`HostRunLoop`) at
       the plug frame's `IRunLoop`, installs it in VSTGUI once, and opens the
       frame with an `X11::FrameConfig` carrying it. Only that config makes
       VSTGUI open its X connection; before this fix the frame was opened
       without it and the first X call crashed on a null connection.
       VSTGUI keeps the first run loop it is given for the life of the module,
       hence the forwarding object: each registration remembers the host loop
       it went to, so it is removed from the right loop even after another
       editor retargeted the forwarder;
    3. refuses to open (`attached` returns `kResultFalse`) when neither the
       plug frame nor the factory host context provides a run loop, instead of
       crashing;
    4. when the last editor closes, keeps the X connection open across the
       frame teardown, finishes cairo's xcb device for it, and only then lets
       VSTGUI disconnect. cairo caches per-connection state keyed by the
       `xcb_connection_t` address, and the next editor's `xcb_connect` often
       gets the same address back; without the finish, reopening the editor
       asserted in cairo (`_get_screen_index`).
  - **`attached` result.** `VSTGUIEditor::attached` reports success even when
    `open` fails; the ArpSID editor returns `kResultFalse` then, so the host
    does not show an empty window.
- **Windows and Linux without the editor** (`-DARPSID_VST3_EDITOR=OFF`):
  `createView` returns no view, and hosts show their generic parameter UI.
  `source/gui/arpsid_vst_headless_bridge.cpp` provides the Cocoa entry points
  as stubs on those platforms.

---

## Threading

| Thread | What runs there |
|---|---|
| Audio (`process`) | `readBypass_`, `collectEvents_`, `readTransport_`, `applyBypass_`, DIGI capture, `Vst3KernelHost::render` → `ArpSIDDSPKernel::processBlock`. No locks, no allocation: the host test counts both (it interposes `operator new` and `pthread_mutex_lock` on Linux) over blocks with notes, automation, bypass and editor MIDI, and requires zero. The whole callback runs under the kernel's realtime guard (`SidRealtimeScope`), which counts lock and allocation violations. Scheduled roots and GUI models arrive through the kernel's mailboxes; editor MIDI and parameter intents through its multi-producer rings. |
| Main / UI | Controller calls, messages, `setState` / `getState`, the editor timer, model edits, telemetry reads, SID file loads. |

- **Models.** `modelMutex_` guards the models on the non-realtime side. The
  render thread never takes it; it reads the published copies.
- **Pending root.** The pending-root sequence counters are atomics.
  `pendingRootMutex_` guards only the non-realtime copy of the pending root.
- **DIGI capture.** Arming and stopping (UI) and the capture write (audio)
  hand over through two atomics (armed, busy); the buffer is allocated when
  arming, never on the audio thread, and stopping yields while an audio
  block finishes its write.
- **Linux editor.** X events and timers run on the host's `IRunLoop` (UI
  thread); the editor offers X11 embedding only (VSTGUI has no Wayland
  backend), so Wayland hosts embed it through XWayland.
- **Kernel.** The kernel's own rules are in
  [REALTIME_OWNERSHIP.md](REALTIME_OWNERSHIP.md).

---

## Building and installing

```bash
# Linux build dependencies for the editor (Debian/Ubuntu): see README.md
cmake -S . -B build-vst3 -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DARPSID_BUILD_VST3=ON -Dvst3sdk_SOURCE_DIR=$HOME/vst3sdk
cmake --build build-vst3 --target arpsid_vst3                # also runs the Steinberg validator
cmake --build build-vst3 --target arpsid_vst3_host_check     # host integration test
cmake --build build-vst3 --target arpsid_vst3_editor_check   # render every editor tab (Windows/Linux)
cmake --build build-vst3 --target arpsid_vst3_presets         # factory .vstpreset files -> build-vst3/VST3 Presets
cmake --build build-vst3 --target arpsid_vst3_install_user   # bundle to ~/.vst3 (Linux) or the user VST3 folder, presets to ~/.vst3/presets or Documents/VST3 Presets
sudo cmake --install build-vst3 --prefix /usr                # /usr/lib/vst3 and /usr/share/vst3/presets (Linux)
./build.sh --fetch-vst3-sdk --package-vst3 --no-tests         # release-style zip + installer
```

Installing released builds: [INSTALL.md](INSTALL.md).

- The VST3 SDK is v3.8.1 (tag `v3.8.1_build_84`). `scripts/fetch_vst3_sdk.sh`
  or `-DARPSID_FETCH_VST3SDK=ON` (CMake FetchContent) gets that version;
  `ARPSID_VST3SDK_TAG` overrides it.
- `-DARPSID_VST3_EDITOR=OFF` builds the Windows/Linux VST3 without VSTGUI.
- **Linux.** Before VSTGUI is configured, CMake checks every editor package
  with pkg-config and names the missing ones.
  `scripts/linux/install_build_deps.sh` installs them with apt, dnf, pacman or
  zypper.
- **Windows.** The C/C++ runtime is linked statically everywhere: ArpSID, the
  SDK (`SMTG_USE_STATIC_CRT`) and VSTGUI (`CMAKE_MSVC_RUNTIME_LIBRARY`). CI
  checks that the module does not import `VCRUNTIME140`/`MSVCP140`.
- VSTGUI is built from the SDK's `vstgui4` without its standalone, tools,
  uidescription scripting, OpenGL or Wayland parts. See
  [VST3_EDITOR.md](VST3_EDITOR.md#platform-notes).
- On Windows the bundle goes to `C:\Program Files\Common Files\VST3\`.

---

## Tests

| Test | Checks |
|---|---|
| Steinberg `validator` (runs during every `arpsid_vst3` build) | 47 SDK conformance tests: buses, state, parameters, process formats, flush, variable block size, and more. |
| `arpsid_vst3_host_check` / `Vst3HostIntegrationTests` (`source/tests/vst3_host_integration_tests.cpp`) | Loads the built bundle like a host and checks each of these: <ul><li>both classes instantiate and connect;</li><li>Program is a 180-entry program-change list;</li><li>the MIDI bus has 16 channels, and the DIGI capture input is an auxiliary bus, inactive by default;</li><li>units: 32 units, each parameter in an existing unit, Program in the root, and <code>selectUnit</code> remembered;</li><li>on Windows and Linux, editor sizing: the 3:2 constraint, the minimum size, and a reopened view keeping its size;</li><li>on Linux with a <code>DISPLAY</code> (CI: Xvfb, and <code>ARPSID_REQUIRE_X11_EDITOR=1</code> makes it mandatory), the editor in a real X11 window: X11 only (no Wayland claim), attach, the X connection and timers on the host's <code>IRunLoop</code>, <code>IParameterFinder</code> naming the parameter under the mouse at 1× and after a resize, a live resize, detach leaving no handlers behind, the same again for a second open, and a refusal (no crash) for a host frame without a run loop;</li><li>tempo, transport and musical position are requested;</li><li>the MIDI mapping leaves unmapped CCs alone and routes CC 101/100/99/98/6/38 to each channel's RPN/NRPN/Data Entry parameters;</li><li>the 208 host MIDI parameters are hidden, host-writable and uniquely titled;</li><li><code>IEditController2</code> accepts the three knob modes;</li><li>released output settles to exact zero and is flagged silent;</li><li>selecting a program refreshes the host and the controller mirror matches the processor state;</li><li>editor-keyboard (UiMidi) and host note-ons produce audio, and Master Volume automation reaches the engine;</li><li>a v5 state loads into a second instance;</li><li>a legacy v4 state is accepted by processor and controller;</li><li>Bypass is a <code>kIsBypass</code> on/off parameter in the root unit, silences the output, is saved in the state and read back by a fresh controller, and un-bypassing restores the sound;</li><li>64-bit processing renders a note with finite samples, and an oversized 64-bit block does not overrun;</li><li>the controller state round-trips the editor size and tab, and a foreign stream is ignored;</li><li><code>IInfoListener</code> accepts a track name and colour;</li><li>programs report the <code>Instrument|Synth</code> category;</li><li>a patch-only preset state changes the processor's and the controller's patch but leaves bypass alone, and a later project save is a full state again.</li></ul> |
| Realtime contract (in `arpsid_vst3_host_check`) | On Linux an allocation probe (the test's `operator new`) counts heap allocations inside `process()` during a busy block of unsorted notes and 512 automation points: it must be 0. A note-off in a block with more than 4096 automation points still releases the note. An 8192-frame 32-bit block starts a note at frame 6000 exactly there. An oversized 64-bit block renders the same audio as announced-size blocks. The test also prints the render cost as a share of real time at 32 to 2048-frame blocks. |
| `arpsid_vst3_presets` / `Vst3FactoryPresetExport` | Writes all 180 factory `.vstpreset` files and reloads each into a fresh instance: the bank slot and every persistent parameter must match. |
| `arpsid_vst3_editor_check` | The offscreen editor render ([VST3_EDITOR.md](VST3_EDITOR.md#tests)). |
| `Vst3KernelHostStateTests` (`source/tests/vst3_kernel_host_state_tests.cpp`, SDK-free, runs in every build) | The v5 state keeps the models, the C64 tune and its subtune. A tune-less state unloads a tune. A restored tune can switch subtune. A truncated state keeps what came before the cut. A DIGI capture round trip works (arm, feed, stop, then the slot plays the take). Bypass is saved, restored, cleared by an un-bypassed state, read by `decodeBypass`, and never set by a legacy state. A patch-only preset state is small, recognised by `isPresetState`, plays its patch, and keeps the loaded tune, the MIX model and bypass. |
| `EditorLayoutCoverageTests`, `ParameterReferenceDocTests` | Editor coverage and the decode law, and that the parameter reference is up to date. |

CI runs all of these on Linux x86_64/aarch64 and Windows x64/arm64/x86. On macOS
it runs the validator and host test for the universal bundle (see the README's
CI table).
