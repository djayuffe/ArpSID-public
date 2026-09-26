# ArpSID GUI tab architecture

This document is the current specification for the 9-tab GUI architecture.
The canonical inventory lives in `include/arpsid/gui/tab_architecture.h`,
where compile-time `static_assert` pins keep the tab order, labels, drum
contexts, and implementation status locked to the release contract.

All 9 tabs are implemented. There are no remaining
GUI scaffold tabs.

## Inventory

| # | Tab | HUD | Status | DrumContext | Notes |
|---|---|---|---|---|---|
| 0 | DRSID | DRSID | Implemented | DrSID_C64Wavetable | Register-microprogram drums on one SID |
| 1 | SID-808 | S808 | Implemented | SID808_AnalogProjection | TR-style analog x0x projection |
| 2 | DIGI | DIGI | Implemented | Digi4Bit | $D418 volume-DAC sample surface |
| 3 | SEQ | SEQ | Implemented | None | Global step sequencer |
| 4 | KIT | KIT | Implemented | None | Kit editor for all drum engines |
| 5 | MIX | MIX | Implemented | None | Per-instrument mixer and FX |
| 6 | SIDCORE | SCORE | Implemented | None | Live SID register timeline |
| 7 | C64 STATE | C64 | Implemented | None | CPU/SID/CIA/PSID state inspector |
| 8 | SETTINGS | SET | Implemented | None | Global preferences and diagnostics |

## Implemented Surfaces

### DRSID

DrSID remains the C64-wavetable drum surface. The tab is backed by the
DrSID instrument-program and kit-compiler contracts, with deterministic
register microprogram playback and factory-bank coverage.

### SID-808

SID-808 remains the analog x0x projection surface. It owns the SID808
engine/router path, transport integration, factory definitions, voice smoke,
accent, hat choke, determinism, and peak-headroom coverage.

### DIGI

DIGI is implemented as the $D418 sample-facing tab. Its GUI model and tab
wire are covered by the v563-v565 tests, including model defaults, tab
projection, and state persistence.

### SEQ

SEQ is implemented as the global sequencer surface with note range, swing,
tempo, rate-law, and step-grid coverage across the v567-v572 tests.

### KIT

KIT is implemented as a full drum-kit editing surface. The model, tab wire,
32-step grid, assign config, voice config, and state blob are covered by the
v555-v560 tests.

### MIX

MIX is implemented as the per-instrument mixer and FX surface. The panel
model, tab wire, FX processors, and state persistence are covered by the
v547-v548, v554, and v561 tests.

### SIDCORE

SIDCORE is implemented as the live SID register/timeline surface. It is fed
from the RT-safe SIDCORE model, ingress ring, scope triple-buffer, and C64
PSID SIDCORE timeline coverage.

### C64 STATE

C64 STATE is implemented as the read-only C64 inspector. It includes
platform/clock/CPU registers, SID model/topology readout, PSID address map,
CIA/IEC/tape/ROM state, SID register mirror, memory-window summary, C64 bus
oscilloscope views, SIDCORE timeline, and audit counters.

### SETTINGS

SETTINGS is implemented as the global preferences and diagnostics surface,
with model, persistence, theme, language, and tab-wire coverage.

## Audit-Correctness Invariants

These invariants apply across all tabs and are pinned through the tab
architecture header and tests:

1. Tab inventory size is exactly 9.
2. Enum order matches array order for state persistence.
3. HUD labels are at most 6 characters.
4. Implementation status is compile-time queryable.
5. `implementedTabCount() == 9`.
6. `scaffoldTabCount() == 0`.
7. Primary `DrumContext` per tab matches the engine-split architecture.
8. GUI never sources truth for engine identity; the engine-layer
   `DrumKitIdentity` always wins and the GUI reflects it.

## Validation

The v591 final source package was built from the applied source tree after
the GUI/tab/button/realtime/scope/telemetry cleanup and the DrSID/SID-808
live drum-data audit. The v590 package added the final top-bar mount and
dedicated DRSID telemetry/overlay wiring pass; v591 adds AUv3 render-scratch
and transport hardening. Current validation baseline:

- Fresh Release CMake configure: passed.
- Fresh Release full build: passed.
- Full ctest: 125/125 passed.
- MIX/KIT/DIGI GUI POD state projects through `gui_realtime_projection_v588.h`
  into compact render-friendly control and telemetry intent.
- DrSID live kick overlay base/sweep telemetry, canonical Tom note 47, complete
  8-class DrSID kit programs, and all SID-808 factory slots 120..149 are pinned
  by `drsid_808_kit_data_v589_tests.cpp`.
- Top-bar controls, dedicated DRSID panel live clock/chip/HUD/LED telemetry,
  no-adapter clearing, and DRSID knob-overlay inclusion are pinned by
  `gui_viewcontroller_wiring_v590_tests.cpp`.
- AUv3 hard-ceiling render scratch, interleaved scratch epoch checks, chunk
  beat math, and 8-attempt transport seqlock reads are pinned by
  `auv3_render_scratch_transport_v591_tests.cpp`.
- AUv2 installed component smoke and strict verifier/auval were already
  green from the v582 binary pass.
