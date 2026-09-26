# ArpSID DIGI Canonical `$D418` 4-Bit Nibble Technical Specification

Status: release contract for REC, import, preview, storage, AUTH playback, FAST playback, and debug telemetry.

This document is intentionally detailed. It exists to prevent future regressions where DIGI audio is accidentally treated as ordinary PCM/waveform audio instead of the canonical Commodore 64 SID `$D418` low-nibble volume-DAC stream.

---

## 1. One-sentence contract

A committed ArpSID DIGI user sample is **not PCM**. It is a bounded, canonical, 8 kHz stream of unsigned 4-bit SID `$D418` volume-DAC nibbles, physically stored as bytes whose valid value range is `0x00..0x0F`.

```text
host REC/import float mono
→ sanitize / prepare / optional normalize
→ resample to canonical 8000 Hz
→ quantize to unsigned 4-bit `$D418` nibble 0..15
→ store one nibble per byte
→ play as low nibble of SID register `$18` / absolute `$D418`
```

---

## 2. Terminology

### 2.1 `$D418`

`$D418` is the SID volume and filter mode register. In SID register-index form this is register `$18`.

```text
absolute C64 address: $D418
SID register index:   $18
```

The low nibble of `$D418` is the visible volume-DAC code:

```text
$D418 bit 0..3 = volume / DIGI sample nibble
$D418 bit 4..7 = filter/mode/high-nibble control state
```

### 2.2 Canonical nibble

A canonical DIGI frame carries one unsigned 4-bit value:

```cpp
std::uint8_t nibble; // valid range 0..15 only
```

Only the low nibble is meaningful. The upper nibble must be zero in stored canonical data.

### 2.3 Canonical stream

A canonical `$D418` stream is:

```cpp
frames[i] <= 0x0F
rateHz = 8000
```

Every frame corresponds to one scheduled low-nibble `$D418` update.

### 2.4 PCM8 in legacy structure names

Some historical structs still contain fields named `pcm`. In the canonical DIGI bank this does **not** mean free 8-bit PCM. It means a storage byte used to represent canonical DIGI material. The valid committed content is quantized from/to the `$D418` nibble ladder, and preview/playback must recover the nibble with helper functions such as `digiPcm8ToD418Nibble(...)`.

Do not render committed `clip.pcm[i]` using `clip.pcm[i] & 0x0F` unless the storage contract explicitly says it is already raw nibble bytes. The safe visual/playback path is through the canonical helper.

---

## 3. Format authority

The canonical constants live in:

```text
include/arpsid/gui/digi_record_limits.h
include/arpsid/gui/digi_sample_bank_v596.h
include/arpsid/engines/digi_d418_stream_engine.h
```

The release profile uses:

```cpp
kDigiUserSampleCanonicalRateHz = 8000;
kDigiUserSampleMaxFrames = 60000;
kDigiUserSampleMaxSeconds = 7.5;
```

Legacy profile:

```cpp
kDigiUserSampleMaxFrames = 8192; // 1.024 seconds at 8 kHz
```

Schema-v1 maximum because `frameCount` is `uint16_t`:

```text
65535 frames / 8000 Hz = 8.191875 seconds
```

The release deliberately stays below 8 seconds:

```text
60000 frames / 8000 Hz = 7.5 seconds
```

---

## 4. Storage structure contract

A user sample clip conceptually contains:

```cpp
struct DigiUserSampleClip {
    std::uint32_t handle;
    std::uint32_t sourceHash;
    std::uint32_t sourceSampleRateHz; // canonical samples use 8000
    std::uint16_t frameCount;
    std::uint8_t  flags;
    char          name[...];
    std::uint8_t  pcm[kDigiUserSampleMaxFrames];
};
```

Despite the field name `pcm`, committed sample data is canonicalized to the 4-bit `$D418` domain.

A valid committed user sample must satisfy:

```text
flags contains PRESENT
frameCount > 0
frameCount <= kDigiUserSampleMaxFrames
sourceSampleRateHz == 8000 for canonical committed clips
sample material is recoverable as 0..15 `$D418` nibbles
```

Strict validation may scan the clip and verify tail cleanliness. Realtime render lookup must use O(1) validated-snapshot lookup and must not scan 60000 bytes per audio block.

---

