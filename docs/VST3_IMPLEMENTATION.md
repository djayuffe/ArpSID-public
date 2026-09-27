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
| `source/arpsid_controller.cpp` | `ArpSIDControllerPhase3` | `EditController` + `IMidiMapping` + `IUnitInfo` (program list). It is compiled into `factory.cpp`. |
| `source/arpsid_vst_messages.h` | — | Message IDs and attribute names between processor and controller. |
| `source/factory.cpp`, `source/plugin_ids.{h,cpp}` | — | The factory and the two class IDs. |
| `source/au3/ArpSIDKernelTelemetryFill.h` | — | Kernel → `ArpSIDTelemetry` projection, shared with the AU adapter. |

---

## Plug-in identity and buses

| | Value |
|---|---|
| Processor class | `ArpSID`, FUID `A1B2C3D4-E5F60718-9A0B1C2D-3E4F5A6B`, category `ARPSID_PLUGIN_CATEGORY` (`include/arpsid/version.h`) |
| Controller class | `ArpSID Controller`, FUID `B2C3D4E5-F6071829-A0B1C2D3-E4F5A6B7` |
| Audio | no inputs; one output bus, stereo (mono accepted) |
| Events | one input bus, `MIDI In`, 16 channels |
| Sample size | 32-bit float |
| Tail | `kInfiniteTail` (release, reverb and delay can ring on) |
| Process context | tempo, transport state, project time (music), cycle, bar position, time signature |

The class IDs are unchanged since the first VST3 release, so projects saved
with any earlier version open with the current engine.

---

## Processor

### `initialize`

The processor adds the buses and the process-context requirements, then
queues factory patch 0. That makes a fresh instance start from a known patch;
the patch is applied at the first rendered block.

### `setupProcessing` / `setActive`

`setupProcessing` calls `Vst3KernelHost::setup(sampleRate, maxSamplesPerBlock)`.
A re-setup (new sample rate or block size) keeps the audible state: the host
snapshots every parameter and the sticky preset slot, sets the kernel up again,
restores the snapshot and republishes the GUI models. `setActive(false)` resets
the kernel (voices, effect tails).

### `process`

1. **Parameter flush.** With `numSamples == 0` there is no audio. The last
   value of each changed parameter is staged through the kernel's non-realtime
   parameter intent.
2. **Events** (`collectEvents_`) go into a preallocated `TimedEvent` array;
   `process` does not allocate.
   - Every point of every parameter queue becomes an `EventKind::ParameterSet`
     at its sample offset, so automation is sample-accurate. Program and
     BankSlot are skipped: the controller turns those into a patch load (see
     below).
   - Note on and note off become `NoteOn` / `NoteOff` with channel, pitch,
     velocity and the host note ID. A note-on with velocity 0 is a note-off.
   - Poly pressure becomes `PolyPressure`.
   - Events are clamped to the block, given a monotonic arrival order, and
     stable-sorted by offset.
3. **Transport** (`readTransport_`) maps `ProcessContext` to the kernel's
   `TransportState`: tempo, musical position, playing, cycle active, and loop
   start/end. The render sample rate always comes from `setupProcessing`.
4. **Render.** The output buffers are cleared, then `Vst3KernelHost::render`
   runs.
5. **Silence flags.** A channel that is exactly zero for the whole block is
   flagged silent, so hosts can skip downstream processing.

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

- If the block fits the kernel's `kMaxFramesPerBlock` (4096), it is one
  `processBlock` call (stereo), `processBlockMono` (mono) or an output-less
  call (no bus).
- A larger host block is split into kernel-sized chunks. For each chunk the
  event offsets are rebased and the musical position is advanced by
  `start × bpm / (sampleRate × 60)`, the same rule as the AUv3 wrapper. The
  chunk event scratch is preallocated.

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
| `loadSidFile`, `unloadSidFile`, `isSidFileLoaded` | The PSID/RSID player. |
| `c64ControlHubCommand(n)` | 1 boot, 2 start, 3 stop, 4 reset, 5 load projection bootstrap, 6/7 VIC-II fast on/off, 8/9 CPU fast on/off. |
| `setPureSid1Q1OutputMode` | Pure-SID output mode (saved in the `OUTM` chunk). |
| `injectMidi` | Queue a MIDI message from the editor keyboard. |
| `readTelemetry(out, scopes, c64)` | Fill `ArpSIDTelemetry`. The scope and C64 snapshot sections are copied only on request. |
| `pollNonRealtime` | Perform drum-bridge factory-slot loads that render deferred (called from the editor timer). |

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

