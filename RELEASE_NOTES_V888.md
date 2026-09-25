# ArpSID v888 — Mixed unresolved event clock-law closure

## Closure

v888 closes the remaining canonical dispatcher split-clock edge after v887:

- Fractional-capable backends already used one physical per-sample SID-cycle law for unresolved-only blocks.
- Mixed queues were still vulnerable because `SidTimedEvent::before()` sorts resolved events before unresolved events.
- That meant an unresolved MIDI/UI "now" event could be deferred until after the last resolved sample/cycle event in the same block.

v888 preconsumes the unresolved tail at sample 0 for fractional-capable physical-clock dispatch, then dispatches only the resolved prefix through the per-sample SID-cycle law.

## New test

- `SidRuntimeMixedUnresolvedClockLawV888Tests`

The test verifies that a mixed queue applies the unresolved event exactly once before the first physical sub-sample span, while the resolved event is still interleaved at its resolved sample/cycle position.
