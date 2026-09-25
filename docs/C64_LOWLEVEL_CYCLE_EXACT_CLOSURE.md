# C64 low-level cycle closure

This audit replaces the three broad approximation paths in the C64 runtime.

- CIA 6526 timer A/B, CNT, chained-underflow gating, IRQ/ICR, serial and PB6/PB7
  output events now share one per-PHI2 transition function. Batched stepping and
  scalar ticking produce identical edge/state ledgers.
- VIC-II BA is now an early-warning signal and AEC is the actual CPU bus grant.
  PAL and NTSC use separate per-sprite DMA slots; badlines warn on cycles 12-54
  and own PHI2 on cycles 15-54.
- SID POTX/POTY/OSC3/ENV3 readback is clocked from absolute PHI2. OSC3 comes from
  the selected waveform output, ENV3 from the live envelope counter, and POT
  results latch on the 512-cycle conversion boundary.
- The PHI2 machine no longer treats BA-low warning cycles as VIC-owned cycles.

Regression coverage is in
`source/tests/c64_cycle_exact_closure_v741_tests.cpp`.

Remaining physical-risk flags are intentionally independent: missing or
unverified ROMs, the board-level open-bus capacitance model, reachable legacy
instruction-atomic playback, and chip-sample-dependent unstable 6510 opcodes.