**Reading.**

- A truncated chunk stops reading. Whatever was already applied is kept, and
  the load succeeds if a root was found.
- Versions 1–4 are the pre-0.9.5 Phase2 layout: a `u32` version followed by
  one state-root blob. Version 4 uses the state magic; 1–3 use the patch magic.
  They load as a root only; the models keep their defaults.
- `decodeStateRoot` extracts only the root from any version. The controller
  uses it in `setComponentState` to mirror parameters.

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
- **Flags**:
  - Program is `kIsProgramChange | kIsList`.
  - Panic, Virtual Gate and Bank Command are hidden.
  - Every parameter whose table entry is automatable can be automated.
  - Toggles are never marked `kIsBypass`, because that flag belongs to the
    host's bypass.

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
  host.

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
| CC70–77 (e.g. AKAI MPK mini K1–K8) | Filter Cutoff, Resonance, Drive, Env Amount, LFO Amount, Master Volume, Reverb Mix, Forensic Intensity (`sid_midi_cc_mapping.h`, the same targets as AU and Standalone) |

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
| `ArpSID.KernelHost` | processor → controller | `ptr`, `pid` | The address of the processor's `Vst3KernelHost`. The controller accepts it only if `pid` is its own process ID. |
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
  `source/gui/arpsid_vst_headless_bridge.cpp` provides the Cocoa entry points
  as stubs on those platforms.

---

## Threading

| Thread | What runs there |
|---|---|
| Audio (`process`) | `collectEvents_`, `readTransport_`, `Vst3KernelHost::render` → `ArpSIDDSPKernel::processBlock`. No locks, no allocation. Scheduled roots and GUI models arrive through the kernel's mailboxes. |
| Main / UI | Controller calls, messages, `setState` / `getState`, the editor timer, model edits, telemetry reads, SID file loads. |

- **Models.** `modelMutex_` guards the models on the non-realtime side. The
  render thread never takes it; it reads the published copies.
- **Pending root.** The pending-root sequence counters are atomics.
  `pendingRootMutex_` guards only the non-realtime copy of the pending root.
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
cmake --build build-vst3 --target arpsid_vst3_install_user   # copy to ~/.vst3 (Linux) or the user VST3 folder
sudo cmake --install build-vst3 --prefix /usr                # /usr/lib/vst3 (Linux)
```

- The VST3 SDK is v3.8.1 (tag `v3.8.1_build_84`).
- VSTGUI is built from the SDK's `vstgui4` without its standalone, tools,
  uidescription scripting, OpenGL or Wayland parts. See
  [VST3_EDITOR.md](VST3_EDITOR.md#platform-notes).
- On Windows the bundle goes to `C:\Program Files\Common Files\VST3\`.

---

## Tests

| Test | Checks |
|---|---|
| Steinberg `validator` (runs during every `arpsid_vst3` build) | 47 SDK conformance tests: buses, state, parameters, process formats, flush, variable block size, and more. |
| `arpsid_vst3_host_check` / `Vst3HostIntegrationTests` (`source/tests/vst3_host_integration_tests.cpp`) | Loads the built bundle like a host and checks each of these: <ul><li>both classes instantiate and connect;</li><li>Program is a 180-entry program-change list;</li><li>the MIDI bus has 16 channels;</li><li>tempo, transport and musical position are requested;</li><li>the MIDI mapping leaves unmapped CCs alone;</li><li>selecting a program refreshes the host and the controller mirror matches the processor state;</li><li>editor-keyboard (UiMidi) and host note-ons produce audio, and Master Volume automation reaches the engine;</li><li>a v5 state loads into a second instance;</li><li>a legacy v4 state is accepted by processor and controller.</li></ul> |
| `arpsid_vst3_editor_check` | The offscreen editor render ([VST3_EDITOR.md](VST3_EDITOR.md#tests)). |
| `EditorLayoutCoverageTests`, `ParameterReferenceDocTests` | Editor coverage and the decode law, and that the parameter reference is up to date. |

CI runs all of these on Linux x86_64/aarch64 and Windows x64/arm64. On macOS
it runs the validator and host test for the universal bundle (see the README's
CI table).
