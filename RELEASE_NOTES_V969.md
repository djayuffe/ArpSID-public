# ArpSID v969 — test-suite integrity closure

This release is a test-infrastructure audit-and-repair pass on top of the v968 DrSID kit-save closure. No production/runtime behavior changed.

Package: `0.0.690-pass380-v969-test-suite-integrity-closure`

## Audit result

A full audit of the test suite (491 test sources) checked for orphaned (unbuilt) tests, obsolete/superseded tests, and flaky tests:

- **No genuinely legacy/obsolete tests were found to remove.** The apparent "orphans" turned out to test current, live behavior. Most were already registered via string-constructing CMake `foreach` loops (e.g. `dr808_${tgt}_tests`, `${_test}_tests.cpp`) that a naive filename grep does not see. Removing them would have destroyed real regression coverage — the repository already documents an earlier "over-eager stale-target cleanup" that orphaned valid tests.
- Exactly **one** source was genuinely orphaned (present on disk, never built): `auv3_render_scratch_transport_v591_tests.cpp`.

## Fixes

- **Restored dropped coverage:** `auv3_render_scratch_transport_v591` (the AUv3 render-scratch/transport source-contract guard) is now registered in the build. Its one drifted assertion — the stable-scratch helper signature, which v965+ changed from `static inline` to `static constexpr` — was made linkage-tolerant (matches on return type + signature); the guarded helper and its behavior are intact.
- **De-flaked `ScopeTripleBufferMultiConsumerV687Tests`:** the multi-consumer telemetry test could fail its progress/starvation assertion when a consumer thread was starved during the fixed producer burst under a loaded parallel CTest — a scheduling artifact, not a `ScopeTripleBuffer` defect. Both consumers now start before the producer, and the producer ends the run only after both consumers have made progress (bounded ~5 s, so a genuinely stuck consumer still fails). The coherency / no-torn / no-id-regression invariants are unchanged.

## Regression coverage

- `TestSuiteIntegrityV969Tests` pins both repairs: the AUv3 test stays registered with its linkage-tolerant assertion, the dr808 voice-test foreach registration remains, and the V687 producer waits for both consumers while keeping its coherency/monotonic invariants.

## Validation

- Full CTest suite green (now 384 registered native tests + script guards). Version-coherence, source-tree and audit-closure guards pass. No production source changed; macOS AU/Logic, signing and real-SDK VST3 validation remain external sign-off items (unchanged).
