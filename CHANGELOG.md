# Changelog

All notable changes to ArpSID are documented here. The project uses
[semantic versioning](https://semver.org/); the single source of truth for the
version is `VERSION.txt`, mirrored by `project(VERSION)` in `CMakeLists.txt`
and `include/arpsid/version.h`.

## [0.9.7] — 2026-09-27

### Fixed (VST3 editor, Windows and Linux)

- **Knob arcs on Linux.** VSTGUI's cairo backend treats `drawArc` angles as
  radians although the API takes degrees, so every knob drew a full ring and
  the value arc was invisible. Arcs are now stroked through a graphics path.
- **SETTINGS menus.** The topology, theme, language and diagnostics menus did
  not draw: their pop-up menu kept its initial zero size. Menus now follow
  their container's size.
- **Stepped values match the engine.** Waveform and filter mode decode with
  `floor(v × 8)` and arp octaves with `int(v × 3)`; the editor rounded. A
  host-automated value between grid points (e.g. waveform 0.42) showed a
  different waveform than the one playing. One shared decode law now drives
  menus, labels, the filter curve, LFO shapes and the SEQ grid, and a test
  pins it to the engine formulas.
- **DIGI `$D418` mode menu** was mislabelled. The kernel's modes are
  `AUTH C64-BUS D418` (0) and `FAST PRIVATE D418` (1); the menu offered
  "legacy float" (a debug-only path) as its first entry, so the default
  authentic mode showed as "legacy float".
- Theme changes restyle every menu, including those in the MIX, KIT, DIGI and
  SETTINGS panels.
- MIX, KIT and DIGI knobs stop dragging when the host cancels a mouse
  gesture.

### Improved (VST3 editor)

- Page layout: rows of plain controls take their natural height and rows with
  displays or panels share the rest, so knob rows are compact and scopes and
  grids get the space.
- The on-screen keyboard lights every note the engine is sounding, not only
  the last one.
- The VCO scopes and SID bus timeline draw one labelled lane per signal.
  Modulation meters draw LFOs and pitch bend from the centre.
- The SID register readout wraps the raw register image; SEQ steps past the
  sequence length are faded; knob captions and values shrink to fit.
- The DIGI panel re-reads its ~480 KB sample bank only when it changes, not
  30 times a second.
- MIX, KIT and DIGI knobs have a Shift fine-drag.

### Documentation

- New [docs/VST3_EDITOR.md](docs/VST3_EDITOR.md): a screenshot and guide for
  each of the 17 tabs, the controls and gestures, and how the editor code
  works (data flow, layout, decode law, platform notes, tests).
- New [docs/VST3_IMPLEMENTATION.md](docs/VST3_IMPLEMENTATION.md): processor,
  kernel host, state format v5, controller, MIDI mapping, messages, threading.
- New [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): layers, the kernel's
  block pipeline, engines, parameters, state, GUI models, telemetry and
  wrappers. It replaces `docs/ARCHITECTURE_NOTES.md`.
- New [docs/PARAMETER_REFERENCE.md](docs/PARAMETER_REFERENCE.md): all 512
  parameters with default, values, steps and editor tab. It is generated from
  the code, and `ParameterReferenceDocTests` fails when it is stale
  (regenerate with the `arpsid_update_parameter_reference` target).
- [docs/TAB_ARCHITECTURE.md](docs/TAB_ARCHITECTURE.md) is rewritten for the
  current 17-tab ring; it described 9 tabs.
- `scripts/update_editor_screenshots.sh` regenerates the editor screenshots.

## [0.9.6] — 2026-09-27

### Fixed (VST3 editor, Windows and Linux)

- Long parameter labels (SID register knobs, SYNTH MODE toggles) no longer
  clip: labels shrink to fit and SID register knobs read `D400 FREQ LO`.

### Documentation

- README shows the Windows/Linux VST3 editor (MAIN, SID REG, MIX and BANK
  tabs), taken from the offscreen editor render.

### Release

- Releases carry only the packages and `SHA256SUMS.txt`. The first 0.9.5
  upload also attached the CI editor snapshots; rebuilding an existing
  release now removes assets outside that set.

## [0.9.5] — 2026-09-27

### Changed (VST3)

- **Same engine as the AU and Standalone.** The VST3 processor now runs the
  shared `ArpSIDDSPKernel` instead of the smaller Phase2 engine, so the MIX
  (per-channel FX chains, sends, master), KIT, DIGI sampler, SETTINGS and C64
  tune player layers all work in VST3. Parameter changes and notes are
  sample-accurate, and the host transport drives the sequencer, arpeggiator
  and C64 player. The processor class ID is unchanged, so existing projects
  open with the new engine.
- **State format v5** stores the patch plus the settings, mix, kit, DIGI
  model, sample bank, DIGI runtime and output-mode chunks. Projects saved by
  0.9.4 and earlier (state v1 to v4) still load. A patch that was picked but
  not yet rendered is what the host saves.
- **macOS:** every tab of the native editor now works in VST3 (MIX, KIT,
  DIGI, SETTINGS, SIDCORE, C64 player, pure SID capture, bank import and
  export). Before, those tabs had no engine behind them.

### Added (VST3, Windows and Linux)

- **Full ArpSID editor.** A VSTGUI editor replaces the host's generic
  parameter list. It has the same 17 tabs as the macOS editor, and every user
  parameter is on its tab. It includes:
  - live oscilloscope, filter response, LFO, VCO, SID register, sequencer,
    drum, forensic, HI-FI, modulation and SIDCORE displays;
  - the BANK browser (180 factory patches, user bank, and `.arpsid` /
    `.arpsidbank` load and save);
  - the SETTINGS, MIX, KIT and DIGI editors (including WAV import for DIGI
    pads);
  - the C64 tune player;
  - an on-screen keyboard and output meters.

  The editor follows host automation and preset changes, and supports HiDPI
  scaling.
- `arpsid_vst3_editor_check` renders every editor tab offscreen with the real
  engine running, writes one PNG per tab and checks the parameter binding.
  CI runs it on Linux and Windows and keeps the Linux PNGs as an artifact.

### Linux

- The VST3 build brings in VSTGUI directly (no GTK or standalone
  dependencies). It needs only the X11/xcb, xkbcommon, cairo, pango,
  fontconfig and Wayland development packages listed in the README.
- The warning gate now also covers the editor and the kernel on MSVC and
  Linux VST3 builds.

## [0.9.4] — 2026-09-26

### Fixed (VST3)

- **Factory presets load.** Picking a program in the host's program list,
  a Program Change mapped by the host, or the Program/Bank slot parameters now
  loads that factory patch. Before, the processor ignored both parameters and
  no factory patch could be loaded in a VST3 host. The controller sends the
  load to the processor (IConnectionPoint message, off the audio thread) and
  mirrors every parameter back to the host.
- **Controller follows project state.** `setComponentState` is implemented, so
  after a project or preset is restored the host's parameter view shows the
  restored values instead of defaults.
- **MIDI on all 16 channels.** The MIDI input bus declared one channel, so
  hosts could drop everything outside channel 1 (including GM drums on
  channel 10).
- **Host timing.** The processor now requests tempo, transport, musical
  position, cycle, bar position and time signature from the host
  (`IProcessContextRequirements`); it requested nothing before.
- **MIDI CC mapping** covers the same realtime CC law as the other formats:
  CC2 and CC70–77 (filter, envelope, LFO) plus CC4 (foot) and CC7 (volume),
  on every channel. Previously only 7 CCs were mapped.
- **Editor on-screen keyboard** (macOS) plays notes; it was silent. Editor
  parameter edits now also update the controller's own value.
- `setState` and preset loads no longer race the audio thread: `process()`
  try-locks the state and outputs one silent block while a load is in
  progress, never waiting on the UI thread.

### Fixed (core)

- 6510 CPU: decimal-mode `SBC` left-shifted a negative intermediate (undefined
  behaviour, found by the new UBSan job). The result bits are unchanged.

### Added

- `arpsid_vst3_host_tests`: a headless VST3 host test that loads the built
  bundle and checks all of the above. CI runs it on Linux (x86_64, aarch64),
  Windows (x64, arm64) and macOS.

### Build

- Linux: `./build.sh --vst3-sdk DIR [--install-vst3]` builds the VST3, runs the
  validator and host test, and installs to `~/.vst3`. `cmake --install` puts
  the bundle in `<prefix>/lib/vst3`; `arpsid_vst3_install_user` copies it to
  the per-user folder (Linux and Windows).
- `-DARPSID_ENABLE_SANITIZERS=ON` (`./build.sh --sanitize`) builds with
  AddressSanitizer + UBSan; CI runs the full suite under both.
- ccache is used automatically when installed; CI caches it.
- CI builds and tests the core with Clang as well as GCC.

## [0.9.3] — 2026-09-26

### Added

- Every product variant is now built, validated and published with each
  release:
  - **macOS (universal):** `ArpSID Standalone.app`, the AUv3 inside the
    Logic-compatible `ArpSID.app`, the AUv2 component (five flavors) and VST3.
  - **Windows:** VST3 for x64 and arm64 (first Windows builds).
  - **Linux:** VST3 for x86_64 and, new, aarch64.
- CI builds all of them on every push: the Steinberg validator runs on every
  VST3, strict `auval` on the AUv2, and the Standalone app gets a launch test.

### Fixed

- MSVC builds used `/fp:fast`, which can optimise away the NaN/Inf
  sanitation ArpSID relies on; they now use `/fp:precise` (and `/utf-8`).
- `digi_panel_model.h`: the DIGI tune-bias limits mixed an unsigned literal
  with a negative constant, which MSVC flagged (C4308) and which crashed the
  MSVC compiler; values are unchanged.
- MSVC `/W4` is aligned with the GCC/Clang warning policy, and two implicit
  double-to-float conversions in `sid_chip.h` are now explicit (same result).

## [0.9.2] — 2026-09-26

### Fixed

- AUv2 now honours host-owned user presets: `PresentPreset` with a negative
  preset number keeps the host's name (reported back by `PresentPreset` and
  in the saved state) instead of snapping to a factory slot and dropping it.
  The audible state and factory-preset authority are untouched. This clears
  the last `auval` warning ("Preset name is not retained in retrieved class
  data").
- VST3: no longer links the SDK's `base` library twice (Apple `ld` duplicate
  library warning), and warnings from Steinberg SDK sources are silenced so
  build logs show only ArpSID diagnostics.
