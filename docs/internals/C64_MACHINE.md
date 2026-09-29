# C64 machine — low-level reference

The emulated Commodore 64 that plays `.sid` tunes: a PHI2-cycle machine with a
bus-cycle-accurate 6510, VIC-II, two 6526 CIAs, the PLA memory map, open bus,
colour RAM, a timed SID bridge and SID readback, plus the PSID/RSID loader and
runtime that boots, initialises and plays tunes on it. It lives in
`include/arpsid/core/c64_*.h` and `psid_header.h` (about 13 500 lines).

What the player claims and does not claim is in
[C64_EXACTNESS_BOUNDARIES.md](../C64_EXACTNESS_BOUNDARIES.md); the file format
in [SID_FILE_FORMAT_NOTES.md](../SID_FILE_FORMAT_NOTES.md); rollback in
[REALTIME_ROLLBACK_JOURNAL.md](../REALTIME_ROLLBACK_JOURNAL.md).

| Source | Contents |
|---|---|
| `c64_phi2_machine.h` | `C64Phi2Machine`: one φ2 cycle of CPU + CIA + VIC + bus |
| `c64_cpu6510_micro.h` | `Cpu6510Micro`: bus-cycle microsequencer for all 151 official and the stable illegal opcodes |
| `c64_vic.h` | `VicII`: raster, badlines, sprite DMA, BA/AEC, raster IRQ |
| `c64_cia.h` | `Cia6526`: timers, TOD, ICR, serial, PB6/PB7 |
| `c64_pla.h`, `c64_processor_port.h`, `c64_memory_matrix.h` | memory visibility, ROM identity, `$00/$01`, RAM/ROM/IO/colour RAM decode |
| `c64_open_bus.h` | capacitive open-bus latch with bit decay |
| `c64_sid_bridge.h`, `c64_sid_readback.h`, `c64_sid_bus_sink.h`, `c64_sid_projection_bridge.h` | timed `$D400` writes into the audio path; PHI2-clocked OSC3/ENV3 readback |
| `c64_sid_mix.h` | multi-SID gains and PSID per-chip model hints |
| `c64_psid_runtime.h`, `psid_header.h` | `C64Runtime`, `PsidParser`, exactness ledger, render transactions |
| `c64_platform.h` | `C64Platform`: boot surface, bootstraps, ROM set, inspection mirror |
| `c64_timing_math.h`, `c64_phi2_types.h`, `c64_phi2_audio_scheduler.h`, `c64_fixed_write_scheduler.h` | clocks, frame lengths, sample ↔ cycle scheduling |
| `c64_telemetry.h`, `c64_boot_trace.h`, `c64_phi2_trace.h` | telemetry snapshot, opt-in boot tracer, per-cycle trace sink |
| `c64_rom_cache_manager.h`, `c64_embedded_rom_loader.h`, `c64_embedded_roms.h` | ROM supply (placeholders in public source) |
| `c64_cia.h`, `c64_peripherals.h`, `c64_cartridge.h` | IEC lines, cartridge GAME/EXROM |

---

## 1. Clocks and frame timing

| | PAL | NTSC |
|---|---|---|
| φ2 clock | 985 248 Hz | 1 022 727 Hz |
| VIC-II | 6569: 63 cycles × 312 lines | 6567: 65 cycles × 263 lines |
| VIC frame | 19 656 cycles (≈ 50.1245 Hz) | 17 095 cycles (≈ 59.83 Hz) |
| PSID CIA latch | round(985248/50) = 19 705 | round(1022727/60) = 17 045 |

Two play cadences exist and are never mixed (`C64TimingMath`, static-asserted):

- **VBI tunes** (speed bit 0) are played every **physical VIC frame**
  (19 656 / 17 095 cycles), the cadence a real raster interrupt gives.
- **CIA tunes** (speed bit 1) use the CIA1 Timer A latch of the tune, or the
  50/60 Hz compatibility latch above when the tune does not program one.

