# ArpSID 0.0.690 pass380 v867 - SID-808 Snare And SIDPLAY Register Closure

Date: 2026-07-04

## Purpose

v867 is a post-authority audio correctness release. The v861-v866 sequence
fixed SID-808 routing, fail-open output ownership, bridge telemetry, and
scheduled hit observability. Those fixes proved that hits reached the bridge
and that the UI could report bridge authority, but two deeper sound-generation
problems remained:

1. SID-808 snares could sound like the wrong instrument because factory snares
   were authored as pulse-only or sustained one-shot voices.
2. C64 SIDPLAY could still sound distorted/wrong because its live register
   backend was not fully aligned with the corrected core SID path.

This closure fixes those sound-generation roots.

## SID-808 Root Cause

The SID-808 factory table had two drum-authorship problems:

- Several snares used pulse-only waveform control (`$40`) even though the
  intended snare sound needs a noise component.
- Factory one-shots used nonzero sustain nibbles. With no guaranteed gate-off,
  a one-shot could behave like a held synth note and drone after the hit.

That is why the bridge could be routed correctly, telemetry could move, and the
output could be audible, while the snare still sounded wrong.

## SID-808 Fix

Factory authorship is now one-shot safe:

- Classic Snare: `$C0` pulse+noise, sustain `0`
- Punch Snare: `$C0` pulse+noise, sustain `0`
- Lo-Fi Snare: `$80` noise, sustain `0`
- Hard Snare: `$C0` pulse+noise, sustain `0`
- Wide Snare: `$C0` pulse+noise, sustain `0`

All other factory SID-808 drum families also author zero sustain. The
deterministic variation layer still varies useful kit character, but it no
longer reintroduces sustain for one-shot drums.

The engine now also enforces one-shot gate lifetime:

```text
noteOn drum
  -> allocator marks the fixed physical SID voice
  -> GATE off
  -> write frequency / pulse width / waveform
  -> write ring, sync, filter routing
  -> write ADSR and voice level
  -> GATE on
  -> arm one-shot auto-release
  -> render chunks up to release boundary
  -> noteOff token at release boundary
  -> allocator frees voice after release tail
```

This ordering prevents partially programmed first-hit blocks, stale GATE state,
and long-lived one-shot sustain.

## SIDPLAY Root Cause

SIDPLAY uses `SidRegisterEngine` for live C64 register playback. Some earlier
core fixes landed in the corrected `SIDChip` path, but the register engine
still had two important parity gaps:

- Its pulse-width edge code treated `$FFF` as constant-low.
- C64 SIDPLAY did not consistently enable `$D418` volume-DAC emulation in the
  register engine path.

The result was a mismatch between what a C64 tune wrote and what the live
SIDPLAY backend produced.

## SIDPLAY Fix

`SidRegisterEngine` now routes pulse output through a helper with explicit edge
semantics:

```text
PW=$000 -> constant high
PW=$800 -> midpoint comparator
PW=$FFF -> final one-step high spike
```

The C64 SIDPLAY handoff/render path enables D418 volume-DAC emulation on the
register engines. The primary engine keeps its register image through handoff,
but the D418 DAC memory is reset so stale volume-DAC state does not leak across
player activation. Secondary engines are reset on handoff as before.

Subphase write queue pressure remains observable through existing telemetry:
overflow count, coalesced count, and dropped-oldest count.

## Behavioral Guards

`Sid808SnareOneShotV867Tests` proves the drum side:

- slots 120..149 all resolve to snares with the noise bit set;
- every factory one-shot drum has sustain nibble zero;
- every factory snare has audible attack RMS;
- every factory snare tail from 500 ms to 1000 ms is below 2 percent of attack
  RMS;
- every factory snare has transient zero-crossing density high enough to reject
  the old pulse-only shape;
- every rendered one-shot releases its active SID-808 voice.

`SidplayRegisterEngineV867Tests` proves the SIDPLAY side:

- `$000`, `$800`, and `$FFF` pulse-width comparator edges are pinned;
- D418 DAC output is silent when disabled and audible when enabled;
- the C64 SIDPLAY render path enables `setD418VolumeDacEmulation(true)`;
- the old `$FFF` constant-low source branch is absent;
- subphase write overflow/coalesce/drop telemetry increments under queue
  pressure.

## Validation

Focused validation:

```text
Sid808SnareOneShotV867Tests        PASS
SidplayRegisterEngineV867Tests     PASS
Focused SID808/SID-core set        22/22 PASS
Full CTest                         387/387 PASS
Direct AUv2 component smoke        PASS
Direct AUv2 SID-808 smoke          PASS
Direct AU3 SID-808 smoke           PASS
AUv2 install/cache/strict validate PASS
Installed AUv2 component smoke     PASS
Installed AUv2 SID-808 smoke       PASS
codesign verify                    PASS
auval -a flavor enumeration        ArIn, ArpS, C64P, DrSD, S808
```

The 22-test focused set includes SID core exactness/audit guards, D418 guards,
SID-808 bridge no-silence/mix/runtime-authority guards, target-routed MIDI,
audible-authority telemetry, restore preload/drain, and strict AU slot policy.

Installed AUv2 binary SHA256:
`878f6216738f820d2d752affb886a03861090f8ceb5994d2a4da73d63fef8ccc`.

## Expected Listening Result

SID-808 snares should now have a noisy transient and clean one-shot decay. A
snare pad or sequenced snare should not collapse into a plain pulse tone, and
factory one-shots should not leave a sustained drone behind the pattern.

C64 SIDPLAY should preserve the audible edge cases that matter to real tunes:
extreme pulse-width writes retain the comparator edge, and `$D418` volume nibble
changes can produce audible digi/percussion output through the register engine.

## Carried Forward

The earlier authority fixes remain active:

- v861 fail-open bridge output replacement;
- v862 target-routed canonical drum MIDI;
- v865 bridge telemetry and unified audible-authority display;
- v866 scheduled hit telemetry;
- v864 SID-core exactness closure.
