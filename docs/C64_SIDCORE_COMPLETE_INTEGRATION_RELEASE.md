# PASS288 — C64 / SIDCORE complete integration closure

Version: `0.0.648-pass288`

This pass completes the concrete C64/SIDCORE integration items that remained
after the D418/full-SIDCORE passes:

- 6510 processor-port `$0001` readback is now the exact C64 bit surface:
  `0xC0 | (PORT & DDR) | (0x3F & ~DDR)`.
  High bits 6/7 stay board-high and low floating bits pull high; the PLA still
  decodes LORAM/HIRAM/CHAREN from the resulting low bits.
- The standalone `ProcessorPort6510` helper and `C64Platform::effectiveProcessorPort()`
  now agree, preventing split-brain between the PHI2 memory matrix and the
  full C64 platform.
- CIA2 PA0/PA1 now drive the VIC bank through normal CPU writes to `$DD00/$DD02`.
  Changing either PRA or DDRA immediately re-evaluates the VIC bank; no test-only
  helper call is required.
- The physical C64 SID bus contract is locked through the platform path:
  `$D418` and its `$D400-$D7FF` mirrors preserve repeated identical volume writes,
  retain absolute PHI2 timestamps, and reconstruct through the Pure SID ZOH helper.
- Removed a duplicate `psidCiaVectorEntered` assignment in the CIA-driven playback
  telemetry handoff.
- Added `source/tests/c64_sidcore_complete_integration_v709_tests.cpp` to pin the
  integrated contract across processor-port readback, CIA2->VIC bank switching,
  SID mirror `$D418` capture, repeated-value capture and ZOH reconstruction.
- Updated `c64_phi2_processor_port_tests.cpp` to the C64 physical `$0001` contract
  instead of the stale open-bus-input expectation.

Validation performed in the sandbox:

```sh
cmake -S . -B /tmp/arpsid_cmake288b -DARPSID_BUILD_TESTS=ON
cmake --build /tmp/arpsid_cmake288b --target \
  arpsid_c64_sidcore_complete_integration_v709_tests \
  arpsid_c64_full_sidcore_integration_v708_tests \
  arpsid_c64_d418_digi_capture_v706_tests \
  arpsid_c64_open_bus_vic_hle_v707_tests \
  arpsid_c64_cia_proper_v618_tests \
  arpsid_c64_cia_extended_v619_tests \
  arpsid_c64_cia_phi2_integration_v620_tests \
  arpsid_c64_phi2_processor_port_tests \
  arpsid_release_root_clean_v705_tests \
  arpsid_source_comment_code_merge_guard_v704_tests -j2
ctest -R 'C64SidcoreCompleteIntegrationV709Tests|C64FullSidcoreIntegrationV708Tests|C64D418DigiCaptureV706Tests|C64OpenBusVicHleV707Tests|C64CiaProperV618Tests|C64CiaExtendedV619Tests|C64CiaPhi2IntegrationV620Tests|C64Phi2ProcessorPortTests|ReleaseRootCleanV705Tests|SourceCommentCodeMergeGuardV704Tests' --output-on-failure
```

Result: `10/10` CTest pass.
