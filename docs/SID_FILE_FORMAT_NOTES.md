# SID file format notes (PSID / RSID) — ArpSID reference

How ArpSID interprets the HVSC SID file format, including the tune-class
behaviours ("known hacks") the player implements. Cross-references the
implementation so claims stay verifiable.

## Header layout (big-endian)

| Offset | Size | Field | Notes |
|-------:|-----:|-------|-------|
| $00 | 4 | magic | `PSID` or `RSID` |
| $04 | 2 | version | 1–4 accepted (`psidParse` rejects others) |
| $06 | 2 | dataOffset | $76 (v1) / $7C (v2+); smaller = `BadOffset` |
| $08 | 2 | loadAddress | 0 = first two payload bytes are the little-endian load address |
| $0A | 2 | initAddress | 0 → effective load address |
| $0C | 2 | playAddress | 0 = tune drives itself from interrupts (continuous machine runner) |
| $0E | 2 | songs | 1–256; 0 rejected |
| $10 | 2 | startSong | 1-based; must be ≤ songs |
| $12 | 4 | speed | Per-song timing bits, see below |
| $16 | 32 | name | Latin-1, not necessarily NUL-terminated |
| $36 | 32 | author | |
| $56 | 32 | released | |
| $76 | 2 | flags (v2+) | See below |
| $78 | 1 | startPage / $79 pageLength | Relocation info (informational) |
| $7A | 1 | secondSIDAddress (v2NG+) | `(addr >> 4) & $FF`, valid $42–$FE even, D4xx/D5xx/D6xx/D7xx ranges |
| $7B | 1 | thirdSIDAddress (v3+) | |
| $7C/$7D | 1+1 | fourth/fifthSIDAddress (v4 extension) | parsed when dataOffset ≥ $7E |

A header-only file (zero payload) is rejected (`TooShort`, v849). RAM load is
clamped to the 64 KiB window (`psidLoadIntoRam`).

## speed field (per-song timing)

Bit N (0-based) selects timing for song N+1; songs above 32 use bit 31.

- Bit = 0 — **VBI**: play is called at the video frame rate. ArpSID uses the
  PHYSICAL VIC frame length (PAL 63×312 = 19656 cycles ≈ 50.1245 Hz; NTSC
  65×263 = 17095 ≈ 59.83 Hz) — see `c64_timing_math.h` for the explicit policy
  versus the 50.000 Hz "compatibility" cadence (19705), which is the CIA number.
- Bit = 1 — **CIA**: play is driven from CIA1 Timer A. The environment provides
  the default 50/60 Hz latch, but the tune's init MAY reprogram $DC04/$DC05
  (multi-speed 2x/4x, tracker tempos). ArpSID preserves a tune-programmed latch
  (reset latch $FFFF is the "untouched" sentinel, v856) and derives the service
  cadence from the LIVE latch every block, so mid-song tempo changes follow.

RSID ignores the speed field entirely: the tune installs its own interrupts and
is run by the continuous machine.

## flags (v2+)

- bit 0: BASIC flag (RSID: tune starts via BASIC — ArpSID rejects these
  fail-clean rather than executing BASIC as machine code)
- bit 1: PlaySID/C64 compatibility (RSID must have it 0)
- bits 2–3: video standard — 00 unknown, 01 PAL, 10 NTSC, 11 either.
  ArpSID uses the file's value when present, else the active variant profile.
- bits 4–5: SID model — 00 unknown, 01 6581, 10 8580, 11 either.
- bits 6–9 (v3+/v4): second/third SID model bits.

## RSID constraints

`loadAddress` raw field and `playAddress` must be 0 (`BadRsidHeader` otherwise);
load address comes from the payload; the tune is entered once via init and then
runs on real CIA/VIC interrupts through the PHI2 machine. Strict RSID refuses to
fall back to instruction-atomic execution (`StrictRsidNotPhi2` accounting).

## Init/play calling conventions (what tunes actually rely on)

- **Init**: entered with A = song number (0-based). ArpSID clamps the selected
  song to [1, songs] (v849) and dispatches through a RAM bootstrap at $0334.
- **Play (VBI)**: dispatched as `JSR play / JMP halt`. Tunes may exit with:
  - `RTS` — normal return;
  - `BRK` — accepted after a SID write (compatibility, sentinel trap);
  - `RTI` — a large class written for IRQ-entry players. The RTI consumes the
    2-byte JSR frame + one deeper byte as an IRQ frame; ArpSID detects the exact
    signature (RTI retired with SP == entrySP+1) and accepts the frame (v856).
    Tune-internal nested IRQ/RTI pairs never match the signature.
- **Play (CIA)**: entered through a genuine CIA1 Timer A IRQ frame via the
  $0314 vector trampoline (ack $DC0D, JSR play, restore regs, RTI), with
  completion proven by returned-to-idle (`serviceComplete`, v855).

## Memory model behaviours ("known hacks") the player honours

- **Write-through under ROM**: CPU writes to $A000–$BFFF / $E000–$FFFF always
  hit the RAM underneath regardless of banking (`decodeCpuWrite` routes all
  non-I/O writes to RAM; ROM is never a write target). Tunes freely keep data
  under KERNAL/BASIC.
- **$00/$01 processor port**: banking honoured for reads (LORAM/HIRAM/CHAREN,
  Ultimax); port writes also write through to RAM at $0000/$0001.
- **I/O-hidden SID stores**: with I/O banked out, a $D400-range write goes to
  RAM (counted as `ioHiddenSidStoresToRam`), not to the SID.
- **$DC0D/$DD0D ICR**: read-to-acknowledge semantics; the CIA bootstrap ack's
  before dispatching play.
- **SID register holes / read-only regs**: writes to $D419–$D41D handled per
  hardware ($D41D is ArpSID's pseudo/system byte via `writeSystemByte`);
  $D41B/$D41C readback served by the readback model (OSC3 = top 8 waveform
  bits, PW=$FFF spike included, v855).
- **Timed-write authority**: every play-routine SID write reaches the audio
  renderer with its exact PHI2 cycle stamp (v855) — playback is not
  block-quantized.

## Multi-SID (v2NG/v3/v4)

Second..fifth SID addresses are validated (even, in-range, not the $D8xx color
region) and mapped via `configurePsidSidBases()`. Secondary-chip writes travel
through the timed-write queue with chip indices.

## Where the boundaries are

`C64_EXACTNESS_BOUNDARIES.md` remains authoritative for what is physically
exact versus approximated (VIC bus-steal half-cycle contention, CIA model,
open-bus). Strict RSID never silently downgrades; observed risks set the
downgrade ledger.