`psidPlayPeriodSamplesFromCycles(cycles, sampleRate, phi2Hz)` converts either
into a host-sample period. `Phi2AudioScheduler::cyclesForNextSample()` hands
out whole φ2 cycles per host sample, carrying the fractional debt so the
machine never drifts against the audio clock.

---

## 2. The φ2 machine (`C64Phi2Machine`)

`tickPhi2()` advances exactly one φ2 cycle, in this order:

1. Decay the open-bus latch to this cycle.
2. Tick **CIA1** and **CIA2** one cycle.
3. Tick the **VIC-II** (full model, or `stepHalfCycles(2)` in *VIC fast* mode).
4. Wire the interrupt lines: `IRQ = CIA1 | VIC`, `NMI = CIA2`, `RESET`.
5. Bus arbitration: **BA** (the VIC's three-cycle early warning) drives the
   CPU's **RDY**; **AEC** is the actual bus grant. Keeping them separate is what
   makes badline/sprite steals exactly as long as on hardware.
6. Unless CPU execution is suppressed, `cpu.tickPhi2Begin()` issues a bus
   request; the memory matrix performs the read or write (tagging SID accesses,
   RMW dummy/final writes, stack and vector fetches, interrupt entries); then
   `cpu.tickPhi2End(data)`.
7. Unless *6510 fast* is on, copy ~40 diagnostics (CIA timer phases, IRQ/NMI
   edges, raster position, badline/sprite/stolen cycles, open-bus reads, dirty
   writes, unsupported/approximate opcode counts, jam reason).
8. Report the cycle to an attached trace sink; `++phi2`.

`Phi2MachineConfig`: `video` (PAL/NTSC), `runtimeMode` (`PSIDFast`,
`RSIDStrict`, `RawMachine`), `deterministicPowerRam`, `enableVicBusSteal`,
`enableCartridge`, `traceEnabled`, `vicFast`, `cpuFast`.

- **VIC fast** (`VIC:FAST` button): skips sprite/badline DMA servicing and bus
  steals but keeps the raster counter, so `$D011/$D012` reads still work. Only
  tunes that depend on cycle-exact badline timing are affected.
- **6510 fast** (`6510:FAST`): skips only the diagnostics snapshot; CPU, CIA,
  VIC and SID stay bit-exact. Only the on-screen telemetry becomes coarse.
- `setCpuExecutionSuppressed()` freezes the 6510 for passive stepping while
  CIA/VIC time advances; it is distinct from a real JAM.

Snapshots (`captureSnapshot` / `restoreSnapshot`) capture the whole machine for
render transactions.

When no real ROMs are present, `installDeterministicKernalVectors_()` installs a
minimal HLE vector surface so player code can run:

| Address | Stub |
|---|---|
| `$FFFA → $FE43` | `JMP ($0318)` (NMINV) |
| `$FFFC → $E000` | safe reset/init stub |
| `$FFFE → $FF48` | `PHA / TXA / PHA / TYA / PHA`, `JMP ($0314)` (CINV) |
| `$EA31` | `LDA $DC0D`, `PLA/TAY`, `PLA/TAX`, `PLA`, `RTI` |
| `$FE47` | `LDA $DD0D`, `RTI` |
| `$FF8D` (RESTOR) | copy `$FD30–$FD4F` to `$0314–$0333` |

---

## 3. CPU (`Cpu6510Micro`)

A bus-cycle microsequencer: every `tickPhi2Begin()` / `tickPhi2End()` pair is
one φ2 cycle, with the exact read/write/dummy-access pattern of each addressing
mode.

- **Opcodes**: all 151 official opcodes and all stable NMOS illegal opcodes
  (NOPs with operands, `SLO`, `RLA`, `SRE`, `RRA`, `SAX`, `LAX`, `DCP`, `ISC`,
  `ANC`, `ALR`, `ARR`, `SBX`, …). `KIL` jams the CPU.
- **Approximate illegal opcodes** (chip-dependent results): `XAA $8B`,
  `LAX# $AB`, `AHX $93/$9F`, `SHY $9C`, `SHX $9E`, `TAS $9B` (and `ARR $6B`
  on the ledger, although its result is value-exact). The
  `ApproximateOpcodePolicy` is `Allow` (best effort, counted) or `Jam` (halt,
  used by strict RSID).
- **State** (`Cpu6510MicroState`): `PC A X Y SP P`, current opcode, micro-step
  `t`, operand/address latches, page-cross flag, RMW temporaries, reset / IRQ /
  NMI / BRK sequence flags, IRQ level, NMI edge latch, RDY/AEC.
- **Interrupts** are modelled at bus-cycle resolution. IRQ is level-sensitive,
  NMI edge-latched. `CLI`, `SEI` and `PLP` change the I flag *after* the
  interrupt poll for the next boundary (a one-shot delayed I value), `RTI` takes
  effect at once — the real 6502 behaviour.
- **RDY**: read cycles stall while RDY is low; writes complete (the NMOS rule).
- **Jam reasons** (`CpuJamReason`): `KilOpcode`, `UnsupportedOpcode`,
  `ApproximateOpcodeStrictPolicy`, `TrapBrkAsJam` (init completion sentinel).
- Counters: retired instructions, IRQ latches, NMI edges, unsupported and
  approximate opcodes (per opcode), last jam reason and opcode.

Power-on: `SP = $FD`, `P = $34`, processor port `DDR = $2F`, data `$37`.

---

## 4. Memory map

### 4.1 Processor port (`$00/$01`)

`ProcessorPort6510`: `$00` is the DDR, `$01` the data. Reads return
`$C0 | (data & ddr) | (~ddr & $3F)` (inputs float high). Bits 0–2 are
**LORAM**, **HIRAM**, **CHAREN**.

### 4.2 PLA decode

| Range | Visible device |
|---|---|
| `$A000–$BFFF` | BASIC ROM when LORAM and HIRAM, else RAM |
| `$D000–$DFFF` | I/O when CHAREN and (LORAM or HIRAM); character ROM when !CHAREN and (LORAM or HIRAM); RAM when both LORAM and HIRAM are low |
| `$E000–$FFFF` | KERNAL ROM when HIRAM, else RAM |
| cartridge | GAME/EXROM lines select 8K/16K/Ultimax maps (`C64VisibleDevice::CartridgeLo/Hi`) |

RAM always exists underneath ROM and I/O, and writes to ROM areas land in RAM.

### 4.3 I/O page

| Range | Device |
|---|---|
| `$D000–$D3FF` | VIC-II (registers repeat every 64 bytes) |
| `$D400–$D7FF` | SID (repeats every 32 bytes; extra SIDs at the configured bases) |
| `$D800–$DBFF` | colour RAM: a separate 4-bit array; reads return the high nibble from the open bus |
| `$DC00–$DCFF` | CIA1 |
| `$DD00–$DDFF` | CIA2 (PA0/PA1 select the VIC bank) |
| `$DE00–$DFFF` | I/O 1/2 (cartridge; open bus otherwise) |

`MemoryMatrix` records **dirty writes** (address, value, cycle) so the
inspection mirror and render transactions can be reconciled.

### 4.4 ROMs

`C64RomSet` holds BASIC (`$A000`, 8 KB), KERNAL (`$E000`, 8 KB) and CHARGEN
(`$D000`, 4 KB). A ROM's trust is derived from its **CRC32**, never from a
caller flag:

| ROM | Known images |
|---|---|
| BASIC | 901226-01 |
| KERNAL | 901227-01, -02, -03; Japanese 906145-02; Swedish 325017-02; Swedish VIP64; SX-64 251104-04 |
| CHARGEN | 901225-01; Japanese; Swedish 325018-02 |

Public source releases ship zero-filled placeholders
(`c64_embedded_roms.h`). `C64RomCacheManager` loads user-supplied images off
the audio thread (exact sizes, rejects HTML/error pages, persistent cache); it
never fetches ROMs by itself.

### 4.5 Open bus

`OpenBusLatch` keeps the last bus value observable for the documented
persistence window (about `$1D00` φ2 cycles) with deterministic bit decay, so
reads of unmapped I/O are reproducible. Every such read is counted.

---

## 5. VIC-II (`VicII`)

Models the bus-facing state that matters to a 6510 workload:

- **Raster**: 312 × 63 (PAL) or 263 × 65 (NTSC); `$D011` bit 7 and `$D012`
  read the raster line; `$D019` IRQ status (bit 7 = any enabled), `$D01A`
  enable. The raster IRQ fires once per line when `raster == compare` at cycle
  0.
- **Badlines**: when DEN was set on line `$30`, lines `$30–$F7` with
  `(raster & 7) == YSCROLL` are badlines: BA warning on cycles 12–54, matrix DMA
  on cycles 15–54.
- **Sprites**: per-sprite pointer fetch slots and the two extra φ2 steals for
  three data bytes, with the line wrap for sprites 3–7 (first slots PAL
  `58 60 62 1 3 5 7 9`, NTSC `60 62 64 1 3 5 7 9`); DMA start/stop, MC/MCBASE
  and Y-expansion.
- **BA/AEC**: BA drops three cycles before AEC so the CPU finishes its writes.
- **Bank**: `setMemoryBank(bank)` from CIA2 PA0/PA1; screen, character and
  sprite fetch addresses follow `$D018`.
- Counters: stolen cycles, lines, frames.

---

## 6. CIA (`Cia6526`)

One-φ2 state machine for both CIAs (batched `step()` uses the same single-cycle
law, so results never depend on batch size):

- **Timers A and B**: 16-bit, latch and reload, one-shot or continuous, force
  load, φ2 or CNT counting, Timer B chained to Timer A underflows; PB6/PB7
  underflow outputs (pulse or toggle).
- **ICR** (`$0D`): mask set/clear on write, flags read-to-clear, IRQ level
  (CIA1 → IRQ, CIA2 → NMI).
- **Timer read latch**: reading the low byte latches the high byte.
- **TOD**: tenths, seconds, minutes, hours (BCD), 50/60 Hz divider, latch on
  hours read, stop on hours write, alarm write mode.
- **Serial**: shift register, SP in/out, bits remaining, self-clock.
- **Ports**: PRA/PRB/DDRA/DDRB with external inputs (`portAPins()` drives the
  VIC bank).
- **Intent counters**: every Timer A/B latch and control write is counted, so
  the runtime can tell a tune-programmed `$FFFF` latch from the reset
  sentinel.

---

## 7. SID access from the C64

### 7.1 Timed writes (`C64SidBridgeState`)

Every CPU write to SID space is recorded with its **absolute φ2 cycle**
(`C64SidBridgeTimedWrite`, up to 4096 per render block) and replayed into the
audio SID at that exact cycle, so play-routine updates are sample-exact rather
than quantised to buffer edges. Overflow is counted
(`TimedWriteOverflow` downgrade). `$D418` writes are never change-filtered:
repeated identical low nibbles are real 4-bit samples
(`c64_d418_capture.h`).

`C64FixedWriteScheduler` orders writes by (sample, cycle) with four stable
8-bit bucket passes (O(4N + 1024), no allocation, no comparison sort), keeping
the original order for ties.

### 7.2 Readback (`c64_sid_readback.h`)

`$D419–$D41C` (POTX, POTY, OSC3, ENV3) are read from a digital SID model
**clocked by φ2 timestamps**, independent of host-sample rendering, so a 6510
polling OSC3 between two host samples sees the right value. Writes to the read
registers (`$D419–$D41F`) are counted as SID hole writes.

**Cost.** The model advances cycle by cycle (three oscillators, noise LFSRs,
hard sync, three envelopes, the POT counter) up to each SID access.

- The OSC3 byte of a single waveform, or of voice 3 under TEST or with no
  waveform, is a pure function of the current state and is computed when
  `$D41B` is read. Combined waveforms carry the combined-wave hysteresis from
  cycle to cycle, so they are still updated every cycle.
- The PSID runtime's own sink (`C64RuntimeSidSink`) answers every CPU read.
  The timed bridge (`C64SidBridgeState`) receives the same writes and used to
  advance a second, never-read readback model. The runtime now tells it
  (`SidRegisterSink::setReadsAnsweredElsewhere`), and it skips that model; a
  read that still reaches it is counted in `shadowedReadCount`.
- VIC-II: the two per-cycle sprite loops are skipped when no sprite DMA is
  active (one 8-byte test instead of eight flags per cycle).

Together about 10 % fewer instructions for PSID playback, bit-identical output
(checked with a tune that polls OSC3 and ENV3 every frame).

### 7.3 Projection bridge

Synth, SID REG and drum engines that project register writes onto the C64 bus
use `c64_sid_projection_bridge.h`. `C64Platform::sidRegisterImage()` is the only
register mirror: immediate writes go through `cpuWrite($D400+reg)`, timed
writes are queued and become visible when the φ2 mirror reaches their cycle.
There is no second shadow array.

### 7.4 Multi-SID

Up to five SID chips (PSID v3/v4 second, third and more SID addresses; bases
`$D400`, `$D420`, `$D440` … in the extra-SID ranges). Gains
(`c64SidMixGains`): one chip 1.0; `n` chips give the primary `2/(n+1)` and each
other `1/(n+1)`, so the sum is exactly 1.0 (no clipping). Per-chip models come
from PSID flags bits 4–9 (SID 1–3); chips 4–5 inherit SID 1; values 0/3 follow
the user's global model.

---

## 8. PSID/RSID files (`psid_header.h`)

`psidParse()` validates:

- magic `PSID`/`RSID`, version 1–4, data offset, load/init/play addresses,
  songs and start song, speed bits, flags (clock, SID models, compatibility,
  BASIC flag), relocation `startPage`/`pageLength`, second/third SID addresses
  (and the extensions);
- result codes: `OK`, `BadMagic`, `TooShort`, `BadVersion`, `BadOffset`,
  `BadSongCount`, `BadStartSong`, `BadRsidHeader`, `UnsupportedMultiSid`,
  `BadSidAddress`, `UnsupportedMusSpecific` (Compute! MUS data),
  `UnsupportedRsidBasic`, `DuplicateSidBase`, `BadRelocationRange`.

`PsidParsePolicy::StrictSpec` rejects out-of-range song metadata;
`SidTuneCompatible` clamps it like libsidplayfp. Playback uses Compatible.

---

## 9. Runtime (`C64Runtime`)

### 9.1 Loading

`loadPsid()` / `loadPsidImage_()`:

1. Validate the header, payload and relocation range.
2. Reset every per-tune counter and ledger (no leakage between tunes).
3. **Cold boot**: `C64Platform::coldBootForSidLoad()` initialises the
   processor port, stack, vectors, screen and colour RAM, CIAs, VIC, IEC and
   tape surfaces — a deterministic power-on state, never a direct-PC shortcut.
4. Configure SID bases; load the payload into RAM (truncation counted).
5. Pick a **bootstrap page** that does not overlap the tune (default `$0334`,
   span `$49`: init at `+$00`, play at `+$1C`, CIA service at `+$3C`), or fail
   the load.
6. Mirror RAM and ROMs into the φ2 machine.

A failed load returns a `PsidLoadFailure` code (shown in the editor):
`ParseFailed`, `ConfigureSidBasesFailed`, `BootstrapRelocationFailed`,
`ParseTooShort`, `BadMagic`, `BadVersion`, `BadOffset`, `BadSongMetadata`,
`BadRsidHeader`, `BadSidAddress`, `DuplicateSidBase`,
`UnsupportedMusSpecific`, `UnsupportedRsidBasic`, `BadRelocationRange`,
`EmptyPayload`, `InvalidParsedHeader`.

### 9.2 Init (`runInit(song)`)

- The song is clamped to `[1, songs]`; `A = song − 1`.
- PSID: a bootstrap calls INIT and ends on an idle loop or `$FFFF` sentinel;
  RSID: an RSID bootstrap installs the reset vector and runs the tune the way a
  real C64 would.
- Init **always runs on the φ2 machine** (budget `maxInstructions × 12`
  cycles), with BRK-as-JAM as the completion sentinel. There is no legacy
  instruction-atomic fallback (the physical-only build policy locks it out).
- After init: RSID starts the realtime SID core; CIA-timed PSIDs get the CIA
  playback bootstrap (the tune's Timer A latch, or the compatibility latch).

### 9.3 Play (`runPlay(budget, timedSink)`)

- **RSID**: the machine runs one physical VIC frame of cycles; the tune drives
  itself from CIA/VIC interrupts. Refused (and counted) if the φ2 machine is not
  ready.
- **PSID CIA**: CIA1 Timer A IRQ, `$FF48/$0314` vectoring, `$DC0D` ACK and
  the play routine all run on the φ2 machine.
- **PSID VBI**: the play bootstrap calls PLAY once per VIC frame.
- Every SID write goes to `timedSink` with its φ2 stamp.
- Play runs inside a **render transaction** (`beginRenderTransaction`,
  `commitRenderTransaction`, `rollbackRenderTransaction`): if the service jams
  or runs out of budget, CPU, CIA/VIC, RAM, colour RAM, the SID bridge and the
  diagnostics roll back together, so a failed frame never leaves half-written
  state.

The kernel calls `runPlay()` from the render thread with an adaptive budget and
the kernel's SID bridge, and schedules it at the play period computed in §1.

### 9.4 Playback modes and exactness

`RsidPlaybackMode`:

- **Strict**: BRK vectors normally, CLI is needed before play can be
  interrupted, approximate opcodes jam.
- **Compatible**: BRK acts as a JAM (init sentinel), approximate opcodes run
  best-effort.

The runtime keeps an honest **downgrade ledger** (`RsidExactnessDowngrade`
bits): `NotRsid`, `Phi2NotActive`, `UnsupportedOpcode`, `ApproximateOpcode`,
`VicBusStealApprox`, `TimedWriteOverflow`, `DroppedMultiSidWrites`,
`MissingRealRoms`, `BasicStartupUnsupported`, `CiaModelApprox`,
`SidReadApprox`, `RmwSidWrite`, `LegacyCpuPlayback`, `SidHoleWrite`,
`OpenBusApprox`, `StrictRsidNotPhi2`, `RomIdentityUnverified`,
`InitBrkSentinel`, `InvalidSidChipAccess` (plus retired ABI bits).

`C64PhysicalExactnessBlocker` bits list what still prevents a
"physically exact" claim (missing or unverified ROMs, CIA/VIC not declared
cycle-exact, SID readback and open-bus approximations, BASIC startup, PSID CIA
compatibility, colour-RAM open bus, POT X/Y). `rsidPhysicallyExact()` is true
only on the strict φ2 path with an empty ledger and no blockers. The C64 tab
shows these, and the telemetry exports them.

---

## 10. Telemetry and tracing

- `c64_telemetry.h` — an RT-safe, allocation-free snapshot of the machine:
  CPU registers, three disassembled lines at PC, VIC raster/cycle/badline/bank,
  CIA timers/ICR/TOD, SID bus lanes (register, value, write strobe, φ2, IRQ/DMA
  for the last 128 samples), eight memory windows with hashes and change masks,
  debug events, the PSID header fields and the exactness state. Heavy parts are
  copied only while an editor view needs them.
- `c64_boot_trace.h` — opt-in (compiled out by default) boot/init/play
  tracer, one line per boot-state transition.
- `IPhi2TraceSink` — per-cycle `Phi2BusPhase` records (owner, BA/AEC/RDY,
  IRQ/NMI/RESET before sample, dummy/stack/vector accesses, interrupt entry,
  RMW kind, SID read/write) for tests and offline analysis.
- `c64_sid_trace_import.h` — offline SID trace events stamped in absolute φ2
  cycles.

See [RUNTIME.md](RUNTIME.md) for how the kernel schedules the C64 player
inside a block, and [SID_CHIP.md](SID_CHIP.md) for the SID it drives.
