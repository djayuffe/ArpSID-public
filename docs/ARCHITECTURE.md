# ArpSID architecture

How ArpSID is put together: one realtime kernel that every plug-in format and
the Standalone app share, the engines inside it, and the threading, state,
GUI-model and telemetry contracts around it. The deeper documents cover single
topics:

| Topic | Document |
|---|---|
| Render thread vs. producer side | [REALTIME_OWNERSHIP.md](REALTIME_OWNERSHIP.md) |
| Rollback-safe C64 render transactions | [REALTIME_ROLLBACK_JOURNAL.md](REALTIME_ROLLBACK_JOURNAL.md) |
| GUI tabs | [TAB_ARCHITECTURE.md](TAB_ARCHITECTURE.md) |
| VST3 wrapper | [VST3_IMPLEMENTATION.md](VST3_IMPLEMENTATION.md) |
| VST3 editor (Windows/Linux) | [VST3_EDITOR.md](VST3_EDITOR.md) |
| Parameters | [PARAMETER_REFERENCE.md](PARAMETER_REFERENCE.md) |
| DIGI `$D418` sample format | [D418_NIBBLE_SPEC.md](D418_NIBBLE_SPEC.md) |
| PSID/RSID files | [SID_FILE_FORMAT_NOTES.md](SID_FILE_FORMAT_NOTES.md) |
| C64 player claims | [C64_EXACTNESS_BOUNDARIES.md](C64_EXACTNESS_BOUNDARIES.md) |

---

## Layers

```
 ┌──────────── wrappers (host-facing) ─────────────────────────────────────────────┐
 │ AUv2 component   AUv3 app extension   Standalone app   VST3 processor+controller│
 │ source/au2       source/au3            source/au3       source/vst3, controller │
 └───────┬──────────────────┬──────────────────┬────────────────┬──────────────────┘
         │  TimedEvent[] (sample-stamped) + TransportState + parameter intents     │
         ▼                                                                          │
 ┌──────────── ArpSIDDSPKernel (source/au3/ArpSIDDSPKernel.hpp) ──────────────────┐│
 │ ingress rings · state-root mailbox · GUI-model mailboxes · telemetry publish   ││
 │ ┌──────── canonical runtime (include/arpsid/core/sid_runtime_*) ─────────────┐ ││
 │ │ event queue & materializer · voice tokens/allocator · register shadow       │ ││
 │ │ fractional-cycle render · post-FX timeline                                  │ ││
 │ └─────────────────────────────────────────────────────────────────────────────┘ ││
 │ engines (include/arpsid/engines): BitPerfect · single SID · SID register ·      ││
 │ arpeggiator · sequencer · DrSID · SID-808 · DIGI sampler / $D418 stream         ││
 │ C64 machine (include/arpsid/core/c64_*): 6510 · VIC-II · CIA · PLA · SID bridge ││
 │ post: MIX FX chain · reverb · limiter · Hi-Fi Transcendence                     ││
 └──────────────────────────────────────────────────────────────────────────────────┘│
         ▲ published telemetry (ArpSIDTelemetry)          GUI models (SETTINGS/MIX/  │
         └──────────────── editors: Cocoa (macOS), VSTGUI (Windows/Linux) ──────────┘
```

**Authority rules**

- The kernel owns behaviour. Wrappers translate host events into the kernel's
  canonical ingress and do not invent policy of their own.
- Host sample offsets reach the kernel intact: as `TimedEvent.sampleOffset`, or
  through `enqueueMidiIntent` or an exact compatibility shim. Intra-block
  timing is never collapsed.
- Patch and program storage is zero-based inside; users see one-based labels
  (`001`–`180`).
- Invalid or extreme host values (sample rate, block size, parameter values)
  are sanitized before they become runtime state. Scratch buffers are sized
  off the render thread, with headroom.

---

## Source tree

