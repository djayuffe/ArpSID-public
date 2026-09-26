# ArpSID 0.9.0

**A Commodore 64 SID synthesizer and C64 tune player as an audio plug-in for macOS.**

ArpSID is a large, SID-accurate instrument built around a single canonical render
pipeline. It ships as Audio Unit (AUv2 and AUv3), VST3, and a standalone macOS host,
and combines faithful MOS 6581/8580 SID emulation, an authentic cycle-exact C64
PSID/RSID tune player, three drum engines, a DIGI sampler with authentic `$D418`
playback, and a full arranger (arpeggiator, 32-step sequencer, kit routing, mixer and
post-FX) — with host-accurate parameter display, automation and state restore.

---

## Screenshots

Captured from the ArpSID standalone host (AUv3 presentation), running the shipped build.

![ArpSID — main synth panel](docs/screenshots/arpsid-main-synth.png)

*Main synth view — Master section, three VCO banks (VCO1–3), the multimode Filter, ADSR envelope and the Output/FX stage.*

![ArpSID — DrSID drum machine](docs/screenshots/arpsid-drsid-drum-machine.png)

*DrSID register/wavetable drum core — 16-step pattern grid (BD/SD/CP/CH), the SID-808 voice bank with its General-MIDI note map, the live `$D400–$D418` register scope, and the Analog SID control bank.*

![ArpSID — SID register view](docs/screenshots/arpsid-sid-register-view.png)

*SID-register (`$D400`) monitor — per-voice VCO, filter and envelope register read-outs for direct SynthMode inspection.*

---

## Plug-in formats

| Format | Notes |
|---|---|
| **AUv2** (`aumu`, manufacturer `ASID`) | Five component flavors: `ArpS` (ArpSID), `ArIn` (ArpSID Instrument), `DrSD` (DrSID drum machine), `S808` (SID-808), `C64P` (C64 tune player). |
| **AUv3** | App-extension + wrapper-app path on macOS. |
| **VST3** | Phase2 processor/controller path (built against the Steinberg VST3 SDK). |
| **Standalone** | macOS standalone host shell. |

Wrappers share the canonical core but declare capabilities explicitly: AU owns the
KIT/DIGI/MIX overlay layers; Phase2/VST owns the canonical core, fractional rendering,
exact post-FX and no-output continuation. "Parity" means shared contracts where
capabilities overlap — not forced feature identity.

## Sound engines

Each SID exposes three independent voices — triangle, sawtooth, variable-pulse and
noise waveforms, with per-voice pulse-width modulation, ring modulation and hard sync —
feeding one shared multimode filter (low-, band- and high-pass, selectable per voice)
and a master volume. ArpSID drives that silicon through several complementary engines:

- **BitPerfect / classic SID** — SID-accurate register, waveform, ADSR and multimode
  filter behavior; glide/portamento laws; fractional-cycle rendering.
- **SID-register / SynthMode** — direct `$D400`-register synthesis with a canonical
  voice-token identity model (real host note IDs are protected; anonymous/replayed
  notes use stable synthetic identities).
- **DrSID drum synthesis** — a distinct drum authority with per-hit register
  microprograms and two machine models (SID-authentic drum core and Analog X0X-8):
  kick, snare, closed/open hat, clap, cowbell, tom, rim.
- **SID-808** — an authored x0x-style analog projection with staged musical shapes
  (kick pitch-drop stages, tom pitch movement, hat ring/tail, clap burst train,
  cowbell partial/tail) and its own factory range.
- **DIGI sampler + authentic `$D418`** — import or record samples (decimated to the
  canonical ~8 kHz 4-bit `$D418` nibble stream) and authentic volume-DAC digi playback
  that follows the active PAL/NTSC SID clock.
- **C64 PSID/RSID player** — a cycle-exact PHI2 machine (NMOS 6510 CPU, CIA/VIC-II,
  open-bus, PLA) that runs real `.sid` tunes, with honest strict-RSID vs. compatible
  execution reporting.

