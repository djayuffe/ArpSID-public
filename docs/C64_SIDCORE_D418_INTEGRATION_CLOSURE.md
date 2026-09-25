# C64/SIDCORE `$D418` integration closure

This release integrates the high-value C64/SIDCORE deltas into the render-side source tree.

## Primary closure: `$D418` cycle-correct DIGI capture

The C64 physical SID write bus now treats `$D418` as a bus event, not a changed-register optimization. Every write to SID register `$18` is preserved, even when the value is identical to the previous value. This is required because classic volume-register DIGI uses the sequence and timing of writes as the waveform.

Captured events carry absolute PHI2 timestamps and can be reconstructed by zero-order hold through `include/arpsid/core/c64_d418_capture.h`.

## Supporting C64 correctness closures

- Open-bus persistence now uses the C64-style `0x1D00` PHI2 window rather than dropping fully at 4096 cycles.
- Color RAM continues to return `(openBus & 0xF0) | colorNibble`.
- VIC `$D019` readback now returns fixed high bits 4-6 and an IRQ master bit synthesized from enabled latched IRQs. `$D019` write remains write-one-to-ack.
- The deterministic KERNAL fallback now exposes CINV/NMINV IRQ/NMI indirection, CIA ACK helpers, and RESTOR instead of RTI-only stubs.

## Scope note

This is source-level integration and regression coverage. Full chip-exact 6569 sprite DMA and all undocumented 6526 edge cases remain outside this closure unless future tests explicitly require them.