| Path | Contents |
|---|---|
| `include/arpsid/core/` | The canonical runtime: `sid_runtime_*.h` (model, event materializer, host block, register shadow, fractional render, voice policy, state apply), event queue and timing, voice tokens and allocator, SID chip, filter and envelope cores, analogue calibration, post-FX timeline, Hi-Fi Transcendence, parameter presentation, MIDI CC mapping, state codec and schema, ownership mailbox, scope triple buffer, and the C64 machine (`c64_*.h`: 6510, VIC-II, CIA, PLA, bus, open bus, PHI2 scheduler, PSID runtime, ROM loader, SID bridge and readback). |
| `include/arpsid/engines/` | `bitperfect_engine.h` (the classic multi-chip SID synth), `single_sid_three_voice_engine.h`, `sid_register_engine.h`, `arpeggiator.h`, `voice_manager.h`, `drsid_engine.h` and its wavetable program runner, `sid808_engine.h` and its GM projection, the drum router, bridge and stem mixer, `digi_sampler_engine.h`, `digi_d418_stream_engine.h`. |
| `include/arpsid/modulation/` | `lfo.h`. |
| `include/arpsid/audio/` | `mix_fx_processors.h`: the MIX insert effects (EQ, transient, compressor, saturator, bitcrusher). |
| `include/arpsid/gui/` | GUI models and contracts: SETTINGS, MIX, KIT (panel, step grid, voice and assign config, state blob), DIGI (panel model, sample bank, record limits), SIDCORE, tab architecture, theme palette, language strings, telemetry scope demand, GUI → realtime projection. |
| `include/arpsid/patchbank/` | The factory bank (`forensic_patch_bank.h`), the factory DrSID, SID-808 and DIGI kits and their parameter bridges, patch-bank I/O. |
| `source/parameter_ids.h` | The 512 parameter IDs, names, defaults, step counts and sanitize rules. |
| `source/au3/` | `ArpSIDDSPKernel.hpp` (the kernel), the AU adapter, the AUv3 audio unit, the Cocoa view controller (all editor tabs), the state serializer, canonical events, the sequencer engine, the mod matrix, the Standalone host app, telemetry fill. |
| `source/au2/` | The AUv2 component (five flavors). |
| `source/vst3/`, `source/arpsid_controller.cpp`, `source/factory.cpp` | The VST3 wrapper. |
| `source/gui/` | The VST3 editors: the Cocoa bridge (macOS) and `vstgui/` (Windows/Linux). |
| `source/common/arpsid_telemetry_snapshot.h` | `ArpSIDTelemetry`, the telemetry snapshot every wrapper and editor shares. |
| `source/tests/`, `scripts/` | The test suite (about 480 CTest tests), and the build, validation and guard scripts. |

---

## The kernel

`ArpSIDDSPKernel` is a self-contained C++ class. It uses no VST3 or AU
headers, so it compiles into Objective-C++ AU units and into the portable VST3
alike.

**Thread contract**

| Call | Thread |
|---|---|
| `setParameter` / `getParameter`, `enqueueParameterIntent`, `pushMidi`, `schedulePendingStateRestore`, `publishGuiRealtimeModels`, `readTelemetry`, `loadPsidData`, C64 hub commands | any non-realtime thread: ingress only, no direct runtime mutation |
| `processBlock` / `processBlockMono` | the render thread only, never concurrent |
| `setup` / `reset` | must not overlap with rendering |

**One block** (`processBlock`)

1. **Drain the mailboxes**: a pending state restore (a preset or project load,
   applied before any event), a pending PSID handoff, a pending audio-engine
   topology change.
2. **Chunk.** A block longer than `kMaxFramesPerBlock` (4096) is split. The
   cross-thread MIDI and parameter rings are drained once for the whole
   parent block, then sliced per chunk, so a queued event keeps its position.
3. **C64 player branch.** When a .sid tune is playing, the C64 machine is the
   player authority. It renders the block and returns, after the minimal
   post-parameter work it needs.
4. **Transport and sequencer sync**: host BPM and position, sequencer enable,
   swing and tempo.
5. **Ingress**: UI and CoreMIDI queue events, then the host `events[]`, through
   the same `dispatchCanonicalIngressEvent_` path. Host notes are live input
   even while the transport is stopped. Program and BankSlot never act as
   timed audio events; presets arrive as state roots.
6. **Canonical render.** Host, UI and sequencer events are put in canonical
   order and the block is rendered in slices between event boundaries:
   - the KIT sequencer fires drum notes on step boundaries;
   - portamento and glide are scheduled as SID register writes;
   - the fractional-cycle SID render consumes the register write queue.
7. **Post**: the MIX FX chain, reverb, limiter and Hi-Fi chain on the exact
   canonical timeline (automation at sample N affects sample N and later), a
   NaN guard, then telemetry and SIDCORE publication. Pure-SID 1Q1 output mode
   bypasses all post-engine processing.
8. **Stuck-note safety net.** Gated poly voices are reconciled against the
   host's held-key mirror, with a two-block grace period.

Blocks with no output bus still run the full pipeline into fixed scratch, so
the engine state keeps advancing.

---

## Engines

