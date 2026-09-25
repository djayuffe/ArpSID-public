# AI2AI current overlay — v970 test build-graph closure

Current package: `0.0.690-pass380-v970-test-build-graph-closure`.

v970 is a build-infrastructure change (handoff 22.7 partial; no production/
runtime change). forensic_patch_bank.cpp — recompiled from source by 14 test
executables — is now compiled once into a static library
(arpsid_forensic_patchbank) that those tests link. Cuts duplicate compilation
and peak build memory; ODR-correct, verified by a clean full build with no
duplicate-symbol errors. Production wrappers (VST3, AUv2 GUI smoke) still
compile their own copy (separate binaries, unchanged). The AU-kernel header
(~106 test includes) was deliberately NOT extracted — production surgery, poor
risk/reward on current hardware; remains open under 22.7. Guard:
BuildGraphForensicPatchbankLibV970Tests. External macOS/SDK validation pending.

# AI2AI previous overlay — v969 test-suite integrity closure (historical)

Package was `0.0.690-pass380-v969-test-suite-integrity-closure`.

v969 is a test-infrastructure audit-and-repair pass (no production/runtime
change). Audit of all 491 test sources found nothing genuinely legacy/obsolete
to remove — apparent orphans test live behavior and are mostly registered via
string-constructing CMake foreach loops (dr808_${tgt}_tests, ${_test}_tests.cpp)
that a filename grep misses; removing them would delete real coverage. One true
orphan, auv3_render_scratch_transport_v591, was restored to the build with a
linkage-tolerant stable-scratch assertion; ScopeTripleBufferMultiConsumerV687Tests
was de-flaked (producer waits for both consumers before ending — scheduling
artifact, not a buffer defect). Guard: TestSuiteIntegrityV969Tests. External
macOS/SDK release validation remains pending.

# AI2AI previous overlay — v968 DrSID kit-save normalization + full-suite green (historical)

Package was `0.0.690-pass380-v968-drsid-kit-save-and-queue-test-closure`.

v968 closes the last fixable DrSID/kit/GM audit item and repairs the four
pre-v965 tests that had drifted. DrSID user-kit SAVE now normalizes the saved
document into DrSID mode via `_saveDrumKitDocumentToURL` (forces
DrSidEnable=1/SynthMode=0 for `.arpsid` and `.json`), matching import/export.
`ReleaseFinalClosureV404Tests`/`ReleaseRuntimeClosureV408Tests` were re-pinned to
the PIPE-013 release-reserve admission and PIPE-001 arrival-order/emergency-kill
sort; `ClassicModeAuthorityClosureV909Tests` to the published telemetry ARP/SEQ
authority atomics; `GuiViewControllerWiringV590Tests` to the multi-line DIGI
render call. New test: `DrSidUserKitSaveModeNormalizationV968Tests`. External
macOS/SDK release validation remains pending.

# AI2AI previous overlay — v967 DrSID quick-kit base-profile parity (historical)

Package was `0.0.690-pass380-v967-drsid-quick-kit-base-parity-closure`.

v967 closes a DrSID quick-kit split-brain found by audit: the on-screen kit
popup overlays a GUI tone table (`kArpSIDDrumKitToneProfiles` in
`source/au3/ArpSIDViewController.mm`) on top of the factory patch. Its
"Standard" row is authored to mirror the canonical factory DrSID base
(`applyFactoryDrSidDefaults` in `source/factory_patch_params.h`), but the v909
factory tom-decay correction (0.30→0.44) was never mirrored, so the popup and
the host program list voiced the same default kit differently. The GUI base is
re-synced and pinned by `DrSidQuickKitBaseProfileParityV967Tests`. Also fixed:
the GUI-only "Standard" kit (slot -1) applied nothing to audio while reporting a
load. The curated quick kits remain intentionally distinct voicings.

# AI2AI previous overlay — v966 host parameter presentation authority (historical)

Package was `0.0.690-pass380-v966-parameter-presentation-authority-closure`.