- Removed two unused helper functions (one in the AUv2 component, one in a
  source-contract test) caught by the new warning gate.

### Build, CI and releases

- New CI workflow on every push and pull request: full test suite, VST3 SDK
  validator and, on the public repository, macOS AUv2 + VST3 builds with
  strict `auval` on all five flavors. Any ArpSID compiler/linker warning or
  `auval` warning fails the build.
- Releases publish automatically when a `VERSION.txt` bump lands on `main`
  (or a `v*` tag is pushed), gated on that full pipeline; they refuse private
  repositories and trees containing real C64 ROMs.
- `scripts/sync_public_mirror.sh` mirrors the private tree to the public
  repository, keeping the ROM placeholder and guarding against ROM leaks.
- GitHub Actions moved to Node 24 releases; Dependabot keeps them current.

## [0.9.1] — 2026-09-26

### Fixed

- Stepped parameter defaults now sit exactly on their value grid, so a host
  that sets the declared default reads the same value back. `auval` previously
  warned "Parameter did not retain default value when set" for 37 parameters
  (VCO waveforms, arp pattern length, sequencer length and the 32 sequencer
  step notes); the 16 host pitch-bend defaults had the same off-grid drift.
  The DSP decodes every new default to exactly the same value as before
  (same waveform, 17-step lengths, MIDI note 64, centred pitch bend), so
  patches sound identical. Guarded by the new `ParameterDefaultGridTests`.