| Engine | What it does | Key sources |
|---|---|---|
| **BitPerfect** | The classic SID synth. Three VCOs per chip (8 waveform selections), pulse width and PWM, sync and ring mod, the multimode filter, ADSR, glide and portamento laws, voice modes (poly, mono, legato, unison), and the legacy 8-chip topology. | `bitperfect_engine.h`, `sid_chip.h`, `sid_filter_core.h`, `sid_envelope_core.h`, `sid_portamento_law.h` |
| **Single SID** | One authentic 3-voice chip. Selected by SETTINGS → Topology. | `single_sid_three_voice_engine.h` |
| **SID register / Synth Mode** | Direct `$D400–$D41C` register synthesis. Canonical voice tokens keep host note IDs; anonymous or replayed notes get stable synthetic identities. | `sid_register_engine.h`, `sid_voice_token*.h`, `sid_runtime_register_*` |
| **Arpeggiator** | Up, down, up/down, down/up, random, pattern and chord modes; 1–4 octaves; rate, gate, swing, hold, latch, transpose, random, pattern length, glide legato. Internal gates are written into the canonical event queue at exact sample offsets. | `arpeggiator.h` |
| **Sequencer** | 32 steps (note, velocity, gate), forward, reverse, ping-pong or random, swing, internal tempo or host-following. Its step boundaries are shared by melodic events, KIT and DIGI. | `ArpSIDSequencerEngine.h` |
| **DrSID** | Drums as register micro-programs on the SID. Two machine models (SID-authentic drum core, Analog X0X-8); kick, snare, closed and open hat, clap, cowbell, tom, rim. | `drsid_engine.h`, `drsid_wavetable_program_runner.h`, `drsid_kit_compiler.h` |
| **SID-808** | An x0x-style analog projection with staged shapes (kick pitch drop, tom pitch, hat ring and tail, clap burst train, cowbell partials). | `sid808_engine.h`, `sid808_gm_projection.h` |
| **Drum routing** | General-MIDI channel-10 drums reach DrSID, SID-808 or DIGI per the KIT. | `drum_engine_router.h`, `drum_engine_host_bridge.h`, `sid_gm_drum_map.h` |
| **DIGI** | 8 sample slots, 32-step patterns and pads. Samples play as 4-bit `$D418` volume writes through an isolated SID (AUTH: on the emulated C64 bus, PHI2-timed; FAST: a private SID), so they cannot disturb the main SID. | `digi_sampler_engine.h`, `digi_d418_stream_engine.h`, [D418_NIBBLE_SPEC.md](D418_NIBBLE_SPEC.md) |
| **C64 player** | A cycle-exact PHI2 machine that runs PSID and RSID tunes. It has an NMOS 6510, VIC-II, two CIAs, the PLA and memory matrix, open bus, a SID bridge and readback. RSID needs user ROMs. | `c64_phi2_machine.h`, `c64_psid_runtime.h`, [C64_EXACTNESS_BOUNDARIES.md](C64_EXACTNESS_BOUNDARIES.md) |
| **Forensic model** | Optional analog imperfections: clock jitter, supply ripple, thermal drift, voice crosstalk, external bleed, die temperature, supply voltage, a per-chip seed, filter ohmic behaviour, ADC and bus effects. | `sid_runtime_forensic_config.h`, `sid_measured_*`, `sid_analogue_calibration.h` |
| **Modulation** | 4 LFOs, 8 macros and a mod matrix. There are 9 destinations and 21 sources: LFOs, velocity, note, key follow, wheel, bend, aftertouch, poly pressure, random, macros and the envelope. | `lfo.h`, `sid_mod_matrix_types.h`, `ArpSIDModMatrix.h` |
| **Post** | The MIX FX chain, reverb, limiter, and the Hi-Fi Transcendence chain (oversampling 4/8/16×, tape, warmth, exciter, width, diffuser). | `mix_fx_processors.h`, `sid_postfx_timeline.h`, `sid_hifi_transcendence.h` |

---

## Parameters

There are 512 parameters (`kNumParams`), defined once in
`source/parameter_ids.h`: the ID, name, unit, default, automatable flag, and the
step count via `normalizedParamStepCount`. Every wrapper publishes the same
IDs; the VST3 parameter ID equals the AU parameter address.

- **Text.** `SidParameterPresentation` formats and parses the text, following
  the laws the engine renders with.