## 5. Capture pipeline

### 5.1 Source capture

REC may capture from:

```text
AudioQueue device-bound input
CoreAudio process tap / system audio capture
PureSID / internal render capture
imported file decode
```

Host/device capture may run at:

```text
44100 Hz
48000 Hz
96000 Hz
device-native rates
```

Capture is normalized internally to finite mono float for conversion.

### 5.2 Input sanitation

Every float entering the conversion path must be finite:

```cpp
if (!std::isfinite(x)) x = 0.0f;
x = std::clamp(x, -1.0f, 1.0f);
```

NaN and Inf are not audio. They must never reach quantization, meters, preview, or output buses.

### 5.3 Mono reduction

Multi-channel REC is reduced to mono before the DIGI conversion stage. Supported policies include:

```text
Ch1
Ch2
Ch1+2 mono
Ch3
Ch4
Ch3+4 mono
AllToMono
AutoStrongest
```

`AutoStrongest` is designed for interfaces where the signal may be on input 3/4 instead of 1/2. It must be sticky/hysteretic enough to avoid buffer-to-buffer channel hopping on noise.

### 5.4 Preparation

Recommended conversion stages:

```text
sanitize finite float
optional DC/bias removal
optional trim
optional normalize
optional soft-limit
resample to 8000 Hz
quantize to 4-bit unsigned `$D418`
bound to max frame count
set TRUNC flag if needed
```

---

## 6. Quantization math

### 6.1 Signed float to unsigned nibble

Input domain:

```text
x ∈ [-1.0, +1.0]
```

Mapping:

```cpp
float unsigned01 = (x * 0.5f) + 0.5f;  // -1..+1 → 0..1
int q = std::lround(unsigned01 * 15.0f);
q = std::clamp(q, 0, 15);
```

The stored canonical nibble is:

```cpp
std::uint8_t nibble = static_cast<std::uint8_t>(q);
```

### 6.2 Silence midpoint

Unsigned 4-bit `$D418` visual silence is approximately:

```text
0x08
```

The mathematical center between 0 and 15 is 7.5. Since the register value is integer, `0x08` is the practical displayed midpoint.

Preview must draw around this midpoint and not around arbitrary PCM zero.

### 6.3 Nibble to normalized preview value

For visual display:

```cpp
float visual = std::clamp((float(nibble) - 7.5f) / 7.5f, -1.0f, 1.0f);
```

This produces a visibly stepped 16-level ladder.

---

## 7. Preview contract

### 7.1 Preview must be 4-bit visual

The DIGI preview is a **4-bit `$D418` visualizer**, not an oscilloscope for raw host PCM.

Pending REC TAKE:

```text
host-rate mono float
→ finite clamp
→ digiFloatToD418Nibble(...)
→ draw nibble ladder around 0x08 midpoint
```

Committed sample:

```text
stored canonical clip byte
→ digiPcm8ToD418Nibble(...)
→ draw nibble ladder around 0x08 midpoint
```

The tooltip/status must make this visible to the user:

```text
TAKE→4BIT
canonical 8 kHz $D418
visualized as $D418 nibbles
```

### 7.2 Preview must not write state

The preview must not mutate:

```text
DSP state
sample bank
record buffer
AudioQueue state
SID engine state
```

The preview is GUI-only.

### 7.3 GPU/CoreAnimation safety

GPU acceleration means layer compositing and CoreAnimation/Metal-backed drawing. It does **not** mean GPU audio conversion.

Allowed:

```text
CAShapeLayer waveform
CALayer meters
CoreAnimation compositing
Metal capability marker
GUI-thread path generation
```

Forbidden:

```text
GPU work from audio thread
Objective-C UI work from audio callback
sample-bank writes from preview
DSP mutation from preview
```

---

## 8. Playback contract

### 8.1 AUTH C64-bus `$D418`

AUTH mode schedules PHI2-synchronous writes to SID register `$18` / address `$D418`.

The low nibble is the sample nibble:

```cpp
newD418 = (oldD418 & 0xF0) | (nibble & 0x0F);
```

Unless the engine intentionally owns the full register, high nibble must be preserved.

AUTH mode may respect:

```text
C64 IO bank visibility
open-bus semantics
SID register acceptance
write collisions
PHI2 timeline
```

