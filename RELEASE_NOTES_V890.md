# ArpSID v890 — sample-0 sample-only order closure

## Fix

v889 materialised unresolved host-now events as physically cycle-stamped sample-0/cycle-0 events. That preserved the physical render law, but it could still make a materialised unresolved event outrank an already-resolved sample-0 event that intentionally had no cycle metadata.

v890 materialises unresolved host-now events as **sample-0 sample-only** events instead:

- `sample_offset = 0`
- `cycle_offset = kSidUnresolvedCycleOffset`
- `subphase = 0xFF`

The dispatcher still applies sample-only events at the sample boundary before the first physical SID-cycle span, but canonical sample-0 priority/order is preserved against other sample-only events.

## Test

Added `SidRuntimeUnresolvedSample0SampleOnlyV890Tests`, covering:

- resolved sample-only Panic remains before materialised unresolved NoteOn
- materialised unresolved event stays sample-only, not cycle-stamped
- sample-0 events apply before the first physical span
- later resolved cycle events remain physically interleaved