v966 closes the host parameter display/parse split-brain (handoff 22.1), the
VST UTF-8 byte-cast (22.2) and unconditional VST editor stderr diagnostics
(22.8): one shared parameter-ID-aware service
(`include/arpsid/core/sid_parameter_presentation.h`) now owns host text for
AUv2, AUv3 and VST3, delegating to the same law helpers the runtime renders
with (consolidated in `math_utils.h` with inverses; DSP numerics unchanged).
Regression test: `ParameterPresentationAuthorityV966Tests`. External
macOS/SDK release validation remains pending.

# AI2AI previous overlay — v952 runtime behavior/parity closure (historical)
The historical June handoff below remains useful incident context, but its
workspace path, installed-binary hash, and candidate status are not current
source-package truth. V952 closes SID808 KIT Play/Stop/reset rendering with a
real kernel audio test, implements semantic parameter normalization and shared
host cardinality, makes dirty fallback generation-ordered with normal side
effects, and reconciles AU3/Phase2 state-root local policy off an already
producer-canonicalized root. Local CTest: 465/465. External macOS/SDK release
validation remains pending.

# AI2AI HANDOFF - ArpSID Logic AUv2 / C64P SIDPLAY Release (historical)

Machine-readable handoff for a successor agent. Date: 2026-06-30. All claims are tagged.

## 0. Environment

- [CONFIRMED] Workspace: `/Users/ulfbertilsson/Downloads/ArpSID-0.0.690-pass380-v840-stall-instrumentation`
- [CONFIRMED] No git repo in this folder.
- [CONFIRMED] Installed AUv2 component path: `~/Library/Audio/Plug-Ins/Components/ArpSID.component`
- [CONFIRMED] Logic app: `/Applications/Logic Pro X.app`
- [CONFIRMED] Current installed binary SHA256:
  `d32346194bedf2b1982200c78acaf037af8ba618c3d70193a8d000b69246c8fb`
  (v844 candidate: true pre-cap C64 backlog telemetry, explicit C64 bypassed-note cleanup, real open-bus light telemetry/scope; includes v841/v840/v839/v838 and v843)
- [CONFIRMED] Known-good fallback: pass353 binary SHA
  `937216f170b2beba29f6cf427babe4eff0b6f6f21c1361dd7819f2e10d9ab658`

## 1. Product Facts

- [CONFIRMED] AUv2 bundle id: `com.arpsid.auv2`
- [CONFIRMED] AudioComponents: type `aumu`, manufacturer `ASID`, subtypes `ArpS`, `ArIn`, `DrSD`, `S808`, `C64P`
- [CONFIRMED] `ArpSID::ArpSIDDSPKernel` is 64-aligned and about 5.57 MB.
- [CONFIRMED] `ArpSID::GUI::DigiSampleBankBlob` is pinned at 480,392 bytes.
- [CONFIRMED] `GuiRealtimeModelSnapshot_` contains `MixPanelModel`, `KitStateBlob`, `DigiPanelModel`, and `DigiSampleBankBlob`, so it is large enough to be unsafe as a Logic AU-host stack local.

## 2. Current Build Ledger

