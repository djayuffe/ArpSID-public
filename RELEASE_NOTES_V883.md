# ArpSID v883 — final queue compaction closure

v883 closes the remaining long-running SidBusQueue maintenance edge case after the v877-v882 projection mirror authority work.

## Fix

`SidBusQueue::compactConsumed_()` now sorts and rebases surviving pending events after removing the consumed prefix, not only when projection-mirror events are selectively cleared.

This keeps the queue closed by construction for ordinary CPU/CIA/PSID events as well as projection-mirror events:

- consumed events are removed before capacity-pressure appends;
- surviving pending events retain deterministic same-PHI2 ordering;
- pending event order is rebased to `0..N-1`;
- future same-PHI2 events sort after already-pending survivors;
- the 32-bit order tie-breaker cannot grow unbounded through repeated compaction.

## Test

Added `C64ProjectionMirrorQueueCompactionV883Tests`, which verifies capacity-pressure compaction after partial consumption, dense order rebasing, cursor reset, and deterministic same-cycle ordering of new events after survivors.