## Arranger, routing and modulation

- **Arpeggiator** — mono/legato/unison/poly, fixed and host-synced rates, gate length,
  swing, octave range, hold/latch, transpose, portamento glide; internal gates are
  materialized into the canonical event queue at exact sample offsets.
- **32-step sequencer** — per-step note/velocity/gate, swing, internal or host-tempo
  following; explicit per-step boundaries shared by melodic events, KIT, float DIGI and
  authentic `$D418`.
- **KIT routing** — multi-target drum routing across DrSID / SID-808 / DIGI.
- **MIX** — per-voice volume/pan/mute/solo and insert FX.
- **Modulation matrix** — four LFOs plus velocity, note, key-follow, mod-wheel,
  pitch-bend, aftertouch, poly-pressure, random, eight macros and an envelope, routed to
  filter, oscillator, pulse-width, volume and LFO-rate targets.

## Post-FX

Reverb, output limiter, and the "Hi-Fi Transcendence" chain (oversampling, tape
saturation, analog warmth, psycho-exciter, stereo width, voice diffuser). All post-FX
parameter changes are applied on the exact canonical sample timeline — a value that
arrives at sample K never affects samples before K.

## SID authenticity and forensic modeling

- Three chip revisions selectable: MOS 6581 R2 / R3 / R4 and MOS 8580 R5.
- PAL and NTSC clock domains, with sample-boundary-safe transitions.
- Optional forensic analog modeling: clock jitter, supply ripple, thermal drift, voice
  crosstalk, external bleed, die temperature, supply voltage, per-chip seed, filter
  ohmic behavior and more.

## Host integration

- **Parameter presentation authority** — one shared, parameter-ID-aware service formats
  and parses every host-visible parameter across AUv2/AUv3/VST3 using the *same*
  canonical DSP laws (so displayed values match what the engine actually renders), with
  Unicode-safe VST3 strings.
- **Full parameter automation** — every host-visible parameter is automatable, and value
  changes take effect on the exact canonical sample timeline.
- MIDI CC → parameter mapping, opt-in GM channel-10 drum promotion, and PAL/NTSC-aware
  note handling.

## Standalone host & GUI

- **Standalone macOS host** — a full "visual host" shell that loads the AUv3 presentation
  with an on-screen velocity keyboard, transport (play / stop / record), tempo control and
  a preset/bank browser, so the instrument runs without a DAW.
- **Tabbed editor** — a MAIN synth panel (Master, three VCO banks, multimode filter, ADSR
  and Output/FX) plus dedicated LFO/ARP, SEQ, MACRO, KIT, DIGI, MIX, FORENSIC and SIDCORE
  pages.
- **Live SID register scope** — a `$D400–$D418` register monitor with per-voice VCO,
  filter and envelope read-outs plus PK/RMS output metering, driven only by
  render-published telemetry.
- **DrSID drum view** — a 16-step pattern grid, the SID-808 voice bank with its
  General-MIDI note map, and the Analog SID control bank.