| SHA256 | Build | Result |
|---|---|---|
| `937216f170b2beba29f6cf427babe4eff0b6f6f21c1361dd7819f2e10d9ab658` | pass353 known-good | [CONFIRMED] Works in Logic baseline |
| `786ddef641b8f3a9070b5a563f1ffed760e8c66c8af87473ea86d0e6c92ab7f7` | pass377 stock | [CONFIRMED] Logic crash |
| `9a63ccf5e3763efc4fb42658338b79821b9c21309467cd5f9d4ae851302c90d9` | pass380 stock | [CONFIRMED] Logic crash |
| `1d94b7cd804892542115cae999df4d1ecc2d67066f240cc82db8f511fcc9aa43` | first v843 ownership candidate | [CONFIRMED] auval pass; [DISPROVEN] not enough, real `.ips` showed stack crash |
| `029e927ce2de960f2ed780c4cdd63753873235bffe6bb035a202460d9f27f369` | first stack-fix candidate | [CONFIRMED] auval pass; [SUPERSEDED] reduced captured stack site, but remaining large stack locals existed |
| `108a5e80d75b75fd59a87d33ce6c6dac048475a5192200a6111a3608a06021ac` | stack-wide v843 candidate | [CONFIRMED] auval pass; superseded by v838 |
| `76179da34b542161fd10c88920b19e422fbcea9934b262f15200ea2d8c2ecd8f` | v838 VIC-fast (default off) + v843 stack fix | [CONFIRMED] auval pass; [CONFIRMED] user: stable but VIC fast/acc made no audible change |
| `98b60ec96d4791995ad18cb2b8f62cb5b7447145199fcdce61d7a59d8022aefb` | v839 6510-fast (default off) + v838 + v843 | [CONFIRMED] auval pass; [MEASURED] emulation ~12x realtime, 6510-fast ~6%, VIC-fast ~8% slower → emulation not the choppiness cause |
| `b33cc3ff24ceaeb6d49837462fde0de38548d0283a6ccacc8122a2d61831e862` | v840 render-path stall instrumentation + v839/838/843 | [CONFIRMED] installed, 122/122 C64/diag/telemetry/render tests pass, all 5 auval pass; surfaces render µs/overruns/host-gap/catch-up/debt to GUI; [SUPERSEDED] v841 fixed the exposed catch-up burst |
| `8f35c1a53a0f58f5a8bdc6132c361e3550099e8db5b2e64c5eafbc2df053c6e0` | v841 continuous catch-up clamp + v840/839/838/843 | [CONFIRMED] installed; [CONFIRMED] targeted C64 continuous/timing tests 7/7; [CONFIRMED] codesign pass; [CONFIRMED] all five AUv2 flavors visible to `auval -a`; [SUPERSEDED] user later reported remaining choppiness / possible stuck poly note |
| `d32346194bedf2b1982200c78acaf037af8ba618c3d70193a8d000b69246c8fb` | v844 true backlog telemetry + C64 bypassed-note cleanup + real open-bus telemetry/scope | [CONFIRMED] installed 2026-06-30; [CONFIRMED] targeted guards pass; [CONFIRMED] AUv2 build/sign/install/cache refresh/strict auval pass; [PENDING-USER] Logic C64P field playback confirmation |

## 3. Problem Classes

### Problem #1 - AUv2 editor hang

- [CONFIRMED] Fixed by v835-style synchronous editor build in `source/au3/ArpSIDViewController.mm`.
- [CONFIRMED] `ArpSIDShouldDeferAuv2OOPBootstrap_v831()` returns `NO`; first-paint fence is pre-released.
- [CONFIRMED] Keep this fix. It is independent of the AU-host pre-editor crash.

### Problem #2 - Logic AU-host pre-editor crash

- [CONFIRMED] Real `.ips` reports were captured after reboot:
  `~/Library/Logs/DiagnosticReports/AUHostingServiceXPC_arrow-2026-06-29-201020.ips`
  and similar reports around 20:08-20:10.
- [CONFIRMED] Crash signature:
  - `EXC_BAD_ACCESS / SIGBUS`
  - `Thread stack size exceeded`
  - faulting thread: `1`
  - top stack: `___chkstk_darwin`
  - then `ArpSID::ArpSIDDSPKernel::publishGuiRealtimeModels(...)`
  - then `-[ArpSIDDSPKernelAdapter _publishSanitizedGuiRealtimeModels_v150]`
  - then `-[ArpSIDAudioUnit prepareStandaloneWithSampleRate:maxFrames:]`
- [CONFIRMED] Immediate cause: large GUI/DIGI objects copied on Logic AUHostingService worker stack before the editor runs.
- [DISPROVEN] The immediate cause is not simple 64-byte alignment or the `make_shared` control block.

## 4. Ruled Out

