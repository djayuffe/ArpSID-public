# Final release signoff checklist

A build is not final until every item below has evidence in `release-proof/`:

1. `scripts/macos/release_gate_macos.sh` completed successfully.
2. `ctest` passed on the macOS release machine.
3. `auval -strict -v aumu ArpS ASID` contains `AU VALIDATION SUCCEEDED`.
4. Codesign verification passed for the installed component.
5. Apple notarization status is `Accepted` using `scripts/macos/notarize_release.sh`.
6. Logic Pro manual lifecycle gate has screenshots/logs/notes:
   - scan,
   - UI open,
   - Stop→Play preset retention,
   - save/reopen project state retention,
   - offline bounce repeatability,
   - multi-instance stress,
   - automation storm.
7. No release note may claim RSID machine-accurate playback or multi-SID playback until the runtime routes multi-chip SID buses and schedules real C64 IRQ/CIA/VIC execution.
