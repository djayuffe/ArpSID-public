# Realtime rollback journal — implemented V813

## Goal

PSID/RSID bridge transactions must be rollback-safe without copying an entire `C64Platform` in the realtime audio callback.

Earlier pass380 hardening removed the full platform copy from the stack, but kept a persistent full-platform scratch in `ArpSIDDSPKernel`. V813 removes that scratch and moves rollback ownership into `C64Platform` itself.

## Current implementation

`C64Platform` now exposes:

```cpp
bool beginRenderMutationJournal() noexcept;
void commitRenderMutationJournal() noexcept;
bool rollbackRenderMutationJournal() noexcept;
bool renderMutationJournalActive() const noexcept;
bool renderMutationJournalOverflowed() const noexcept;
```

The journal is a bounded mutation journal: it is fixed-capacity and render-owned. It performs no heap allocation and uses no locks.

## What is snapshotted directly

The journal snapshots deterministic machine substate that is small enough to copy directly:

- PHI2 cycle
- open-bus latch
- CPU state/model object
- CIA1/CIA2
- VIC-II
- SID bus queue
- SID register images and configured SID bases
- last SID write telemetry
- boot/play/bootstrap ledgers
- interrupt validation counters
- PSID-CIA run/service generations
- active SID sink pointer

## What is journaled as dirty cells

RAM-like storage is not copied wholesale. It is restored through bounded dirty-cell logs:

```cpp
RenderMutationJournal::kMaxDirtyRam   = 8192
RenderMutationJournal::kMaxDirtyColor = 1024
```

The first write to an address records the original byte. Duplicate writes to the same address do not grow the journal and still roll back to the pre-transaction value.

Tracked write surfaces:

- `pokeMemory()`
- `loadBytesToRam()` while active
- CPU writes to `$0000/$0001`
- ordinary mapped RAM writes
- Color RAM writes

## DSP kernel transaction flow

`ArpSIDDSPKernel::beginBridgeTransaction_()` stores only the bridge-visible SID state on the stack and starts the platform journal.

Successful play/service transactions call:

```cpp
commitBridgeTransaction_(tx, player);
```

Failed/incomplete play/service transactions call:

```cpp
rollbackBridgeTransaction_(tx, player);
```

Rollback restores:

1. SID bridge timed-write count and register image.
2. C64 platform mutation journal state.

## Guards

`RenderBridgeSnapshotStackGuardV806Tests` enforces that:

- `BridgeTransactionSnapshot` contains no `C64Platform`.
- `ArpSIDDSPKernel` contains no `c64BridgeRollbackPlatformScratch_`.
- bridge transactions call begin/rollback/commit mutation-journal methods.

`C64RenderMutationJournalV813Tests` verifies:

- begin/active/nested refusal behavior.
- rollback restores RAM, CPU state and PHI2 cycle.
- duplicate dirty RAM writes restore the original value.
- commit preserves mutations.

## Remaining future hardening

The journal currently snapshots CIA/VIC/CPU/SID scalar objects directly and journals RAM/ColorRAM cells. A later pass can make the journal even smaller by adding device-specific dirty journals for CIA/VIC internals, but the full-platform copy has been removed from both stack and DSP-kernel persistent storage.