- [DISPROVEN] Stale AUv3 wrapper or AUv3 factory principal.
- [DISPROVEN] `Invalid property id 1843` as root cause. It is bridge noise.
- [DISPROVEN] Signing/library validation as root cause.
- [DISPROVEN] AppKit `_buildUI` and CVDisplayLink as the pre-editor crash. The crash occurs before `ArpSIDVC` runs.
- [DISPROVEN] Simple 64-byte alignment. Local measurement showed `make_unique`, `make_shared`, and `shared_ptr(new)` all 64-aligned for `ArpSIDDSPKernel`.
- [SUPERSEDED] "shared_ptr/make_shared is the root cause" is too broad as a final explanation. The unique ownership change is still useful, but the real captured crash was stack exhaustion in GUI realtime publication.

## 5. Patch Details

- [CONFIRMED] `source/au3/ArpSIDDSPKernelAdapter.mm`
  - `_kernel` is `std::unique_ptr<ArpSID::ArpSIDDSPKernel>`.
  - constructed with `std::make_unique<ArpSID::ArpSIDDSPKernel>()`.
  - `loadSidFileData` is synchronous via `_kernel->loadPsidData(...)`.
  - adapter publication passes `&_digiPair_v596_.bank`; it no longer creates a local stack `DigiSampleBankBlob bank`.
  - DIGI bank default/reset paths use `resetDigiSampleBankBlob(...)` in place.
  - combined getter writes into caller-provided storage instead of stack-local `b`.
- [CONFIRMED] `source/au3/ArpSIDDSPKernel.hpp`
  - `publishGuiRealtimeModels` writes directly to `guiRealtimeMailbox_.producerSlot()`.
  - no local `GuiRealtimeModelSnapshot_ next`.
  - no return-by-value `makeDefaultGuiRealtimeModelSnapshot_()` helper.
  - `guiRealtimeRender_` is object-owned storage reset in constructor.
- [CONFIRMED] `source/au3/ArpSIDAudioUnit.mm`
  - restore/save transient DIGI banks use `std::make_unique<DigiSampleBankBlob>()`.
- [CONFIRMED] `source/au3/ArpSIDViewController.mm`
  - editor round-trip verify bank uses heap storage.
- [CONFIRMED] `include/arpsid/gui/digi_sample_bank_v596.h`
  - `resetDigiSampleBankBlob(DigiSampleBankBlob&)` exists and is used by sanitize/load fallback paths.
- [CONFIRMED] `source/tests/dsp_kernel_gui_realtime_publish_stack_v843_tests.cpp`
  - guards the stack-safe shapes above.

## 6. Validation Run

- [CONFIRMED] Build passed:
  `cmake --build build --target arpsid_auv2 arpsid_dsp_kernel_gui_realtime_publish_stack_v843_tests -j 8`
- [CONFIRMED] Direct stack guard passed:
  `DspKernelGuiRealtimePublishStackV843Tests PASS`
- [CONFIRMED] Targeted CTest passed 4/4:
  `DspKernelGuiRealtimePublishStackV843Tests`
  `DspKernelAllocationShapeV842Tests`
  `PsidAsyncLoadKernelLifetimeV780Tests`
  `Auv2KernelOveralignedNoMakeSharedV836Tests`
- [CONFIRMED] Installed via:
  `cmake --build build --target arpsid_auv2_install_user`
- [CONFIRMED] Strict auval passed for all five:
  `auval -v aumu ArpS ASID`
  `auval -v aumu ArIn ASID`
  `auval -v aumu DrSD ASID`
  `auval -v aumu S808 ASID`
  `auval -v aumu C64P ASID`
- [CONFIRMED] Final pass380/v841 package run passed:
  `RELEASE_NAME=ArpSID-0.0.690-pass380-v841-sidplay-continuous-catchup-release PACKAGE_OUT=dist/... ./build.sh --generator 'Unix Makefiles' --package-release --parallel 8`
  - release-check curated guards: 7/7
  - full CTest: 370/370
  - package written under `dist/ArpSID-0.0.690-pass380-v841-sidplay-continuous-catchup-release.zip`