### 8.2 FAST private `$D418`

FAST mode may bypass some C64-bus policy and feed a private SID/volume-DAC path, but it must preserve nibble semantics:

```text
input sample value = 0..15 only
```

### 8.3 Legacy float debug path

Legacy float playback is debug-only where enabled. It must still sanitize NaN/Inf and clamp output buses. It must not be the release authority for canonical DIGI storage or preview.

---

## 9. Realtime safety contract

The render thread must not do:

```text
heap allocation
Objective-C messaging
file IO
locks that can block on GUI/audio device threads
full 60000-byte clip validation scans
format conversion with unbounded work
```

Realtime render lookup uses O(1) validated-snapshot lookup:

```cpp
digiFindUserSampleClipRealtime(...)
```

Strict validation is only for:

```text
import
load/save
GUI validation
commit/KEEP
offline tests
```

---

## 10. NaN/Inf and clamp policy

Every numeric path that can touch audio output or preview must be finite-safe:

```cpp
if (!std::isfinite(v)) v = 0.0f;
```

Then clamp to the correct domain:

```text
sample input:       [-1.0, +1.0]
additive bus:       [-1.25, +1.25] if deliberate pre-limiter headroom
exclusive output:   [-1.0, +1.0]
telemetry/meter:    finite unit domain, usually [0.0, 1.0]
nibble:             [0, 15]
```

NaN must never survive:

```text
quantizeD418_
pcmToD418Nibble_
legacy float DIGI mix
AUTH private D418 mix
output-tap meters
REC/MON meters
preview path generation
```

---

## 11. Truncation contract

If conversion produces more canonical frames than the configured maximum:

```cpp
if (convertedFrames > kDigiUserSampleMaxFrames) {
    frameCount = kDigiUserSampleMaxFrames;
    flags |= kDigiUserSampleFlagTruncated;
}
```

The UI must say TRUNC or equivalent. It must not report the unbounded host-rate source length as if it were the persisted canonical length.

---

## 12. Error and empty states

### 12.1 Empty source

```text
frameCount == 0
```

UI:

```text
DIGI SLOT EMPTY
```

### 12.2 Silent source

A silent but non-empty take may be kept, but the UI should report low peak/RMS.

### 12.3 Invalid nibble

Stored values must be repaired or rejected. Strict import/load validation should not publish invalid data to the render snapshot.

---

## 13. Correctness examples

### 13.1 Pending REC visual

Wrong:

```cpp
// draws smooth raw PCM and hides 4-bit quantization
v = pendingMono[i];
```

Correct:

```cpp
std::uint8_t q = digiFloatToD418Nibble(pendingMono[i]);
float v = std::clamp((float(q) - 7.5f) / 7.5f, -1.0f, 1.0f);
```

### 13.2 Committed preview

Wrong:

```cpp
// assumes storage byte low nibble directly equals canonical nibble
q = clip.pcm[i] & 0x0F;
```

Correct:

```cpp
q = digiPcm8ToD418Nibble(clip.pcm[i]) & 0x0F;
```

### 13.3 `$D418` write

Correct:

```cpp
std::uint8_t nibble = q & 0x0F;
std::uint8_t next = (oldD418 & 0xF0) | nibble;
writeSIDRegister(0x18, next);
```

---

## 14. Release checklist

Before release, verify:

```text
REC TAKE preview says TAKE→4BIT
REC TAKE preview is stepped/quantized, not smooth PCM
KEEP converts TAKE to committed canonical 8 kHz $D418
Committed preview uses digiPcm8ToD418Nibble(...)
No clip.pcm[i] & 0x0F preview path exists
No NSView.tag assignment exists for custom preview view
No custom FULL/fullscreen HUD button exists
No render path calls full sample-bank strict validation
NaN/Inf tests pass for DIGI engines
AudioQueue invalid route/format stops instead of infinite re-enqueue
```

---

## 15. Human summary

ArpSID DIGI is a C64 SID volume-DAC sample system. The user may record or import normal audio, but the committed result is converted into a tiny, deterministic, 8 kHz, 16-level stream. The preview must show that 16-level `$D418` reality, not the original smooth host waveform. Playback must preserve the `$D418` register semantics: low nibble is sample, high nibble belongs to the SID/filter state unless the engine explicitly owns it.