- Removed an unused duplicate of the AUv2 editor-deferral predicate from the
  non-AUv2 build of the macOS editor (`-Wunused-function`).

## [0.9.0] — 2026-09-26

First public, cleanly versioned release. This consolidates the entire
pre-release development line (internally tracked as `0.0.690`, audit passes up
to 380 and closure revisions up to v970) into one release.

### Highlights

- **Formats:** AUv2 (five component flavors: ArpSID, ArpSID Instrument, DrSID,
  SID-808, C64 tune player), AUv3, VST3 and a standalone macOS host.
- **Engines:** BitPerfect/classic SID, `$D400` SID-register SynthMode, DrSID drum
  synthesis, SID-808, DIGI sampler with authentic `$D418` playback, and a
  cycle-exact C64 PSID/RSID player.
- **Arranger:** arpeggiator, 32-step sequencer, KIT routing, MIX page and a
  modulation matrix; reverb, limiter and the Hi-Fi post-FX chain.
- **Canonical render pipeline:** one timed-event ordering authority across
  ingress, storage and dispatch; sample-boundary-safe structural changes;
  exact-offset post-FX; render-owned telemetry.
- **Parameter presentation authority:** one shared service formats and parses
  every host-visible parameter across AUv2/AUv3/VST3 with the canonical DSP
  laws; Unicode-safe VST3 strings.
