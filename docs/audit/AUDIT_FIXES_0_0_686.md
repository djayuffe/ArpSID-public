# ArpSID 0.0.686 (pass326) — Remaining Audit Closure

This pass closes the remaining reported PureSID, AudioQueue, C64 telemetry,
timing-model, source-hygiene, release-validation, and rotary-control findings.

## Realtime capture

- PureSID and `$D418` callbacks acquire their in-flight lease before any
  capture-owned active/capacity/vector/held/observed state is touched.
- Vector capacity is published through an atomic mirror and revoked before
  stop/copy or reallocation. Status and callback paths never inspect vector
  metadata.
- PureSID capture sample rate is atomic.
- AudioQueue ingest uses a plain `noexcept` function pointer plus context, and
  callback sample rate is atomically published before `AudioQueueStart`.
- Every record-vector prepare path catches allocation failure and fails closed.

## C64 correctness

- Combined PHI2 bridge and sink telemetry uses saturating addition instead of
  `max`, preserving simultaneous events.
- Strict RSID init executes real BRK vectoring and rejects compatibility BRK
  sentinels. Compatible RSID and PSID retain the explicit sentinel contract.
- VBI PSID passive time accrues progressively to each play deadline and cannot
  borrow cycles from later host samples.
- Continuous RSID/play-address-zero writes use a dedicated block timeline rather
  than the discrete-call `PlayBase` model.

## Audit and release gates

- All nine production `std::sort` calls are classified as bounded realtime or
  setup-only at the call site.
- `RemainingAuditClosureV747Tests` pins lease ordering, capacity publication,
  callback ABI, allocation handling, timing contracts, sort classification,
  PlugInKit receipt gating, and compact-knob UX.
- Complete-release packaging now requires a successful Logic AUv3 PlugInKit and
  targeted-AU validation receipt by default. An unregistered archive requires an
  explicit development-only override and is labeled accordingly.

## UX

- Shared rotary visuals are 70% of the original radius while preserving the full
  pointer hit area.
- Horizontal drag, closed-hand drag feedback, Page Up/Down large steps, fine
  Shift gestures, keyboard focus, accessibility, and reset gestures are exposed.
