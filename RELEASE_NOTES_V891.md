# ArpSID v891 — dispatcher/RSID/load-contract closure

This pass closes the explicit remaining items requested after v890:

- sample-only dispatcher events no longer render subphase `0..255` before the event.
- same-sample policy is explicit: sample-boundary destructive controls (`Panic`, `AllSoundOff`, `AllNotesOff`) preempt cycle-stamped writes in the same sample; ordinary sample-only events remain weaker than physically cycle-stamped writes.
- RSID speed is normalized: `rawSpeed` preserves the file word, while `speed`/`normalizedSpeed` become `0xFFFFFFFF` internally so every RSID subtune is CIA-timed.
- `psidUsesCiaTimingForSong()` returns true for RSID.
- malformed `RSID_BASIC` with non-zero init address is rejected; valid `RSID_BASIC` returns `UnsupportedRsidBasic` until a real BASIC startup path exists.
- `PsidLoadFailure` has a richer taxonomy and AU telemetry publishes last parse/load failure codes.
- `loadPsidParsed()` validates parsed header/data/size/relocation invariants before constructing the runtime image.
- startPage/pageLength relocation validation rejects zero/partial/low-page/wrap/overlap ranges.
- PSID play and PSID-CIA playback bootstraps set play IOMAP from the play routine address.
- timed-write overflow during a current play transaction is fatal to that transaction: rollback, mark exactness, retry from restored state next block instead of rendering a truncated write list.
- `toCoreHeader_()` was removed; runtime timing now uses the already-lossless runtime header directly.

New tests:

- `SidRuntimeSampleOnlySubphaseV891Tests`
- `SidRuntimeSameSamplePolicyV891Tests`
- `PsidRsidNormalizationV891Tests`
- `PsidRelocationLoadFailureV891Tests`
