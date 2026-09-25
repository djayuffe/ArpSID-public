# ArpSID 0.0.690 pass380 v868 - SID-808 Snare Complete Closure

## Scope

This note closes the remaining SID808 snare audit items after v867:

1. SID808 audible-minimum regression coverage must keep every drum audible.
2. SID808 waveform/control-bit conversion must stay guarded.
3. SID808 bridge/output telemetry must still expose routed hit, configured kit,
   and output peak.
4. The audible-authority display must remain unified.
5. Snare must sound like a drum, not a held SID pulse oscillator.

v867 fixed the factory table and one-shot lifetime problems. v868 completes the
live render path and the shared-voice routing rules.

## Root Cause

SID808 used `SingleSidThreeVoiceEngine::forceNoteOnVoice()` to reserve a fixed
physical SID voice. That method is correct for melodic single-SID playback, but
it also programs and gates the default melodic voice before returning.

For drums this is the wrong authority order:

1. reserve voice,
2. briefly program/gate default melodic registers,
3. rewrite the voice as SID808,
4. gate the SID808 hit.

That ordering can leak the wrong first-sample ownership and makes the snare
dependent on a generic note-on path that was never meant to be a drum renderer.

## Fix 1 - Allocator-Only Forced Ownership

Added:

```cpp
forceNoteOnVoiceBookkeepingOnly(...)
```

This updates allocator, choke, note identity, key-down, sustain, and sostenuto
bookkeeping, but it does not write frequency, pulse width, waveform, ADSR, level,
or gate to the SID voice.

`Sid808Engine` now uses that path for every hit. SID808 is the first and only
owner of the audible register programming for its drum voices.

## Fix 2 - Snare Snap/Body Microprogram

Snare now renders as a two-stage SID808 program:

- Stage 1: high-frequency noise-only snap.
- Stage 2: authored body restored after about 7.5 ms.

The body stage is scheduled on the render timeline. `processBlock()` now chunks
at the next scheduled event, which can be either a micro-stage boundary or a
one-shot release boundary. That means the snap-to-body transition is not rounded
up to the next host buffer.

The body stage does not raise a new gate. It changes the oscillator and envelope
registers under the original drum gate so the hit remains one one-shot event.

## Fix 3 - Runtime Snare Sanitizer

Snare config is now fail-closed:

- waveform always includes noise,
- sustain nibble is forced to zero,
- filter routing is enabled.

This applies after kit data, GUI conversion, scheduled hit overrides, and
per-hit overrides. A pulse-only sustained snare override can no longer recreate
the v865/v867 bad snare.

## Fix 4 - Factory Filter Authorship

Every base SID808 factory snare now sets `Sid808Detail::kFlagFilter`.

Because slot variation preserves flags, every resolved slot 120..149 carries the
snare filter route as authored kit data.

## Fix 5 - Shared Voice Routing

SID808 voice ownership is fixed by drum family:

- Kick/Tom use physical SID voice 0.
- Snare/Clap/Rim use physical SID voice 1.
- Hats/Cowbell use physical SID voice 2.

That makes voice 1 a shared drum lane. The first v868 implementation correctly
marked snare as filtered, but the full CTest pass caught a regression:

`Sid808AudibleAuthorityV865Tests` failed because a Clap override inherited the
snare filter route on voice 1 and became too quiet.

The final routing law is:

- the current physical voice is routed from the current hit's config,
- other physical voices may preserve their table-authored filter routes,
- a snare row cannot force the current Clap/Rim hit through the snare filter.

This keeps snare shaping without breaking shared-voice audible authority.

## Guard Coverage

`Sid808SnareCompleteClosureV868Tests` verifies:

- allocator-only forced SID voice ownership exists,
- SID808 does not call the old default-gating `forceNoteOnVoice()` path,
- every resolved factory snare in slots 120..149 contains noise,
- every resolved factory snare keeps zero sustain,
- every resolved factory snare has the filter flag,
- pulse-only sustained snare overrides are sanitized at runtime,
- the live voice starts as a noise-only snap,
- the live voice switches to the authored body after the scheduled stage.

Existing guards still pass:

- `Sid808AudibleAuthorityV865Tests`
- `Sid808SnareOneShotV867Tests`
- `Sid808TargetRoutedDrumMidiV862Tests`
- `DrumBridgeNoSilenceV613Tests`
- `SidplayRegisterEngineV867Tests`

## Validation

Completed locally on 2026-07-04:

- focused SID808/factory/bridge CTest set: 35/35 PASS
- full CTest: 388/388 PASS
- AUv2 build/install/cache refresh/strict validation: PASS
- codesign verification: PASS
- `auval -a` lists:
  - `aumu ArIn ASID - Uber Sound Solutions: Pure Instrument`
  - `aumu ArpS ASID - Uber Sound Solutions: ArpSID`
  - `aumu C64P ASID - Uber Sound Solutions: C64 SID Player`
  - `aumu DrSD ASID - Uber Sound Solutions: DrSID Drum Machine`
  - `aumu S808 ASID - Uber Sound Solutions: SID-808`

Installed AUv2 binary SHA256:

`fc863eccdc85890b8dc896256a0febf7897f986975ae1fc3b591e640a6907304`
