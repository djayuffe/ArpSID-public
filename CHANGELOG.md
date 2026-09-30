# Changelog

All notable changes to ArpSID are documented here. The project uses
[semantic versioning](https://semver.org/); the single source of truth for the
version is `VERSION.txt`, mirrored by `project(VERSION)` in `CMakeLists.txt`
and `include/arpsid/version.h`.

## [0.9.12] — 2026-09-30

### Changed

- **CI runs only in the public mirror.** Every workflow is skipped in the
  private repository (which carries the real C64 ROMs); the public mirror
  runs all builds, tests and releases.

### Fixed

- **RPN pitch-bend range reached the wrong channel.** The per-channel
  host-control parameters start at id 286, which is not a multiple of 16, but
  the RPN code took the MIDI channel from the low four bits of the id: a
  bend range sent on channel 1 changed channel 15's range (and read channel
  15's RPN selection). The channel now comes from the offset into the block.
- **RPN / NRPN / Data Entry did nothing in the VST3 and on raw MIDI.** The
  VST3 MIDI mapping did not route CC 101/100/99/98/6/38 and the raw-MIDI path
  (AU, Standalone) ignored them, so hosts and MPE setups could not set the
  pitch-bend range. Both now drive the shared RPN state machine: RPN 0 +
  Data Entry (CC6 semitones, CC38 cents) sets the channel's bend range;
  selecting an RPN changes nothing by itself; selecting an NRPN or Reset All
  Controllers (CC121) deselects the RPN, so NRPN data never lands on the
  bend range.
- **VST3: host MIDI parameters were read-only.** The 208 per-channel host
  MIDI parameters (written by the host through the MIDI mapping) were
  flagged `kIsReadOnly`, which tells a host only the plug-in may change them.
  They are now hidden and host-writable, and each title names its channel
  (`Host MIDI Sustain Ch 5`), so the titles are unique (the SDK validator
  reported 195 duplicate titles).
- **VST3: silence flags.** Output was flagged silent only when exactly zero,
  and the engine's 24-bit dither is never zero, so released voices were never
  reported silent. Blocks at or below −120 dBFS are now written as zero and
  flagged, so hosts can skip downstream processing.

### Added (VST3)

- **Control under the mouse** (`IParameterFinder`): the editor tells the
  host which parameter is under a point, so host "learn" functions (quick
  controls, "last touched" parameter) work with the editor, at any size.
- **Host knob mode** (`IEditController2::setKnobMode`): circular, relative
  circular or linear knob dragging follows the host's preference; the editor
  stays linear until a host sets a mode.
- **Drum note names**: the drum kit programs (DrSID / SID-808) report the
  General MIDI drum name of notes 35–81 (`IUnitInfo` program pitch names), so
  host drum editors and piano rolls label the notes.
- **Plug-in browser image**: the bundle carries a snapshot of the editor's
  MAIN page (`Resources/Snapshots/<class id>_snapshot.png`), listed in
  `moduleinfo.json`.

## [0.9.11] — 2026-09-29

### Fixed (VST3 realtime and buffers)

- **Stuck notes under heavy automation.** Parameter points were collected
  before notes into the per-block event buffer, so a block with more
  automation points than the buffer holds (4096) lost its note-offs. Notes are
  now collected first, and automation shares a per-block budget (256 events)
  that keeps the block inside the kernel's event lanes: past it each
  parameter is thinned to evenly spaced points that always include its last
  value.
- **Allocation on the audio thread.** Events were ordered with
  `std::stable_sort`, which allocates a temporary buffer. They are now sorted
  with the kernel's total order (`std::sort`, allocation-free); the host test
  counts heap allocations inside `process()` and requires zero.
- **Silent 64-bit blocks.** A 64-bit block larger than the announced maximum
  block size came out silent. It is now rendered in slices and sounds the
  same as announced-size blocks.
- **Editor keyboard timing in large blocks.** Blocks over 4096 frames were
  split before the kernel, which then drained the editor's queued notes and
  parameter changes per chunk instead of once per block. The whole block now
  goes to the kernel, which splits it correctly.

### Performance (every format)

- **About half the CPU.** The SID filter law rebuilt its analogue calibration
  table (and sorted it) for every sample, the SID register engine recomputed
  the full law whenever the drift inputs moved (almost every sample), and the
  CLASSIC engine recomputed filter coefficients on all eight chips for every
  render slice. The calibration is now a static table, the law is split into
  a cached base stage and a cheap per-sample finish, and unchanged cutoff and
  resonance keep their law. A held chord in the VST3 went from about 46 % to
  22 % of one core at 512-frame blocks (56 % to 29 % at 32 frames), with
  bit-identical audio.
- The VST3 host test prints the render cost at block sizes from 32 to 2048
  frames, and the Linux editor's CPU use in a real X11 window.
- **Idle CLASSIC engine (regression in 0.9.10).** The release-tail fix treated
  every chip whose envelope reported "active" as a tail, including chips that
  never played, whose power-on envelope state stays "active". An idle CLASSIC
  instance therefore rendered all eight chips: 10–40× the idle CPU (a fresh
  instance: 156 M instructions for its first block instead of 3.5 M) and every
  silent chip's noise and dither in the output, about 50 dB down. A tail is
  now a slot that played a note since reset and still has a running envelope;
  release tails are unchanged.
- **C64 tune player.** About 10 % fewer instructions, bit-identical output: the
  timed SID bridge no longer advances a second cycle-by-cycle OSC3/ENV3
  readback model that nothing reads (the runtime's own model answers every
  read), the OSC3 byte of a single waveform is computed when read instead of
  every cycle, and the VIC-II skips its sprite loops when no sprite is active.

### Fixed (all formats, all platforms)

- **Floating-point mode leaked into the host.** The engine switched the
  calling thread to flush-to-zero (x86 FTZ/DAZ, AArch64 FZ/DN) in every render
  and in setup, and never restored it, changing the arithmetic of the host's
  mixer and of other plug-ins on the same threads. It is now set only while
  the engine renders and restored afterwards; setup no longer touches it.
  Windows x64 builds (MSVC, which does not define `__SSE__`) now flush
  denormals too; they did not before.

## [0.9.10] — 2026-09-28

### Fixed (engine, every format)

- **CLASSIC Unison voice count.** Voice Spread now sets the number of stacked
  voices, `1 + int(spread × 7)` (1–8), as well as the detune, as documented. The
  count was never applied and Unison always stacked 4 voices. No factory patch
  uses Unison. A project saved with Unison at Voice Spread 0 now plays one voice.
- **CLASSIC Mono, Legato and Unison release tails.** The release phase now sounds
  after note-off, at the same level as Poly. The live render skipped released
  voices, and their velocity and pitch were cleared, so notes were cut dead
  whatever the Release setting. The active-voice count now drops to 0 when idle;
  it used to report 8 with no notes playing.
- **SYNTH / SID REG filter mode.** `$D418` now gets all eight Filter Mode choices
  (OFF, LP, BP, LP+BP, HP, NOTCH, BP+HP, ALL) from the same decode as CLASSIC and
  the editors. Before, the choices were collapsed onto LP, BP and HP: HIGH-PASS
  played band-pass, and OFF still filtered low-pass.
- **SYNTH / SID REG hard sync.** VCO1 Sync and VCO3 Sync now reach the chip; only
  VCO2 Sync did.
- **Pitch-bend range.** One 0–48 semitone limit (RPN 0, the MPE standard) in
  every engine. CLASSIC and the single-SID engine stopped at 24.
- **Factory filter modes.** The factory bank's filter types are stored as
  canonical Filter Mode values. Parameter, editor, `$D418` register mirror and
  sound now agree for every slot, and no factory patch selects OFF.
- **macOS JSON sound import.** `filter.mode` strings (`lp`, `bp`, `hp`,
  combinations, `notch`, `all`, `off`) now map to the right Filter Mode. Before,
  low-pass imported as band-pass.

### Fixed (VST3)

- **Linux: the editor crashed the host when it opened.** The VSTGUI frame was
  opened without the X11 frame configuration that carries the host's run loop,
  so VSTGUI never opened its X connection and crashed on the first X call. The
  editor now runs on the host's `Linux::IRunLoop` (from the plug frame) through
  a forwarding run loop, offers X11 only (VSTGUI is built without Wayland), and
  refuses to open instead of crashing when a host provides no run loop.
- **Linux: reopening the editor could crash.** When the last editor closed,
  VSTGUI disconnected from X without finishing cairo's device for the
  connection; the next connection often reused the address and cairo asserted.
  The device is now finished before the disconnect.
- **Editor `attached` result.** A failed open now returns `kResultFalse`, so the
  host does not show an empty window.
- **Bypass text.** The Bypass parameter parses its own `On`/`Off` text.

### Added (VST3)

- **Factory patches as `.vstpreset` files.** Hosts whose preset browser reads
  preset files never showed the factory patches, only hosts that read the
  program list did. The build now writes all 180 as `.vstpreset` files (one
  folder per role, with name, category and description metadata), each reloaded
  and verified against the program it came from. The VST3 zips ship them in
  `VST3 Presets/` and the installers put them in the VST3 preset folders
  (`~/.vst3/presets`, `Documents\VST3 Presets`, `~/Library/Audio/Presets`, or
  the system folders). Loading one changes only the patch, exactly like picking
  the program: the MIX/KIT/DIGI/SETTINGS models, a loaded tune, bypass and the
  editor size stay. `cmake --install` and `arpsid_vst3_install_user` install
  them too.
- CI opens the Linux editor in a real X11 window (Xvfb) with a host run loop,
  resizes, closes and reopens it, and checks the preset files on every VST3
  platform.

### Fixed (installers)

- `install.sh` and `install_macos.sh` no longer abort on macOS's bash 3.2 when
  no `sudo` is needed (an empty array expanded under `set -u`).

### Compatibility

- VST3 states can carry a `PRST` chunk that marks a patch-only (preset) state.
  Older versions skip unknown chunks, so they load such a preset as a normal
  state.
- Saved states now carry a **state-law revision** marker: an extra semantic entry
  that older versions ignore, so 0.9.10 projects still open in 0.9.9. A project
  saved before 0.9.10 that plays in SYNTH mode is migrated on load so it sounds
  exactly as before: its filter mode is set to the type it actually played, and
  the never-audible VCO1/VCO3 Sync is cleared. CLASSIC and DrSID projects are not
  changed.

### Documentation

- `docs/internals/SYNTH_MODES.md`: render modes, flavors, the SYNTH / SID REG
  mode in full, and the projection engines. `docs/FEATURES.md`,
  `docs/TECHNICAL_SPECIFICATIONS.md` and `docs/internals/` (SID chip, C64
  machine, runtime, engines) complete the reference set.
- The MIX channel volume law is now documented as the code implements it:
  gain = value / 200, so 200 is unity and 255 is +2.1 dB. The old comment said
  +6 dB.

### Fixed (macOS editor: AUv2, AUv3, Standalone, macOS VST3)

- Knob captions were cut to nine characters ("Master Vo", "Voice Mod"); they
  now shrink to fit and only then end in "…".
- Knob values showed raw 0–1 numbers ("0.780"); they now show the same text as
  the host (`+0.0 ct`, `POLY`, `SAW`, `LOW-PASS`, `1.60 ms`).
- In macOS Light Mode, standard controls (BANK slots, preset and transport
  buttons, pop-ups, the MIX strips) turned light with pale, unreadable text.
  Controls now follow the editor theme (dark, or light for the Light theme).
- The tab strip clipped names to "◈ MA…"; it now picks a font size at which
  all 17 names fit and sizes each tab from its label.
- Standalone: **Reconnect All Sources** moved to ⇧⌘R; it shared ⌘R with
  **Random Patch**.

### Added

- `arpsid_au_editor_snapshot` renders every tab of the Cocoa editor and the
  landing page of each AU flavor from the installed AUv2
  (`scripts/update_au_screenshots.sh`, and the manual **Screenshots**
  workflow, which publishes to the `ci-screenshots` branch).
- `docs/AU_EDITOR.md`: the macOS editor tab by tab, with a screenshot of every
  tab and flavor, controls, shortcuts, the Standalone menus and how the editor
  works.
- README: a complete feature list and screenshot galleries of all 17 tabs in
  both editors and the five AU flavors.

## [0.9.9] — 2026-09-27

### Added (VST3)

- **Host bypass.** A `Bypass` parameter (`kIsBypass`) drives the host's
  bypass button. It fades the output out and in over 10 ms, so it never
  clicks, while the engine keeps running (notes, arpeggiator, sequencer and
  .sid playback stay in time). It is saved with the project (`BYPS` state
  chunk) and the editor header shows `BYPASSED`.
- **64-bit processing.** Hosts that run a 64-bit audio path (Reaper, Cubase
  and others) no longer need to convert around ArpSID.
- **Editor state in the project.** The editor size and the last tab are saved
  in the controller state, so a reopened project opens the editor the way you
  left it.
- **Right-click parameter menu.** Right-clicking a knob, switch or menu opens
  the host's menu for that parameter (automation, MIDI learn, ...) with an
  added `Reset to Default`.
- **Computer-keyboard notes.** `A`–`L` play a piano octave and a half, `W E T
  Y U O` the black keys, `Z`/`X` shift the octave. Keys with Ctrl/Alt/Cmd
  still go to the host.
- **Track name and colour.** Hosts that share channel context
  (`IInfoListener`) see their track name, in the track colour, in the editor
  header.
- **Program attributes.** Factory programs report the `Instrument|Synth`
  category to hosts that sort presets by it.

### Fixed (VST3)

- The controller no longer keeps a reference to the last editor view after
  the host closed it (a reference cycle kept the view alive until the plug-in
  was unloaded).
- Host text for parameters outside the shared set (Bypass) now uses the
  generic conversion instead of failing.

### Tests

- Host integration test: bypass (flag, silence, persistence, recovery),
  64-bit rendering and oversized blocks, controller state round trip, track
  info, program category.
- Kernel host state test: bypass save/restore/clear and legacy states.
- Editor test: tab memory, right-click menu hit testing, computer-keyboard
  notes and octave shift, Ctrl passthrough.
- The extended SDK validator (`validator -e`) passes 537 tests, including
  bypass persistence.

## [0.9.8] — 2026-09-27

### Added (VST3)

- **Named values in hosts.** Automation lanes, generic editors and typed
  entry show and accept names instead of indices or 0..1 values: waveform
  `PULSE`, filter `LOW-PASS`, voice `UNISON`, glide `C64 SLIDE`, LFO `S&H`,
  arp `UP/DOWN` and `2 OCT`, seq `PING-PONG`, Hi-Fi `TRANSCENDENCE`, arp
  transpose `+3 st`, pattern length `16 steps`. One shared table serves
  hosts, both editors and the parameter reference, following the engine's
  decode laws. AU parameter units are unchanged.
- **The C64 tune is saved with the project.** The loaded `.sid` and its
  subtune come back when the project opens (new `SIDF` state chunk; older
  versions skip it). SONG < / > now works on a restored tune.
- **Parameter groups.** Parameters sit in one VST3 unit per editor tab, plus
  a host-MIDI group with a sub-unit per controller type, so hosts that show
  units no longer list 512 parameters flat.
- **Resizable editor** (Windows/Linux): it scales from 0.5× to 3× with the
  window, keeps its 3:2 shape, and reopens at the size you chose.
- **DIGI recording** (Windows/Linux): the new `DIGI Capture In` side-chain
  input (off until a host routes audio to it) and REC/STOP in the DIGI tab
  record a take into the selected slot.

### Fixed (VST3)

- A truncated saved state now keeps and publishes the chunks read before
  the cut.
- Windows: the C/C++ runtime is linked statically, so the plug-in no longer
  needs the Visual C++ Redistributable (CI checks the module's imports).
- A Windows/Linux build without the editor returns no editor view (hosts
  show their generic UI) instead of a macOS-only view.

### Linux build

- `scripts/linux/install_build_deps.sh` installs the build packages with
  apt, dnf, pacman or zypper (`--dry-run`, `--no-editor`).
- `scripts/fetch_vst3_sdk.sh` and CMake `-DARPSID_FETCH_VST3SDK=ON` fetch the
  pinned Steinberg SDK.
- CMake checks the editor packages with pkg-config and names every missing
  one.
- `-DARPSID_VST3_EDITOR=OFF` builds a VST3 without the editor.
- `build.sh --install-deps --fetch-vst3-sdk --install-vst3` goes from a bare
  system to an installed plug-in. `--package-vst3` makes a release-style zip,
  and `--no-vst3-editor` skips the editor.

### Installers

- Every release zip contains its installer: `install.sh` (Linux: user or
  system install, uninstall, a missing-library check), `install.ps1`
  (Windows: all users or per-user, unblock, uninstall), and
  `install_macos.sh` (all macOS products, quarantine removal, AUv3
  registration, AU cache refresh, optional auval, uninstall).
- One-line installers `get_arpsid.sh` / `get_arpsid.ps1` download the build
  for your machine, verify it against `SHA256SUMS.txt` and install it.
- New [docs/INSTALL.md](docs/INSTALL.md); the README and release notes point
  to it. CI smoke-tests the Linux and Windows installers on every build.

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