- **Stepped values.** The engine decodes them per parameter. Most round; a
  few use floor bins (see
  [VST3_EDITOR.md](VST3_EDITOR.md#stepped-values-and-the-decode-law)).
- **Reference.** [PARAMETER_REFERENCE.md](PARAMETER_REFERENCE.md) is generated
  from these tables and checked by a test.

---

## State

- **Canonical state.** The persistent truth is the canonical state root
  `SidStateRootV1`, written with the binary state codec
  (`ArpSIDStateSerializer.h`, `sid_state_codec.h`). Normalized parameter arrays
  are import and export helpers only.
- **Applying a root.** A preset or project load builds a root on a
  non-realtime thread and hands it to the render thread through the ownership
  mailbox (`sid_ownership_mailbox.h`). Three buffers change owner through one
  atomic word, so no buffer is ever shared between the threads. The next
  block applies it.
- **Wrapper state.** Each wrapper saves the root plus the GUI models that are
  not parameters: SETTINGS, MIX, KIT, and the DIGI model plus sample bank. The
  VST3 also saves the loaded C64 tune and its subtune. The
  DIGI model and bank are restored only as a pair. The VST3 layout is in
  [VST3_IMPLEMENTATION.md](VST3_IMPLEMENTATION.md#state-format).
- **Factory bank.** 180 slots: melodic patches 0–79, DrSID kits 80–119,
  SID-808 kits 120–149 and DIGI kits 150–179. The melodic range is
  GM-addressable, but every slot is a plausible SID voice, not a sampled
  imitation.
- **User files.** Patches and banks are saved as `.arpsid` / `.arpsidbank`
  (`arpsid_file_bank.{h,cpp}`).

---

## GUI models and telemetry

**GUI → engine.** The SETTINGS, MIX, KIT and DIGI models are plain structs.
Wrappers keep them under a mutex, sanitize them, and publish them to the
kernel (`publishGuiRealtimeModels`). The render side projects them into
compact control intent (`gui_realtime_projection_v588.h`).

**Engine → GUI.** The kernel publishes `ArpSIDTelemetry` each block. It holds:

- levels, active voices and the transport state;
- the SID register image, voice tokens and LFO values;
- drum, DIGI, forensic and Hi-Fi meters;
- scopes: main output, filter in and out, VCOs, DIGI and C64 bus;
- a full C64 machine snapshot.

Heavy sections (scopes, the C64 snapshot) are copied only when a visible
editor view asks for them (`telemetry_scope_demand.h`). Editors never read
engine internals; they only read published telemetry. The kernel → telemetry
projection is shared by the AU adapter and the VST3 kernel host
(`ArpSIDKernelTelemetryFill.h`).

---

## Wrappers

| Wrapper | Entry | Notes |
|---|---|---|
| AUv2 | `source/au2/ArpSIDAUv2Component.mm` | Five component flavors (ArpSID, Instrument, DrSID, SID-808, C64 player) with one kernel. Strict `auval` in CI. |
| AUv3 | `source/au3/ArpSIDAudioUnit.mm` | The app extension inside the Logic-compatible `ArpSID.app`. |
| Standalone | `source/au3/ArpSIDHostMain.mm` | Hosts the AUv3 presentation, with a keyboard, transport, tempo and preset browser. |
| VST3 | `source/vst3/`, `source/arpsid_controller.cpp` | See [VST3_IMPLEMENTATION.md](VST3_IMPLEMENTATION.md). |

`include/arpsid/core/sid_render_pipeline_capabilities.h` declares what each
wrapper's pipeline supports. Since 0.9.5 the VST3 capabilities equal the AU
ones.

---

## Build and verification

- **CMake options** select the products: `ARPSID_BUILD_TESTS`,
  `ARPSID_BUILD_AUV2`, `ARPSID_BUILD_AUV3`, `ARPSID_BUILD_LOGIC`,
  `ARPSID_BUILD_STANDALONE`, `ARPSID_BUILD_VST3` and
  `ARPSID_ENABLE_SANITIZERS`. `build.sh` wraps the common flows.
- **Tests.** The CTest suite covers unit tests, runtime behaviour, audio shape,
  timing and clock conservation, state and persistence, GUI wiring, and source
  contracts. It also runs under AddressSanitizer and UBSan.
- **Guards.** The repository guards are `scripts/check_version_coherence.py`,
  `scripts/verify_source_tree.py` and `scripts/check_audit_closure.py`.
  `scripts/ci/check_build_warnings.sh` fails any build that has an ArpSID
  compiler or linker warning.
- **AUv2 installation.** A user-local AUv2 install is run through the shell
  from CMake rather than executed by path, so it does not depend on the zip
  keeping execute bits.
- **CI and releases** are in the README.
