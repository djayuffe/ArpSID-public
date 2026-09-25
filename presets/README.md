# ArpSID — Original C64-auth sound presets (JSON)

Three importable, human-authorable C64-authentic sounds in the **Classic C64 JSON**
format:

| File | Sound | Chip | Notes |
|------|-------|------|-------|
| [C64_Pulse_Lead.json](C64_Pulse_Lead.json) | Pulse lead | 6581 | Single 50% pulse, fast attack, LP filter bite — the classic SID solo voice. |
| [C64_Punch_Bass.json](C64_Punch_Bass.json) | Punch bass | 6581 | Narrow pulse, tight decay, low closed LP — dry woody SID bottom end. |
| [C64_Ring_Bell.json](C64_Ring_Bell.json) | Ring bell | 8580 | Triangle V1 ring-modulated by triangle V3 (detuned fifth), BP shimmer — iconic metallic SID bell. |

## How to import

In the ArpSID editor, use the bank panel's **Import JSON** action and choose one of
these files. Each file declares one sound in `patches[0]`, so it loads into bank
slot 0; remaining slots fall back to the factory bank. (You can also concatenate
several sounds into a single file's `patches` array to load slots 0, 1, 2, …)

Classic JSON imports are routed to the **BitPerfect** play engine, where the
high-level voice/filter parameters below are the live audio authority and MIDI
drives the note pitch.

## Classic C64 JSON format

```jsonc
{
  "ArpSIDFormat": "ClassicC64JSON",   // optional marker; any non-"BankJSON" value works
  "patches": [                         // required: array of sounds (max 128 -> slots 0..127)
    {
      "name": "C64 Pulse Lead",        // patch display name
      "category": "Lead",              // free text; "drum" routes to the DrSID engine
      "chip": "6581",                  // "6581" or "8580"
      "volume": 15,                    // master volume nibble 0..15
      "voice1": {                      // voice1 / voice2 / voice3 (omit a voice to leave it silent)
        "wave": "pulse",               // triangle | saw | pulse | noise | triangle_pulse
        "pw": 2048,                    // pulse width, 12-bit 0..4095 (2048 = 50%)
        "detune": 0,                   // -48..+48 around centre
        "sync": false,                 // hard-sync this voice from its SID source voice
        "ringmod": false,              // ring-modulate this voice from its SID source voice
        "level": 15,                   // mixer level 0..15 (default 15 if omitted)
        "ad": 6,                       // SID $D4x5 attack/decay byte: (attack<<4)|decay
        "sr": 168                      // SID $D4x6 sustain/release byte: (sustain<<4)|release
      },
      "filter": {
        "cutoff": 8500,                // 0..16383
        "res": 10,                     // resonance 0..15
        "mode": "lp"                   // lp | bp | hp
      },
      "extra": { "drum_mode": false }  // optional; true forces the DrSID drum engine
    }
  ]
}
```

SID ring/sync source topology is cyclic: voice 1's source is voice 3, voice 2's
source is voice 1, voice 3's source is voice 2. The ring/sync source voice keeps
oscillating even when muted — author its waveform (and optionally a low `level`)
so the modulation has a source, as `C64_Ring_Bell.json` does with voice 3.