- [CONFIRMED] Final AUv2 reinstall/cache refresh/strict validation passed:
  `ARPSID_AUV2_HARD_REFRESH=1 ./build.sh --generator 'Unix Makefiles' --install-auv2 --clear-au-cache --validate-auv2 --no-tests --parallel 8`
  - installed binary SHA256: `8f35c1a53a0f58f5a8bdc6132c361e3550099e8db5b2e64c5eafbc2df053c6e0`
  - codesign: PASS
  - strict verifier: PASS
  - `auval -a` lists `ArIn`, `ArpS`, `C64P`, `DrSD`, `S808`
- [CONFIRMED] v844 focused guards passed directly:
  `C64RenderStallInstrumentationV840Tests PASS`
  `GuiTelemetryScopeClosureV743Tests PASS`
  `C64ScopeTelemetryFixClosureV744Tests PASS`
- [CONFIRMED] v844 AUv2 build/sign/install/cache refresh/strict validation passed:
  `ARPSID_AUV2_HARD_REFRESH=1 ./build.sh --generator 'Unix Makefiles' --install-auv2 --clear-au-cache --validate-auv2 --no-tests --parallel 8`
  - installed binary SHA256: `d32346194bedf2b1982200c78acaf037af8ba618c3d70193a8d000b69246c8fb`
  - codesign: PASS
  - strict verifier / auval: PASS
- [CONFIRMED] v844 package validation passed:
  `RELEASE_NAME=ArpSID-0.0.690-pass380-v844-c64-backlog-openbus-release PACKAGE_OUT=dist/... ./build.sh --generator 'Unix Makefiles' --package-release --parallel 8`
  - release-check curated guards: 7/7
  - full CTest: 370/370
  - package written under `dist/ArpSID-0.0.690-pass380-v844-c64-backlog-openbus-release.zip`

## 7. Latest Logic Observation

- [CONFIRMED] After the first stackfix, user reported "stuck like before".
- [CONFIRMED] Process state then showed Logic and InfoHelper alive, no AUHosting crash, no new `.ips`, and no process holding `ArpSID.component` open.
- [CONFIRMED] Samples showed Logic in the normal AppKit event loop and InfoHelper waiting on XPC.
- [INFERENCE] That observation did not reproduce the AU-host crash; it looked like a non-crash Logic/UI/cache state or a test that did not reach ArpSID load.

## 8. SIDPLAY choppiness final closure (v841)

- [CONFIRMED] Separate problem class from the crash: under the **C64P** flavor some SID tunes played stop-start / choppy in Logic.
- [DISPROVEN] Inherent PHI2 compute cost was not the root cause. Standalone measurement showed the cycle-accurate C64 core at about 12x realtime, `6510:FAST` saved only about 6%, and `VIC:FAST` was about 8% slower.
- [CONFIRMED] v838 `VIC:FAST` and v839 `6510:FAST` remain realtime, default-OFF diagnostic/performance levers. They are not the final fix.
- [CONFIRMED] v840 render-path timers localized the failure shape: continuous catch-up could burst far beyond the current AU buffer's mappable PHI2 span after stale passive-cycle debt accumulated.
- [CONFIRMED] v841 root cause: the continuous C64 runtime path consumed all stale `c64PsidPassiveCycleDebt_` in one render callback (`catchup = c64PsidPassiveCycleDebt_`). Timed SID writes beyond the current audio block then clamped to the last sample, creating a block-edge burst and the audible play-stop-play-stop pattern.
- [CONFIRMED] v841 fix: track `passiveCyclesAccruedThisBlock`, derive `continuousAudioBlockCycles`, cap continuous catch-up to `min(debtBeforeContinuousRun, continuousAudioBlockCycles)`, and drop successfully replayed stale realtime backlog instead of compressing it into the present buffer.
- [CONFIRMED] Scope: VBI and CIA-timed PSID paths were already bounded to a play period; the v841 change is for the continuous RSID / PSID `playAddress == 0` path.
- [CONFIRMED] User tested after install/cache refresh: "sid played fine now."
- [SUPERSEDED] User later reported: "Still choppy" and "poly seems note stuck"; v844 tightens the same path and adds bypassed-note cleanup / open-bus telemetry truth.
- [CONFIRMED] v844 keeps realtime pacing byte-for-byte capped to `min(debtBeforeContinuousRun, continuousAudioBlockCycles)`, but `noteC64Catchup_` now records the true pre-cap debt so the CATCHUP MAX readout still reveals spikes.
- [CONFIRMED] v844 backlog drop is explicit and only runs when `debtBeforeContinuousRun > continuousAudioBlockCycles` after a completed cycle budget.
- [CONFIRMED] v844 C64 player branch calls `clearC64BypassedPerformanceState_()` to clear bypassed synth/BPE/DrSID/ARP/DIGI note state without entering the normal render path or injecting normal all-notes-off SID writes into C64 timed writes.
- [CONFIRMED] v844 open-bus visualization uses real light telemetry: decay mask, hold state, age, last-driven cycle, and SID/Color-RAM/POTX/POTY open-bus counters. AU adapter fallback no longer fakes `0xFF` / zero counters, and the scope plots decayed latch value instead of XOR eye candy.
- [CONFIRMED] Guard `C64RenderStallInstrumentationV840Tests` now rejects the old whole-debt catch-up shape, requires current-buffer PHI2-span cap + explicit backlog drop, and requires the C64 bypassed-note cleanup helper.
- [CONFIRMED] Guard `GuiTelemetryScopeClosureV743Tests` now rejects fake open-bus fallback and XOR open-bus scope rendering.

