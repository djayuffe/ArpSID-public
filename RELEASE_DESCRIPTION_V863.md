# ArpSID 0.0.690 pass380 v863 - Feature Description

ArpSID v863 is the Logic/AUv2 closure release for the SID-808, DrSID, classic
SID, C64 SID player, and DIGI-focused ArpSID instrument family. This build
focuses on making the installed macOS AUv2 component behave like the source
code says it should: pads, MIDI notes, transport-driven drum sequencing, C64
SID playback, and strict Audio Unit validation all pass through the intended
audio ownership paths.

The main v863 fix closes the remaining SID-808 "telemetry moves but no sound"
failure. Drum MIDI in DrSID runtime mode now enters the DSP kernel target before
falling back to canonical DrSID handling. In the SID-808 flavor, that target
hook routes notes and releases into the SID-808 drum bridge, so GUI pads, DAW
MIDI clips, live controllers, and sequenced transport events all trigger the
same audible bridge engine instead of only updating runtime telemetry.

## Headline Features

- Five-flavor AUv2 family in one validated component: Classic ArpSID, ArpSID
  Instrument, DrSID, SID-808, and C64 SID Player.
- SID-808 now has a closed input-authority path for GUI pads, host MIDI, and
  transport sequencer notes.
- SID-808 drum MIDI accepts General MIDI drum notes on any MIDI channel for
  stopped-transport audition, while channel 10 remains fully supported.
- SID-808 bridge output is fail-open: real bridge hits and tails own the output
  bus, but an idle or stale silent bridge buffer can no longer erase fallback
  audio.
- Installed AUv2 component passes strict validation for all five subtypes:
  `ArpS`, `ArIn`, `DrSD`, `S808`, and `C64P`.
- Direct AUAudioUnit SID-808 render-event smoke and installed AUv2 SID-808 host
  smoke both produce nonzero audio.
- C64 SID player render path is isolated earlier, avoiding unnecessary normal
  synth, drum, MIDI, sequencer, and bypass-cleanup work.
- C64 PHI2 SID-write observation no longer performs per-block dirty-log scans.
- Dirty-write logs now have consume/clear semantics, preventing a wrapped log
  from permanently forcing full 64 KB RAM synchronization.
- Release packaging is clean and reproducible, excluding build trees, dist
  outputs, and generated validation logs from the source zip.

## SID-808 Audio Path

v863 makes the SID-808 bridge the actual audio authority when the SID-808 flavor
is active. The old failure path could send a note into the canonical DrSID
runtime and update visible activity, while bypassing the kernel function that
owns SID-808 bridge routing. That meant meters, scope, or telemetry could imply
activity even though the bridge had never received a drum hit.

This release fixes that by routing canonical drum MIDI through:

- `runtimeTriggerDrSidNote()` for NoteOn events
- `runtimeReleaseDrSidNote()` for NoteOff/release events
- SID-808 bridge `noteOn` ownership when the active flavor is SID-808
- canonical DrSID fallback only when no kernel target is available

The result is one coherent path for pads, MIDI, transport playback, and runtime
drum triggers. If the user hits a SID-808 pad, plays a GM drum note from a DAW
clip, or starts the internal drum pattern from transport, the bridge now receives
the hit that produces the audible sound.

## SID-808 Output Ownership

The SID-808 bridge still has full bus authority when it is actually rendering a
hit, voice, or tail. That is essential for the SID-808 flavor because the bridge
is the intended final drum engine, not a decorative layer.

The important v861-v863 refinement is that this authority is now guarded by
renderable activity:

- nonzero bridge peak
- active SID-808 voices
- new note-on counter movement

When those signals are present, the bridge replaces the final output bus. When
they are absent, a silent scratch buffer is treated as idle and cannot wipe out
fallback audio. This preserves the full SID-808 replacement model without
letting stale silence become the final output.

## Logic and AUv2 Readiness

The installed AUv2 build was refreshed, installed, cache-cleared, and validated
on macOS. The final closure log shows:

- release-check: 7/7 PASS
- full CTest: 383/383 PASS
- strict AU validation: PASS for all five AUv2 flavors
- installed AUv2 verification: PASS
- direct SID-808 render-event smoke: PASS
- installed SID-808 host smoke: PASS

Installed binary SHA256:

`af2ea5def5d37e1ce6054aee12d99621203a0f6c31ccb7484a00507c3ec50af7`

## C64 SID Player Improvements

v863 retains and guards the C64 SIDPLAY cleanup work from the preceding audit.
The C64 player now branches before the normal synth/drum/MIDI path, which keeps
pure C64 SID playback from doing unrelated per-block work. Bypassed performance
state cleanup is edge-triggered, so the render path does not repeatedly clear
the same state every block.

The C64 memory sync path was also tightened:

- PHI2 SID-write edge capture observes the SID sink directly.
- The old per-PHI2 dirty-log scan was removed from edge capture.
- Dirty-write logs can be consumed and cleared after platform RAM sync.
- A wrapped dirty log no longer permanently degrades into full 64 KB syncs.

These changes reduce avoidable render-path work while keeping the physical
PHI2-only C64 policy intact.

## Regression Protection

v863 adds and updates guards around the actual bugs that caused silence or
release drift:

- `Sid808TargetRoutedDrumMidiV862Tests`
- `DrumBridgeNoSilenceV613Tests`
- `DrumBridgeMixPolicyV623Tests`
- `DigiMidiPadTransportStopV745Tests`
- `C64FinalCorrectnessV617Tests`
- `C64RenderStallInstrumentationV840Tests`
- `Sid808RestorePreloadDrainV803Tests`
- `Sid808StrictAuSlotPolicyV861Tests`
- `ReleasePackagingV702Tests`

These tests protect the bridge-routing authority, fail-open output ownership,
GM drum-note audition behavior, C64 render cleanup, dirty-log semantics, SID-808
restore/slot preload behavior, and clean release packaging.

## Release Artifacts

- Source zip:
  `dist/ArpSID-0.0.690-pass380-v863-sid808-logic-audio-release.zip`
- Patch:
  `dist/ArpSID-0.0.690-pass380-v863-sid808-logic-audio-release.patch`
- Final closure log:
  `release-logs/macos-closure-20260703-234236.log`

Source zip SHA256:

`6287f640349f12287ef74892f3678eb50a29a7be0c3e714686d6d80c04472998`

Patch SHA256:

`5aa874ff5a34f14a3222d0b51f810b9c74a4e00b78883af57a3973e709261f96`

## Remaining Distribution Gates

The source and local AUv2 release are closed. The remaining work is outside the
source-code release boundary:

- Apple notarization
- VST3 SDK/toolchain validation
- AUv3 product packaging and runtime validation
