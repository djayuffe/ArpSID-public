# Playing and programming ArpSID's synth modes

This is a practical guide to the three ways ArpSID makes sound from notes:

- **CLASSIC**: a SID-based polysynth;
- **SYNTH / SID REG**: the C64's own SID, driven register by register;
- **DR SID**: the drum machines.

It covers when to use each, how every control behaves in each mode, recipes for classic
C64 sounds, and a troubleshooting table. The engine-level details are in
[internals/SYNTH_MODES.md](internals/SYNTH_MODES.md); every parameter is listed in
[PARAMETER_REFERENCE.md](PARAMETER_REFERENCE.md).

## Contents

1. [Which mode to use](#1-which-mode-to-use)
2. [Switching modes](#2-switching-modes)
3. [Controls, mode by mode](#3-controls-mode-by-mode)
4. [Voice modes in detail](#4-voice-modes-in-detail)
5. [The filter](#5-the-filter)
6. [Pitch: tune, bend, glide](#6-pitch-tune-bend-glide)
7. [SYNTH / SID REG in practice](#7-synth--sid-reg-in-practice)
8. [Recipes](#8-recipes)
9. [Troubleshooting](#9-troubleshooting)

---

## 1. Which mode to use

| You want | Use | Why |
|---|---|---|
| Pads, chords, big leads, anything with more than 3 notes | **CLASSIC** (Poly) | Up to 8 notes, each a full 3-oscillator SID voice (8 SID chips in the default Poly-Illusion topology) |
| A fat mono lead or bass | **CLASSIC** (Mono, Legato or Unison) | Three oscillators per note, detune, Unison stacks of up to 8 |
| The exact sound and limits of a real C64 | **SYNTH / SID REG** | One SID, three voices, one oscillator per note; every change is a real register write |
| C64 tracker-style playing: hard restart, register slides, fixed-step glide | **SYNTH / SID REG** | Notes become the same register programs a C64 music routine writes |
| To edit or automate raw SID registers | **SYNTH / SID REG** | The SID REG page *is* the chip |
| Drums | **DR SID** | DrSID register-program drums, SID-808 analog projection, GM drum map |
| One real SID with 3 voices, but CLASSIC's controls | **CLASSIC** + SETTINGS → Topology: single SID | Authentic single chip, 3 voices with stealing |

Arpeggiator: CLASSIC only. Step sequencer: CLASSIC and DR SID.

## 2. Switching modes

- **Editor header**: choose `CLASSIC`, `SYNTH / SID REG` or `DR SID`.
- **Parameters**: *Synth Mode (SID Reg)* and *DrSID Enable*. If both are on, DR SID wins.
- **AU flavors** lock the mode:

  | Flavor | Mode |
  |---|---|
  | *ArpSID Instrument* | SYNTH |
  | *DrSID* and *SID-808* | DR SID |
  | *C64 SID Player* | tune playback only |

- **Auto GM Drum Promotion** (off by default): when on, a channel-10 GM drum note switches
  a CLASSIC or SYNTH patch into DR SID. The dedicated drum flavors always do this.

What happens when you switch:

| Switch | Effect |
|---|---|
| to SYNTH | notes are released, ARP and SEQ turn off, the SID registers are loaded from the SID REG page |
| to DR SID | ARP turns off; SEQ keeps running (it drives the drum pattern) |
| back to CLASSIC | ARP and SEQ are off; turn them on again if you want them |

## 3. Controls, mode by mode

| Control | CLASSIC | SYNTH / SID REG |
|---|---|---|
| VCO 1–3 Waveform | one waveform per oscillator; the three are layered on every note | voice *n* uses VCO *n*'s waveform: each note plays on one SID voice with that voice's settings |
| VCO Pulse Width | 12-bit, live | 12-bit, written at note start and whenever the knob moves |
| VCO Detune | ±100 cents per oscillator | not used: SID voices play the note pitch |
| VCO Level | per-oscillator mix | per-voice mixer level |
| VCO Sync / Ring | per VCO: VCO1←VCO3, VCO2←VCO1, VCO3←VCO2 | same per voice, the real SID wiring |
| VCO LF mode | oscillator at LFO rates | not used |
| Attack / Decay / Sustain / Release | SID ADSR on every oscillator | SID ADSR written with each note (`$D4x5`, `$D4x6`), live |
| Filter Cutoff / Resonance / Mode | SID filter on every chip | `$D415–$D418`, live |
| Master Volume | output gain | the 4-bit `$D418` volume (0–15) |
| Master Tune | ±100 cents | ±100 cents |
| Voice Mode | Poly (8), Mono, Legato, Unison (1–8) | Poly (3), Mono, Legato, Unison (1–3) |
| Voice Spread | Unison count **and** detune | Unison count (capped to 3) |
| Portamento, Portamento Style, C64 Glide Delta | glide between notes | glide as a stream of frequency-register writes |
| Velocity | loudness (`√velocity`) | not used: the SID envelope sets the level |
| Channel / poly pressure | modulation sources | scale the sustain level of the pressed voices |
| Mod matrix (MACRO page) | applied | not applied to the registers |

## 4. Voice modes in detail

| Mode | CLASSIC | SYNTH |
|---|---|---|
| **Poly** | up to 8 notes; when all are busy, the oldest released tail is stolen first, then the oldest held note that no pedal holds | up to 3 notes (the SID's three voices); released tails first, then held notes, pedal-held notes last |
| **Mono** | one voice, last-note priority; each new note restarts the envelope | same, on SID voice 1 |
| **Legato** | like Mono, but a note played while another is held glides instead of restarting | same |
| **Unison** | `1 + int(Voice Spread × 7)` copies of the top note (1–8), spread symmetrically over ±24 cents × Voice Spread | `1 + int(Voice Spread × 7)` copies, capped to 3, at the same pitch |

**Voice Spread → Unison voices:**

| Voice Spread | 0 | 0.15 | 0.29 | 0.43 | 0.58 | 0.72 | 0.86 | 1.0 |
|---|---|---|---|---|---|---|---|---|
| CLASSIC voices | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
| SYNTH voices | 1 | 2 | 3 | 3 | 3 | 3 | 3 | 3 |
| CLASSIC detune range | 0 | ±3.6 ct | ±7 ct | ±10 ct | ±14 ct | ±17 ct | ±21 ct | ±24 ct |

In every mode, released notes ring out with their SID **release** phase. Sustain and
sostenuto pedals hold notes per MIDI channel.

## 5. The filter

**Modes.** The Filter Mode choice selects which SID filter outputs are active. Every mode
and every engine uses the same eight choices:

| Choice | What you hear |
|---|---|
| OFF | voices routed to the filter are **silent** (the real chip behaves this way); unrouted voices play dry |
| LOW-PASS | classic warm SID sound |
| BAND-PASS | nasal, vocal; good for drums and "wah" sweeps |
| LP+BP | low end plus a band peak |
| HIGH-PASS | thin, bright; hats and fizz |
| NOTCH (LP+HP) | a hollow, phasey cut at the cutoff |
| BP+HP | bright with a band peak |
| ALL | all three outputs mixed |

**Routing.** Which voices go through the filter is set by `$D417` bits 0–2 on the SID REG
page: V1 = 1, V2 = 2, V3 = 4. The default is 7, all three. It applies in CLASSIC and SYNTH.

**Chip character.** The 6581 filter is darker and distorts when driven. The 8580 is
cleaner and has more resonance. Choose the revision with *SID Chip Revision* on the SID REG page. It is mirrored on the FILTER, FORENSIC and OPTIONS pages. The
Filter Ohmic forensic control adds the resistor behaviour of the real ladder.

**Modulation.** Cutoff and resonance can be modulated from the MACRO page and the filter
envelope (CLASSIC). *Filter Drive* adds up to +12 dB of output drive (+4.5 dB in DR SID).

## 6. Pitch: tune, bend, glide

- **Master Tune**: ±100 cents.
- **Pitch bend**: 14-bit, per MIDI channel.
  - The range is set by RPN 0 (Pitch Bend Sensitivity): 0–48 semitones, default 2.
  - Bend layers on top of a running glide: the glide keeps its destination.
- **Portamento**: 0–5 s (the knob is quadratic, so small times get most of its travel).

**Portamento styles:**

| Style | Behaviour | Good for |
|---|---|---|
| `C64 SLIDE` | even register steps over the glide time, timed on SID cycles | tracker slides |
| `C64 FIXED` | a fixed register step (*C64 Glide Delta*, 1–255) every video frame (50 Hz PAL / 60 Hz NTSC); the glide time depends on the interval | authentic player-routine portamento |
| `LINEAR` | smooth, linear in semitones | modern synth glide |
| `SMOOTH` | `C64 SLIDE`'s steps timed on host samples | smooth but register-stepped |

- **Arp Glide Legato**: keeps the gate open between arpeggio steps so each step slides. It
  works in Mono, Legato and Unison, not Poly.

## 7. SYNTH / SID REG in practice

### 7.1 What a note does

Every note-on writes the classic C64 **hard restart** sequence:

1. frequency;
2. pulse width;
3. control byte with the gate low;
4. attack/decay and sustain/release;
5. 46 SID cycles later (about 47 µs), the control byte with the gate high.

The short gate-low window lets the envelope settle, so every note starts with the same
punchy attack, as a C64 music routine does.

Note-off writes the control byte with the gate low; the voice then rings out with its
release.

### 7.2 Editing registers directly

The SID REG page holds the 29 registers `$D400–$D41C`, plus the ArpSID system register
`$D41D`.

- In SYNTH mode, changing `$D400–$D418` writes straight to the chip. Automating one is
  automating the SID.
- `$D419–$D41C` (POTX, POTY, OSC3, ENV3) are read-only, as on the hardware.
- When you switch into SYNTH mode, the chip is loaded once from these values; after that,
  notes and knobs write their own registers.

| Register (voice 1; voice 2 = +7, voice 3 = +14) | Meaning |
|---|---|
| `$D400/$D401` | frequency low/high: `Hz × 2²⁴ / clock` |
| `$D402/$D403` | pulse width low / high nibble (12-bit) |
| `$D404` | control: gate `$01`, sync `$02`, ring `$04`, test `$08`, triangle `$10`, saw `$20`, pulse `$40`, noise `$80` |
| `$D405` | attack (high nibble) / decay (low nibble) |
| `$D406` | sustain (high nibble) / release (low nibble) |
| `$D415/$D416` | filter cutoff (11-bit: low 3 bits / high 8 bits) |
| `$D417` | resonance (high nibble) / filter routing (V1 1, V2 2, V3 4, EXT 8) |
| `$D418` | voice-3 off `$80`, HP `$40`, BP `$20`, LP `$10`, volume (low nibble) |

### 7.3 Expression in SYNTH mode

- **Channel pressure** scales the sustain level of the held voices from 25 % to 100 %.
  Pressing harder makes held notes louder, the way a C64 player routine rewrites the
  sustain nibble.
- **Poly pressure** does the same per note.
- **Mod wheel, breath, CC7, CC11 and the pedals** refresh the voices and bend.
- **Velocity** does not change the level: the SID has no velocity; the envelope decides.

## 8. Recipes

All values are normalized knob positions unless a unit is given.

**Hubbard-style hard-restart bass** (SYNTH, Mono):

- VCO1 SAW or PULSE (PW about 0.4);
- Attack 0, Decay 0.4, Sustain 0.6, Release 0.2;
- Filter LOW-PASS, cutoff about 0.35, resonance 0.5;
- Portamento 0.

Each note gets the hard restart, so fast lines stay punchy.

**Sync lead** (CLASSIC Mono, or SYNTH):

- VCO2 SAW, VCO2 Sync on (synced to VCO1);
- VCO1 a quiet triangle as the sync source;
- modulate VCO2 detune or pitch from LFO 1 on the MACRO page (CLASSIC) for the classic
  sweeping sync sound.

**Ring-mod bell** (CLASSIC or SYNTH):

- VCO1 TRIANGLE with Ring on (ring-modulated by VCO3);
- VCO3 TRIANGLE a fifth or a non-harmonic interval above (VCO3 detune in CLASSIC);
- Attack 0, Decay 0.6, Sustain 0, Release 0.6.

**C64 chord arpeggio** (CLASSIC):

- ARP on, mode UP, rate synced (knob at 0 = quarter notes, or a fast free rate about
  0.8 for "chip chords");
- Voice Mode Mono with *Arp Glide Legato* off for the stepped C64 sound; on for a slide.

**Supersaw-style stack** (CLASSIC Unison):

- VCO1–3 SAW;
- Voice Spread 0.72 (6 voices, ±17 cents);
- Filter LOW-PASS with some envelope amount.

**Tracker portamento** (SYNTH, Legato):

- Portamento Style `C64 FIXED`, C64 Glide Delta about 32 (the default);
- Portamento above 0;
- play overlapping notes: each glide moves by a fixed register step per frame, like a
  tracker's slide command.

**Filter sweep pad** (CLASSIC Poly):

- VCO1 PULSE with PWM from LFO 1;
- VCO2 TRIANGLE an octave down;
- Filter LOW-PASS, cutoff 0.2, Filter Env Amount 0.6, slow filter attack;
- Release 0.7.

## 9. Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| No sound at all | Filter Mode OFF while voices are routed to the filter (`$D417` bits 0–2) | choose LOW-PASS (or another mode), or clear the routing bits |
| No sound in SYNTH | VCO levels at 0, or volume (`$D418` low nibble) at 0 | raise the levels or Master Volume |
| Only 3 notes sound | SYNTH mode has 3 voices, like the real chip | use CLASSIC for more polyphony |
| Unison sounds thin | Voice Spread 0 gives one voice | raise Voice Spread; the voice count grows with it |
| ARP does nothing | ARP only runs in CLASSIC | switch to CLASSIC |
| Channel-10 notes turn my patch into drums | Auto GM Drum Promotion is on | turn it off (DRSID page → DRSID ENGINE) |
| Bend range is wrong | RPN 0 from the host or keyboard sets it per channel | send RPN 0 with the range you want (up to 48) |
| An old project sounds different | projects saved before 0.9.10 in SYNTH mode are migrated to keep their sound (see [CHANGELOG](../CHANGELOG.md)) | none needed; CLASSIC Unison at spread 0 now plays 1 voice, so raise Voice Spread |