- **DrSID kits:** user-kit save normalizes to DrSID mode for `.arpsid`/`.json`;
  the GUI "Standard" quick-kit matches the factory base voicing.

### Changed

- Version scheme reset to plain semantic versioning. The previous compound
  identifiers (`0.0.690`, `pass380`, `v970`, `…-closure` qualifiers) and the
  `ARPSID_BUILD_PASS` macro are gone.
- Repository cleanup: ~150 historical per-pass release notes, closure
  reports, audit ledgers, hand-off files and a stale committed release manifest
  were removed from the tree. The 9 tests that only asserted the wording of
  those documents were removed, and ~30 source-contract tests had their
  document-wording assertions stripped; all behavioural and source-contract
  assertions are unchanged. The full history remains available in git.
- Reference documentation now lives in `docs/`.

### Fixed

- VST3 now builds off macOS (verified on Linux against VST3 SDK 3.8.1; the
  SDK validator passes 47/47). The Cocoa editor sources are compiled only on
  Apple, with a portable bridge elsewhere; ArpSID's duplicate module entry
  points (`arpsid_module_init.cpp`), which collided with the SDK's own
  `linuxmain.cpp`/`dllmain.cpp` and skipped `InitModule`, were removed; and the
  SDK's example targets (which need GTK on Linux) are disabled.
- AUv2/AUv3 `AudioComponents` version integer now uses Apple's
  `major << 16 | minor << 8 | patch` encoding, so hosts display the real
  version (previously `major*10000 + minor*100 + patch`).
- De-flaked `Auv2RenderNotifyRtSafetyV524Tests`: under a loaded parallel CTest
  run the reader thread could be starved until the publisher finished, failing
  the "snapshot observed" sanity checks. The publisher now runs (bounded) until
  the reader has observed both outcomes; the torn-pair invariant is unchanged.

### Known limitations

Verified on the Linux-buildable core with a green test suite. The following are
external platform sign-off items and are **not** claimed as verified:

- macOS AUv2/AUv3 validation in Logic Pro and other hosts.
- Code signing, notarization and an installer.
- A full build against the real Steinberg VST3 SDK in a host matrix.
- Sanitizer and fuzz coverage.
- Strict physical C64 exactness beyond the documented boundaries
  (`docs/C64_EXACTNESS_BOUNDARIES.md`).