## 8b. Measurement / instrumentation retained

- [MEASURED] Standalone `-O2` microbenchmark of `C64Phi2Machine` (identical SID-playroutine instruction stream per mode, scratchpad `phi2_bench.cpp`, two passes):
  - accurate baseline: **~84 ns/cycle ~= 12x realtime** (PAL 985248 Hz).
  - 6510-fast (per-cycle diagnostics snapshot gated): **~78 ns/cycle, -6.5%**.
  - VIC-fast: **~90 ns/cycle, +7.8% slower**; forcing AEC high removes badline CPU stalls, so the CPU does more work per cycle.
- [CONFIRMED] Budget constants were not throttling below realtime: `kC64RsidMaxInstructionsPerAudioBlock = 0` (continuous path), `kC64PsidMaxInstructionsPerPlay = 16384`, `kC64PsidMaxPassiveDebtCycles = palVicFrameCycles()*8` (~157k cyc ~= 8 PAL frames).
- [DONE v840] Render-thread stall instrumentation remains live and surfaced to the GUI. Six RT-safe counters (lock-free `compare_exchange` maxes, two `steady_clock` reads/block): `c64RenderLastBlockMicros`, `c64RenderMaxBlockMicros`, `c64RenderOverrunCount` (render us > deadline), `c64MaxHostGapMicros` (host delivery jitter), `c64MaxCatchupCycles` (continuous catch-up burst), `c64LastPassiveDebtCycles`. Guard `C64RenderStallInstrumentationV840Tests`.
- [DECISION] The instruction-atomic engine remains locked by `ARPSID_C64_PHYSICAL_ONLY`; it is not relevant to a stall that is not compute-bound.

## 9. Next Actions

1. [DONE] Source release package generated for pass380/v844.
2. [DONE] Release/package checks passed: curated release-check 7/7 and full CTest 370/370.
3. [DONE] Final installed AUv2 verification passed on macOS for the installed component.
4. [PENDING-EXTERNAL] Notarization, VST3 SDK/toolchain validation, and AUv3 product packaging remain separate external release gates unless explicitly requested.
5. [WATCH] If a crash ever recurs, collect newest `AUHostingServiceXPC_arrow*.ips` and compare against the old `publishGuiRealtimeModels` / `___chkstk_darwin` stack-overflow signature.

## 10. Docs Corrected (was "still to correct")

- [DONE] README v836/make_shared alignment text corrected — the over-alignment theory is marked DISPROVEN and the real v843 stack-overflow root is documented.
- [DONE] `PASS353_LOGIC_DIFF_AUDIT.md` carries the DISPROVEN v836 section + the v843 actual-root section.
- [DONE] No release is labelled `makeshared-overalign-fix`; the v838 release is labelled for the VIC-fast + v843 stack fix.