See the [Screenshots](#screenshots) above.

## Presets, banks and state

- **180-slot factory patch bank** spanning the synth, DrSID, SID-808 and DIGI families.
- **User banks and kits** — save and recall user patches and drum kits, and import/export
  DrSID kits as `.arpsidbank` / `.json` drum libraries.
- **Full state restore** — the complete instrument state, including the DIGI user-sample
  bank, is serialized for host save/restore and cross-format state transfer.

## Architecture highlights

- **Canonical render pipeline** — one timed-event ordering authority survives ingress,
  storage and dispatch; structural (engine/model/PAL-NTSC) changes are sample-boundary
  safe; no-output blocks still advance the full pipeline into fixed scratch.
- **Realtime-safe** — render paths are lock-free, allocation-free and bounded; heavy
  producer work (parsing, canonicalization, file I/O) happens off the render thread and
  is published through mailboxes.
- **Render-owned telemetry** — the GUI consumes immutable/atomic snapshots with frame-ID
  coherence rather than reading live render state.
- **C64 render transactions** — a play/service attempt that may overflow rolls back CPU,
  CIA/VIC, RAM/Color-RAM, SID bridge and diagnostics atomically.

## Building

Requires a C++17 toolchain and CMake. On macOS, Xcode provides the AU/AUv3/standalone
toolchain; VST3 additionally needs the Steinberg VST3 SDK on the include path.

```bash
# Configure + build (Linux-buildable core + tests)
./build.sh --build-dir build-release --config Release --parallel 4

# Full test suite
ctest --test-dir build-release --output-on-failure

# macOS: build, install and strict-validate the AUv2 component
./build.sh --install-auv2 --clear-au-cache --validate-auv2 --parallel 8

# Create a clean source release archive
./build.sh --package-release --parallel 4
```

Repository guards (run before/after changes):

```bash
python3 scripts/check_version_coherence.py
python3 scripts/verify_source_tree.py
python3 scripts/check_audit_closure.py
```

## Testing

The CTest suite (~475 tests) spans unit, runtime-behavior, audio-shape,
timing/clock-conservation, state/persistence, GUI-wiring and source-contract
classes:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DARPSID_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure -j4
```

## Documentation

| Document | Contents |
|---|---|
| [`CHANGELOG.md`](CHANGELOG.md) | Release notes. |
| [`docs/ARCHITECTURE_NOTES.md`](docs/ARCHITECTURE_NOTES.md) | Wrapper/kernel authority rules. |
| [`docs/REALTIME_OWNERSHIP.md`](docs/REALTIME_OWNERSHIP.md) | Which code runs on the render thread vs. the producer side. |
| [`docs/REALTIME_ROLLBACK_JOURNAL.md`](docs/REALTIME_ROLLBACK_JOURNAL.md) | Rollback-safe C64 render transactions. |
| [`docs/TAB_ARCHITECTURE.md`](docs/TAB_ARCHITECTURE.md) | GUI tab specification. |
| [`docs/D418_NIBBLE_SPEC.md`](docs/D418_NIBBLE_SPEC.md) | DIGI `$D418` 4-bit sample format. |
| [`docs/SID_FILE_FORMAT_NOTES.md`](docs/SID_FILE_FORMAT_NOTES.md) | PSID/RSID handling. |
| [`docs/C64_EXACTNESS_BOUNDARIES.md`](docs/C64_EXACTNESS_BOUNDARIES.md) | What the C64 player does and does not claim. |
| [`packaging/macos/`](packaging/macos/) | Signing, notarization and release sign-off checklists. |

## Project layout

| Path | Contents |
|---|---|
| `include/arpsid/core/` | Canonical runtime, event queue, timing, C64 machine, post-FX, telemetry. |
| `include/arpsid/engines/` | BitPerfect, arpeggiator, DrSID, SID-808, DIGI sampler / `$D418`. |
| `include/arpsid/gui/`, `include/arpsid/patchbank/` | GUI models; factory patch/kit banks. |
| `source/au2/`, `source/au3/` | AUv2 component, AUv3, DSP kernel, view controller. |
| `source/arpsid_processor_phase2.*`, `source/gui/` | VST3 processor/controller and GUI. |
| `source/tests/`, `scripts/` | Test suite; build/validation/guard scripts. |

## Status and validation limits

Verified on the Linux-buildable core with a green test suite. The
following remain external, platform sign-off items and are **not** claimed as verified:
macOS AUv2/AUv3 + Logic host validation, code signing / notarization / installer, a full
build against the real Steinberg VST3 SDK in a host matrix, sanitizer/fuzz coverage, and
strict physical C64 exactness beyond the documented boundaries
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
