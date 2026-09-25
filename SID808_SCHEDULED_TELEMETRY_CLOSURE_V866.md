# ArpSID 0.0.690 pass380 v866 - SID-808 Scheduled Telemetry Closure

Date: 2026-07-04

## Purpose

v866 is a focused follow-up to the v865 SID-808 audible-authority closure. v865
proved that each SID-808 drum can render audibly through the bridge and that
the AU UI can display bridge-owned authority. The remaining audit finding was
more specific: delayed sample-accurate events rendered audio but did not publish
the routed-hit telemetry used by that authority display.

## Root Cause

Immediate bridge hits used this path:

```text
DrumEngineHostBridge::noteOn()
  -> DrumEngineRouter::noteOn()
  -> publishSid808RoutedHitIfActive_()
```

Delayed scheduled hits used this path inside `processBlock()`:

```text
DrumEngineHostBridge::processBlock()
  -> DrumEngineRouter::noteOn()
```

The direct router call was enough to make the SID-808 engine render, but it
skipped the bridge publisher. The result was a partial authority chain:

```text
scheduled hit -> audible bridge output -> outputPeak updates
scheduled hit -> routedHitCount unchanged
scheduled hit -> last routed drum/note stale
```

That contradicted the intended v865 contract for sequencer-driven SID-808 hits.

## Fix

`DrumEngineHostBridge::processBlock()` now dispatches delayed scheduled events
through the same bridge note surface as immediate pad/MIDI hits:

```text
scheduled note without override -> noteOn(...)
scheduled note with override    -> noteOnWithOverride(...)
```

Those methods still route through `DrumEngineRouter`, but they also publish the
SID-808 routed-hit telemetry after the router accepts the hit.

## Threading Contract Clarified

The scheduled queue remains a fixed-capacity, insertion-sorted render-thread
queue. It is deterministic and allocation-free, but it is not a cross-thread
producer queue for arbitrary GUI/host calls.

Correct use:

```text
render-thread sequencer computes event offsets
render-thread calls noteOnAt(...)
same render-thread calls processBlock(...)
```

Out of scope for v866:

```text
GUI/control thread writes noteOnAt(...) concurrently with audio thread
```

That would need a dedicated lock-free producer/consumer mailbox or another
explicit non-RT handoff.

## Behavioral Guard

`Sid808AudibleAuthorityV865Tests` now tests the actual delayed path instead of
only checking source wiring:

- `noteOnAt(64, Kick, ...)` produces audible output and increments
  `routedHitCount`;
- `noteOnAtWithOverride(96, Clap, ...)` produces audible output and publishes
  last routed drum/note;
- three delayed notes in one block increment telemetry three times and leave
  the final event in the last-routed fields;
- a note scheduled exactly at block end publishes the routed-hit fields before
  the next audio block and then renders audibly in that next block;
- scheduling past the fixed queue capacity increments
  `scheduledNoteOverflowCount()`.

## Expected User-Visible Result

For SID-808 transport/sequencer playback, the AU authority display should no
longer show a stale hit count while the drums are actually sounding. The display
should now move consistently for pads, MIDI input, transport notes, and
sample-accurate scheduled events:

```text
AUTH SID808-BRIDGE KIT120 HIT<n> PK <peak> REPL/FAILOPEN/IDLE
```

## Validation

- `Sid808AudibleAuthorityV865Tests`: PASS with the new scheduled-hit cases.
- Focused SID-808/transport/no-silence CTest set: 8/8 PASS.
- Direct AUv2 component smoke: PASS.
- Direct AUv2 SID-808 component smoke: PASS.
- Direct AU3 SID-808 render-event smoke: PASS.
- AUv2 install/cache refresh/strict validation: PASS.
- Installed AUv2 binary SHA256:
  `f55909a0dfe07b32816a97e2ea2d7fe2c9b5fc4d7be7b87aa9f1924a9d142744`.

## Relationship To Earlier Fixes

- v862 fixed the input owner: canonical DrSID-mode drum MIDI reaches the kernel
  target hook and then the SID-808 bridge.
- v861 fixed the output owner: silent SID-808 bridge scratch cannot erase the
  final output bus.
- v865 fixed the authority proof: bridge hit, configured kit, output peak, and
  UI display are all first-class.
- v866 fixes the delayed scheduled-event proof: sample-accurate bridge hits now
  publish the same routed-hit telemetry as immediate hits.
