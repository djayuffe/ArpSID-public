# ArpSID internals

These are low-level references for people who read, change or port the engine code. Each
one follows the code: it names the source files, gives the constants and formulas, and
explains why the code is the way it is.

Read them in this order. Each builds on the one before.

| # | Document | Covers |
|---|---|---|
| 1 | [SID_CHIP.md](SID_CHIP.md) | The SID chip core (`SIDChip`, `SIDVoice`): 24-bit oscillator, 23-bit noise LFSR, pulse comparator, combined-wave charge model, DAC laws per revision, envelope rate table and counters, filter anchors and Q laws, output stage, 256-subphase cycle stepping, oversampling, analogue state, and the register-domain portamento law. |
| 2 | [C64_MACHINE.md](C64_MACHINE.md) | The C64 used by the tune player: φ2 cycle order, 6510 microsequencer, VIC-II timing (badlines, BA/AEC, IRQ), CIA 6526, PLA and banking, open bus, the SID bridge, PSID/RSID parsing, loading, bootstrap and init/play, exactness downgrades, and rollback-safe render transactions. |
| 3 | [RUNTIME.md](RUNTIME.md) | How a host block becomes audio: the `ArpSIDDSPKernel` thread model and API, the 21-step `processBlock` pipeline, the event model and its ordering law, capacities and overflow reserves, the host-cycle dispatcher and fractional render, register scheduling, voice tokens and stealing, state roots and codecs, ownership mailboxes, telemetry, and the realtime rules. |
| 4 | [ENGINES.md](ENGINES.md) | Everything between the chip and the kernel: BitPerfect (topologies, voice modes, pitch, portamento, quantization, block render), the single-SID engine, the SID register engine and its limiter, the voice manager, the arpeggiator and sequencer algorithms, LFOs and mod matrix, DrSID (microprograms, compiler, runner), SID-808, the drum router and stem mixer, the `$D418` DIGI stream, MIX FX, Hi-Fi Transcendence, the PostFX timeline, forensic resolution and parameter presentation. |
| 5 | [SYNTH_MODES.md](SYNTH_MODES.md) | Render modes and flavors: mode resolution and exclusivity, flavor policy, GM drum auto-promotion, per-mode event routing and transitions, CLASSIC details, the complete SYNTH / SID REG mode (voice policy, note-on register programs, hard restart, glide, token-first note-off, bend/pressure/pedals, filter registers, reseed, direct register editing, cycle math, render), DR SID routing and KIT scheduler, and the projection engines (backend projection, register shadow, C64 SID projection mirror and bootstrap, drum bridge, GUI realtime projection), virtual gate, pure 1Q1 output. |

Related documents one level up:

| Document | Covers |
|---|---|
| [../ARCHITECTURE.md](../ARCHITECTURE.md) | The system at a glance: layers, pipeline, engines, parameters, state, GUI models, wrappers. |
| [../TECHNICAL_SPECIFICATIONS.md](../TECHNICAL_SPECIFICATIONS.md) | Every number and limit in one place. |
| [../FEATURES.md](../FEATURES.md) | What ArpSID does, feature by feature. |
| [../SYNTH_GUIDE.md](../SYNTH_GUIDE.md) | The synth modes from the player's side: controls per mode, recipes, troubleshooting. |
| [../PARAMETER_REFERENCE.md](../PARAMETER_REFERENCE.md) | All 512 parameters (generated and checked by a test). |
| [../VST3_IMPLEMENTATION.md](../VST3_IMPLEMENTATION.md) | The VST3 wrapper: processor, kernel host, state v5, controller, units, messages. |
| [../REALTIME_OWNERSHIP.md](../REALTIME_OWNERSHIP.md), [../REALTIME_ROLLBACK_JOURNAL.md](../REALTIME_ROLLBACK_JOURNAL.md) | Thread ownership and rollback journal details. |
| [../C64_EXACTNESS_BOUNDARIES.md](../C64_EXACTNESS_BOUNDARIES.md), [../SID_FILE_FORMAT_NOTES.md](../SID_FILE_FORMAT_NOTES.md), [../D418_NIBBLE_SPEC.md](../D418_NIBBLE_SPEC.md) | C64 player claims, `.sid` handling, DIGI nibble format. |

## Conventions used in these documents

- **norm** or *n* means a normalized parameter value in 0..1, as hosts see it.
- `$XXXX` is a C64 address in hex. SID registers are given as offsets (`$04`) or as full
  addresses (`$D404`).
- A **cycle** is one φ2 clock of the emulated C64 or SID (PAL 985 248 Hz, NTSC
  1 022 727 Hz). A **subphase** is 1/256 of a cycle.
- "Render thread" is the host's audio callback. Anything described as realtime runs there
  and must not allocate, lock or do I/O.
- File paths are relative to the repository root.
