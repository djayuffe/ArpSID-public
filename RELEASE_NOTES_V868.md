# ArpSID 0.0.690 pass380 v868 Release Notes

## Summary

v868 is the SID-808 snare complete-closure release. v867 fixed the obvious
factory snare errors - pulse-only tables, nonzero sustain, and missing one-shot
release. v868 fixes the remaining deeper problem: the live SID808 snare path
still allowed the default single-SID melodic voice to own the first gate edge,
and it did not have a real snare transient/body program.

The result is a more drum-like SID-808 snare: a noise snap at the front, an
authored pulse+noise or noise body after the transient, no sustained pulse
drone, and no accidental silent/quiet shared-voice side effects for Clap/Rim.

## Fixed

- Added allocator-only forced voice ownership to
  `SingleSidThreeVoiceEngine::forceNoteOnVoiceBookkeepingOnly()`.
- Switched `Sid808Engine` to that allocator-only ownership path so SID808
  updates voice allocation/choke/note-off state without first calling the
  default melodic note-on programming path.
- Added a SID808 snare microprogram:
  - initial noise-only snap,
  - high SID frequency drive for the snap,
  - zero pulse width during the snap,
  - scheduled body-stage switch after about 7.5 ms,
  - body stage restores the authored snare frequency, pulse width, waveform,
    envelope, filter route, and level without issuing a second gate rise.
- Extended SID808 block scheduling so render chunks stop at both snare
  micro-stage boundaries and one-shot auto-release boundaries.
- Added runtime snare sanitization:
  - pulse-only snare overrides get noise restored,
  - nonzero snare sustain nibbles are zeroed,
  - snare filter routing is enabled even when an override omits it.
- Marked factory SID-808 snare configs with `Sid808Detail::kFlagFilter` across
  every authored factory family and every resolved slot 120..149.
- Added snare-specific filter shaping using the SID filter's band-pass +
  high-pass mode.
- Fixed the shared voice-1 filter route interaction: Snare, Clap, and Rim share
  physical SID voice 1, so the current hit now owns that physical voice's route.
  A snare row in the kit table can no longer make a current Clap/Rim hit too
  quiet by leaving voice 1 routed through the snare filter.

## Why this matters

The broken snare was not just a bad preset row. It was a stack of ownership and
timing issues:

- factory tables could describe the wrong kind of sound,
- overrides could reintroduce pulse-only sustained snare data,
- the allocator path could briefly program/gate a default melodic voice before
  SID808 rewrote the registers,
- no transient/body split existed for the snare,
- shared physical voice routing could make non-snare hits inherit snare filter
  state.

v868 closes all five layers in the source, the runtime behavior, and the tests.

## Tests

New guard:

- `Sid808SnareCompleteClosureV868Tests`

Retained guards that passed with the new behavior:

- `Sid808AudibleAuthorityV865Tests`
- `Sid808SnareOneShotV867Tests`
- `Sid808TargetRoutedDrumMidiV862Tests`
- `DrumBridgeNoSilenceV613Tests`
- `SidplayRegisterEngineV867Tests`

Final validation:

- focused SID808/factory/bridge CTest set: 35/35 PASS
- full CTest: 388/388 PASS
- AUv2 build/install/cache refresh/strict validation: PASS
- codesign verification: PASS
- `auval -a` lists `ArIn`, `ArpS`, `C64P`, `DrSD`, `S808`

Installed AUv2 binary:

`fc863eccdc85890b8dc896256a0febf7897f986975ae1fc3b591e640a6907304`

## Files Of Interest

- `include/arpsid/engines/single_sid_three_voice_engine.h`
- `include/arpsid/engines/sid808_engine.h`
- `include/arpsid/patchbank/factory_sid808_kits.h`
- `source/tests/sid808_snare_complete_closure_v868_tests.cpp`
- `CMakeLists.txt`
