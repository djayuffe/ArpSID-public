# ArpSID v889 — Sample-0 Event Order Closure

## Summary

v889 closes the remaining mixed resolved/unresolved event-order edge in the fractional SID host-cycle dispatcher.

v888 correctly moved unresolved host-"now" events into the physical per-sample clock law, but it pre-applied the unresolved tail before all resolved sample-0 events. That could violate canonical priority ordering at the same host boundary, for example an unresolved `MidiNoteOn` could run before a resolved sample-0 `Panic`.

## Fix

For fractional-capable backends, unresolved events are now materialised as resolved sample-0/cycle-0/subphase-0 events and the queue is stably re-sorted with `SidTimedEvent::before()` before physical dispatch.

This preserves both rules:

- unresolved events are host-now/sample-0 events;
- sample-0 priority/order remains canonical and deterministic.

## Tests

Added runtime test:

- `SidRuntimeUnresolvedSample0OrderV889Tests`

It verifies that a resolved sample-0 `Panic` remains before a materialised unresolved `MidiNoteOn`, while a later resolved event still lands at its resolved physical cycle boundary.
