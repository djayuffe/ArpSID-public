# ArpSID-public — AI-to-AI Low-Level Audit

- **Target**: https://github.com/djayuffe/ArpSID-public
- **Commit audited**: `10a5acc` — "Release 0.9.15" (Thu Oct 1 13:06:06 2026 +0000)
- **Date**: 2026-10-02
- **Method**: 5 parallel deep-read agents (core DSP/C64 emulation, VST3/AUv2 wrappers + GUI,
  build/CI/packaging, standalone app + tests + docs, and a second pass over the largest files
  skipped by the first pass). Every P0/P1 below was re-verified by direct source inspection
  before inclusion; agent output was treated as claims, not facts.

## 0. Executive summary

| Severity | Count | One-line character |
|---|---|---|
| **P0** | 3 | 1 real real-time stall hazard (unbounded CPU-catch-up loop), 1 structurally wrong multi-SID register mirror (masked by current mode matrix), 1 user-facing installer that can silently execute `main`-branch code for old releases |
| **P1** | 17 | RT bounded-work violations, an unbounded `join()` + unlocked host-callback reads on AUv2 teardown, a Release-build test blind spot (45 files), a broken `--check` gate, SDK fetch acceptance gap, swallowed build-retry exit codes, entitlement mismatch |
| **P2** | 21 | hygiene: dead code, unclamped host-supplied offsets, stale docs, duplicate scripts, inverted ternary, missing clamps |

**Overall assessment**: unusually high engineering quality for a plugin of this size. The C64
PHI2/CIA/CPU micro-op core, the Vyukov MPSC ingress rings, the PSID/patchbank parsers, the
AUv2 render buffer math, and the view-factory timeout arbitration were all traced and found
correct. The findings concentrate in (a) the C64 readback catch-up path, (b) AUv2 teardown
threading, (c) test-coverage decay in Release builds, and (d) installer/supply-chain edges.
No memory-corruption P0s were found in any audited scope.

---

## 1. P0 findings

### P0-1 — Unbounded PHI2 catch-up loop in the SID readback model (RT stall hazard)
`include/arpsid/core/c64_sid_readback.h:78-88`
```cpp
void advanceTo(uint64_t phi2) noexcept {
    if (!clockStarted_) { clockStarted_ = true; clockCursor_ = phi2; return; }
    if (phi2 <= clockCursor_) return;
    uint64_t cycles = phi2 - clockCursor_;
    while (cycles-- != 0u) clockOne_();
    clockCursor_ = phi2;
}
```
`read()` (line 66) calls `advanceTo()` unconditionally. The main 6510 path has a cycle budget;
this one does not. A PHI2 gap of ~19 656 cycles (a full VBI at PAL timing — the common
write→read cadence in PSID playback) executes ~19.7k oscillator cycles **inside the audio
callback**.
- **Failure scenario**: a tune with long SID-write-free stretches followed by a `$D41B`/`$D41C`
  read produces a multi-microsecond stall per read, at 44.1 kHz with small render blocks.
- **Mitigating factor (verified)**: the readback only advances on the first read *after* a
  `write()` (first read before any write returns `openBus`), so exposure is per
  write→first-read gap, not per raw cycle. Still a real RT hazard.
- **Fix**: cap the catch-up (e.g. `std::min(cycles, kMaxCatchUpCycles)`) and record the gap in
  telemetry, mirroring the main CPU's budget; or make the readback lazy (advance only as far as
  needed for the read register's state).

### P0-2 — Multi-SID register writes stomp the flat `regs[]` mirror with chip 0
`include/arpsid/core/c64_sid_bridge.h:201`
```cpp
regsByChip[chip][r] = value;
...
regs[r] = regsByChip[0][r];   // executed on EVERY write, to ANY chip
```
Every `sidWrite()` to *any* chip overwrites the flat `regs[]` mirror with **chip 0's** value,
corrupting the primary-SID register image seen by `regs[]` consumers whenever `chip != 0` is
written between reads.
- **Failure scenario**: multi-SID PSID tunes that write SID1/SID2 registers and later read
  `regs[]` get chip-0's stale/foreign value.
- **Masked today (verified)**: the only current `regs[]` consumer is the DIGI D418 path, and
  the C64SidPlayer flavor (where multi-SID is possible) disables the DIGI layer. The bug is
  structurally live and will misfire the moment any flavor enables both.
- **Fix**: `if (chip == 0u) regs[r] = value;` or make `regs[]` a view of `regsByChip[primaryChip]`.

### P0-3 — `get_arpsid.sh` can execute an unpinned `main`-branch installer for old releases
`scripts/install/get_arpsid.sh:93-105`
```sh
# Releases from 0.9.8 carry their installer; older ones use the repository copy.
if [ "$OS" = Darwin ]; then
  [ -f "$WORK/extract/install_macos.sh" ] && INSTALLER="$WORK/extract/install_macos.sh"
  [ -n "$INSTALLER" ] || { INSTALLER="$WORK/install_macos.sh"; curl -fsSL -o "$INSTALLER" \
      "$RAW_BASE/scripts/install/install_macos.sh"; }
```
`RAW_BASE` points at the **`main` branch**. For any pre-0.9.8 release (or any product zip
lacking the embedded installer), the one-liner downloads the *current* `main` installer and
runs it with `bash` — with `--system` (sudo) capability — against an old, pinned release.
The SHA256SUMS check (lines 78-88) covers only the zips, not this fetch. A compromised or
drifted `main` silently executes arbitrary code in the user's install flow.
- **Fix**: pin the fallback installer to the release ref (`.../v$VERSION/scripts/install/...`),
  and checksum it like the zips.

---

## 2. P1 findings

### P1-1 — AUv2 teardown: unbounded `join()` on the transport poller + unlocked host-callback reads
`source/au2/ArpSIDAUv2Component.mm:2516-2548` (`componentClose`), `1856-1888` (poller)
```cpp
impl->hostTransportPollerStop.store(true, std::memory_order_release);
if (impl->hostTransportPoller.joinable()) {
    impl->hostTransportPoller.join();          // unbounded, no timeout
}
```
The poller thread reads `impl->hostCallbacks` (a plain, non-atomic 5-pointer struct,
line 597) **without holding `activityMutex`** and then *calls host code*
(`transportStateProc2`, `beatAndTempoProc`, `musicalTimeLocationProc`). It is **not** an
`activeUsers` user, so the close path's `closeWaitCv.wait(activeUsers == 0)` does not cover it.
- **Failure scenario A (hang)**: the host's callback blocks or takes a lock held by the closing
  thread → `componentClose` hangs forever in `join()`.
- **Failure scenario B (UAF on host state)**: close starts, poller is already inside a proc
  call with `hostUserData` the host is simultaneously freeing.
- **Failure scenario C (torn read)**: `componentUninitialize` joins the poller *before* taking
  `activityMutex`; a concurrent `componentSetProperty(kAudioUnitProperty_HostCallbacks)` can be
  mid-update of the struct while the poller reads it.
- **Fix**: make the poller an `activeUsers` user (begin/end per iteration) so the existing
  close wait covers it; bound the join (50 ms, like `waitForRenderUsersToDrainBounded_`); copy
  `hostCallbacks` under `activityMutex` per iteration.

### P1-2 — 45 test files use bare `assert()` → all contract coverage vanishes in Release
Representative: `source/tests/digi_panel_model_v563_tests.cpp:48-60`
```cpp
assert(m.schemaVersion == kDigiPanelSchemaVersion);
assert(m.activeSlot    == 0u);
assert(digiPanelIsWellFormed(m));
```
`build.sh:19` defaults to `CONFIG=Release`; the DSP-kernel Release rule adds `-DNDEBUG`
(`CMakeLists.txt:1994`). 45 of 490 test files (the whole C64 PSID/PHI2 family, all `digi_*`,
all `kit_*`, `lfo_*`, `closure_regression_tests`, ...) use bare `assert()` without `#undef
NDEBUG`; only 1 has a `require()` fallback. The repo's own contract helpers
(`runtime_test_common.h:19-24` `fail()`, `dr808Require`) `abort()` unconditionally and are the
correct pattern.
- **Failure scenario**: a regression in e.g. `DigiPanelModel::sanitize` is compiled out of
  `assert()`, `main()` returns 0, CTest reports PASS. Contract coverage is silently absent
  exactly in the default build configuration.
- **Fix**: `#undef NDEBUG` at the top of all 45 (as `release_gate_regression_tests.cpp:2`
  already does), or convert to `require()`.

### P1-3 — Two "source-contract" guard tests are CWD-dependent and will spuriously fail
`source/tests/c64_sidplay_audio_clock_v886_source_tests.cpp:11-12`
```cpp
std::ifstream f("source/au3/ArpSIDDSPKernel.hpp");
if (!f) f.open("../source/au3/ArpSIDDSPKernel.hpp");
```
and `source/tests/sid_runtime_physical_clock_law_v886_source_tests.cpp:14`
(`../source/vst3/arpsid_vst3_processor.cpp`).
Every other source-reading guard test (45+ sites in CMake) receives
`ARPSID_SOURCE_ROOT`/`ARPSID_SOURCE_DIR` compile definitions; these two (CMake ~5576/5583)
get neither and do not use the portable `dirnameOf(__FILE__)` candidate list their v884/v885
siblings use.
- **Failure scenario**: run from any CWD other than the repo root (or `build/source/tests`)
  → the test `return 1`s with "cannot open" — a spurious red unrelated to the code under test.
- **Fix**: add the `ARPSID_SOURCE_ROOT` definition, or reuse the `__FILE__`-relative fallback.

### P1-4 — RT bounded-work violation: O(n) replacement scan per rejected event under MIDI storm
`include/arpsid/core/sid_event_queue.h:347-362`
```cpp
if (count >= admissionLimit) {
    ...
    for (size_t i = 0; i < static_cast<size_t>(count), ...; ++i) {
        const uint8_t pri = sidTimedEventPriority(events[i].type);
        if (replaceIdx < 0 || pri > worstPriority || ...) { ... }
    }
```
The queue's documented contract is bounded work per RT operation (Phase-0 RT law, line 331).
Once full, every *subsequent* rejected event triggers a full O(kMaxSidTimedEvents) scan.
- **Failure scenario**: a MIDI note-on storm keeps the queue pinned at capacity → O(n) per
  event on the render thread.
- **Fix**: keep a running worst-priority candidate (amortized O(1) replace), or rate-limit
  the scan.
- Related design note (P1-7, kept at P2 in final tally): `kReleaseReserve` admission (lines
  339-340) means non-release events hit the wall 16 slots early and can never displace
  release-critical occupants — by design, but the "1/16th reserve" comment overstates the
  guarantee.

### P1-5 — `install_vst3.sh --check` passes when verification was impossible
`scripts/install/install_vst3.sh:108-143`
```sh
check_bundle() {
  local ok=0
  if [ "$OS" = Linux ]; then
    ...
    if command -v ldd >/dev/null 2>&1; then ... fi
  else
    [ -d "$FROM/Contents/MacOS" ] || { ... return 1; }
    echo "Bundle architectures: $(lipo -archs ... 2>/dev/null || echo unknown)"
  fi
  return $ok
}
```
- macOS `--check`: if `lipo` fails or the binary is missing, the line prints "unknown" but the
  function returns 0 → the documented `./install.sh --check` gate reports a false pass.
- Linux `--check` without `ldd`: no check runs at all, exit 0.
- **Fix**: `return 1` when the architecture/library verification could not be performed;
  make `--check` fail-closed.

### P1-6 — `fetch_vst3_sdk.sh` accepts any checkout it cannot verify
`scripts/fetch_vst3_sdk.sh:14-22`
```sh
if [ -f "$DIR/CMakeLists.txt" ] && [ -d "$DIR/public.sdk" ] && [ -d "$DIR/vstgui4/vstgui" ]; then
  have="$(git -C "$DIR" describe --tags --exact-match 2>/dev/null || true)"
  if [ -z "$have" ] || [ "$have" = "$TAG" ]; then
    echo "$DIR"; exit 0
```
`[ -z "$have" ]` accepts the directory whenever `describe` fails *for any reason* — including
a shallow clone lacking the tag object (exactly what `--depth 1` re-runs produce) or a
detached HEAD at the tag commit. A stale or foreign `.deps/vst3sdk` then silently drives the
build (trusted by `build.sh:177`).
- **Fix**: verify `git -C "$DIR" rev-parse HEAD` against the pinned tag's commit; re-clone when
  unconfirmable. Refuse to clone into an existing non-empty dir (build.sh:178 path).

### P1-7 — Build preflight swallows the serial-retry result
`scripts/run_full_ctest_preflight.sh:34-41` and `scripts/arpsid_build_helpers.sh:39-51`
```sh
set +e
cmake --build "${BUILD}" -j"${NCPU}" 2>&1 | tee "${LOG_DIR}/04_build_all_parallel.log"
BUILD_RC=${PIPESTATUS[0]}
set -e
if [[ ${BUILD_RC} -ne 0 ]]; then
  echo "WARN: parallel build failed; retrying serial build ..." >&2
  cmake --build "${BUILD}" -j1 2>&1 | tee "${LOG_DIR}/04b_build_all_serial_retry.log"
fi
```
The serial retry's exit code is discarded (pipe status = `tee`'s). If the serial build also
fails, the script proceeds to `ctest` — vacuous pass on stale binaries or a confusing
failure. Callers (e.g. `macos_build_install_validate_auv2.sh:30`) believe the build succeeded
and fail later at install with "no ArpSID.component found".
- **Fix**: capture `PIPESTATUS[0]` of the retry and propagate it.

### P1-8 — `sign_auv2_component.sh` shells out via `eval` on untrusted paths
`scripts/macos/sign_auv2_component.sh:44-54`
```sh
ARGS="--force --sign \"$IDENTITY\" --timestamp=none"
...
eval "$CODESIGN_BIN $ARGS"
```
The project's own `CMakeLists.txt:13-16` declares paths like `ArpSID RC(1)` legal and requires
"fix quoting at the command boundary" — this script re-introduces shell interpretation: a
bundle dir with `(`, `)`, `$`, or `"` is word-split/globbed/mis-quoted by `eval`. Invoked from
the `arpsid_auv2_sign` target with `$<TARGET_BUNDLE_DIR>`. `sign_logic_bundle.sh:33-42` has the
same class (unquoted `$RUNTIME_ARGS`).
- **Fix**: shell array: `args=(--force --sign "$IDENTITY" ...); "$CODESIGN_BIN" "${args[@]}"`.

### P1-9 — Logic app signed with sandbox entitlements, contradicting documented design
`scripts/macos/sign_logic_bundle.sh:32-42` + `CMakeLists.txt:1700-1712`
The `arpsid_logic` target passes `ArpSIDHost.entitlements`, which contains
`com.apple.security.app-sandbox = true`, while the dev wrapper (CMakeLists:1473) and the
user-install path (CMakeLists:1887-1894) use `ArpSIDHostAudioCapture.entitlements` (non-sandbox,
whose comment says "These apps are intentionally NOT sandboxed"). A sandboxed host app breaks
file access for the C64 ROM/BASIC loaders and the DIGI capture path; the two call sites disagree
about the entitlements for the same bundle class.
- **Fix**: use `ArpSIDHostAudioCapture.entitlements` for the Logic app in both call sites.

### P1-10 — `ArpSIDStandaloneApp.cmake`: no `--no-undefined`, hand-compiled SDK sources
`cmake/ArpSIDStandaloneApp.cmake:94-123`
```cmake
target_link_libraries(arpsid_standalone_app PRIVATE arpsid_core arpsid_forensic_patchbank vstgui sdk_common
                                                    arpsid_rtio)
```
The VST3 module gets `LINKER:--no-undefined` on Linux (CMakeLists:596-600) "so it cannot fail
only when a host loads it"; the standalone app does not. It compiles `arpsid_vst3_kernel_host.cpp`
+ `memorystream.cpp` by hand and links only `sdk_common` (not `sdk`/`sdk_hosting`). An undefined
SDK symbol links fine and the binary fails at launch with a cryptic dyld/ld.so error — only the
manual `arpsid_standalone_check` target catches it.
- **Fix**: `target_link_options(arpsid_standalone_app PRIVATE "LINKER:--no-undefined")` on
  `UNIX AND NOT APPLE`; link `base` explicitly.

### P1-11 — macOS VST3 post-build hardcodes `VST3/Release/` for the friendly alias
`CMakeLists.txt:659-674` (vs `_arpsid_vst3_bundle` at :713)
The `ArpSID.vst3` alias is written to `VST3/Release/` while the bundle actually lands in
`VST3/$<CONFIG>/`. With `-DCMAKE_BUILD_TYPE=RelWithDebInfo` (used by the sanitizer job and any
user choosing it) the alias source is missing → `cmake -E copy_directory` fails or produces an
empty alias. `build.sh:268` works around it with `find`; CI hardcodes `Release`; any other
config breaks.
- **Fix**: build the alias under `$<TARGET_BUNDLE_DIR:arpsid_vst3>`'s `$<CONFIG>` dir.

### P1-12 — `release.yml` awk release-notes extraction is regex-metacharacter-fragile
`.github/workflows/release.yml:118-123`
```sh
awk -v v="$v" '
  $0 ~ "^## \\[" v "\\]" {on=1; next}
  on && /^## \[/ {exit}
  on {print}
' CHANGELOG.md > release-notes.md
test -s release-notes.md
```
`v` is interpolated raw into a dynamic regex; only `test -s` guards the result. A future
version with regex metacharacters, or a header format drift, silently extracts the wrong
section or an empty file caught late.
- **Fix**: `index($0, "## [" v "]") == 1` (string match) + loud error when no section found.

### P1-13 — `build.yml` macOS: stale-bundle `find -quit` + re-sign drops the friendly alias
`.github/workflows/build.yml:385-389`
```sh
vst="$(find build-vst3/VST3 -maxdepth 3 -type d -name 'arpsid_vst3.vst3' -print -quit)"
codesign --force --deep --sign - "$vst"
```
`-print -quit` takes the first match in filesystem order — in a multi-config tree
(`VST3/{Release,RelWithDebInfo,Debug}/`) it may sign the stale config. The re-sign also drops
the CMake post-build `ArpSID.vst3` alias, so the name advertised in `docs/INSTALL.md` never
ships in the macOS release zip (users get `arpsid_vst3.vst3`). Related: the macOS job builds
neither `arpsid_auv2_component_smoke` nor its ctest (only `--target arpsid_auv2`), so the
"AU renders audio" smoke (CMakeLists:1166-1252) runs only via the uncommitted local gate.
- **Fix**: `find ... -path "*/Release/arpsid_vst3.vst3"`; add the smoke target + ctest to the
  macOS job; ship (or document) the real bundle name.

### P1-14 — `componentClose` poller gap (see P1-1) — *consolidated above*
(counted once)

### P1-15 — `SaturatorProcessor` drive smoother: zeroed coefficient before `prepare()`
`include/arpsid/audio/mix_fx_processors.h:463-467` + `include/arpsid/core/param_smoothing.h:56-70`
```cpp
Scalar smoothed{};      // = 0
Scalar target{};        // = 0
double coefficient = 0; // = 0
```
`ParameterSmoother` defaults its coefficient to 0; only `prepare(sampleRate)` sets it. Any
caller doing `setParams()` → `processStereo()` without a preceding `prepare()` gets
`smoothed = smoothed + (target - smoothed) * 0` — a guaranteed drive ramp from 0 (click /
mis-scaled transistor/tube normalization on first render).
- **Masked in the shipped path (verified)**: `ArpSIDDSPKernel.hpp` re-prepares every processor
  at each sample-rate change. But the library's documented `USAGE PATTERN` (lines 61-74) and
  `static_assert(std::is_trivially_copyable)` (line 521) make the latent state real.
- **Fix**: give `ParameterSmoother` a non-zero default coefficient (1.0 = instant) or call
  `prepare` inside `setParams` when the coefficient is still 0.

### P1-16 — DIGI D418 path renders per-frame instead of batched
`source/au3/ArpSIDDSPKernel.hpp:9020-9045`
The D418 stream path calls `renderBlock(..., 1)` once **per sample** (up to 512 calls per
render block) instead of the batched `renderIntervalAccurate` path used elsewhere.
- **Failure scenario**: per-frame call overhead + function-call churn in the RT path; CPU
  headroom loss exactly on the (heavier) DIGI path.
- **Fix**: batch into one interval call per block, as the main SID path does.

### P1-17 — Mono-mode beat position drifts on live sample-rate change
`source/au3/ArpSIDDSPKernel.hpp:7470`
Beat position is recomputed from a captured `beatsPerSample` rather than re-derived at the
current sample rate.
- **Failure scenario**: a live sample-rate change mid-transport desynchronizes sequencer
  timing in mono mode.
- **Fix**: recompute `beatsPerSample` on sample-rate change (the kernel already re-prepares
  everything else there).

---

## 3. P2 findings

| # | Location | Issue |
|---|---|---|
| 1 | `source/au2/ArpSIDAUv2Component.mm:453-500` | `cachedFactoryPresetArray` process-lifetime leak of CFStrings + CFArray — intentional and documented (NULL-callback CFArray over static storage); ~few hundred KB at most. No action. |
| 2 | `source/au2/ArpSIDAUv2Component.mm:2143-2167` | GM-drum promotion: `autoPromotionAllowed` check is redundant with `decision.promote`; `sidCanonicalEvaluateGMDrSidPromotion` runs on every ch-10 note-on even when promotion is disallowed. Perf/hygiene only. |
| 3 | `source/au2/ArpSIDAUv2Component.mm:3965-3967` | `ArpSIDAUv2ViewFactory interfaceVersion` returns `0` (legacy). Hosts enforcing the modern contract (version `1`) silently reject the factory → no GUI. In-repo tests call `uiViewForAudioUnit:` directly, bypassing the version gate. Verify against current Logic contract. |
| 4 | `source/au2/ArpSIDAUv2Component.mm:3348-3349, 3285` | `componentScheduleParameters` ramp `startBufferOffset` and `componentSetParameter` `inBufferOffsetInFrames` are passed to the kernel as `(int32_t)` **unclamped** (contrast: `componentMIDIEvent` clamps at 3918-3920). A buggy host can inject out-of-range/negative sample offsets. Clamp to `maxFramesPerSlice - 1`. |
| 5 | `include/arpsid/audio/mix_fx_processors.h:546-547` | `BitcrusherProcessor`: at 16 bits the quantizer divides by 32767 → hard-limits \|x\| > 1.0, contradicting the "255 = essentially transparent" comment. Skip quantize when `quantBits_ >= 16`. |
| 6 | `include/arpsid/audio/mix_fx_processors.h:160-173` | `computeHighShelfCoeffs`: dead `a0inv` (identical to `a0hs`) + stream-of-consciousness dev comments ("Let me recalculate properly", "Wait — that's the same") shipped in a production DSP header. Delete. |
| 7 | `include/arpsid/audio/mix_fx_processors.h:502-509` | `SaturatorProcessor` Tube branch: positive half normalized by `tanh(safeDrive)`, negative by `safeDrive` — at drive=1 the DC-gain ratio is ~1.31:1, stronger than the comment implies. Document the actual ratio. |
| 8 | `source/standalone/arpsid_standalone_settings.h:119-120` | Windows `configFolder()`: `appData.empty() ? appData : appData / "ArpSID"` — inverted-dead ternary; on missing `APPDATA` returns an empty path and every settings save silently no-ops with no error. Add a sane fallback + log. |
| 9 | `source/standalone/arpsid_standalone_settings.h:48` | `Settings::sanitize` clamps `tab` to ≥0 but not to `kTabCount-1` (16). A corrupt conf with `tab = 99999` reaches the UI. `std::clamp(s.tab, 0, kTabCount - 1)`. |
| 10 | `source/standalone/arpsid_standalone_app.cpp:54-59` | `writeFile` rename-retry: if the *second* rename fails after removing `p`, the previous good file is destroyed and the `.tmp` is orphaned. Restore-or-log on second failure. |
| 11 | `source/standalone/arpsid_standalone_engine.cpp:72-75` | `Engine::process` `std::fill(outputs[c], outputs[c] + frames)` for c≥2 has no capacity bound beyond the caller's contract (safe today, single caller). Document/contract the `frames`-sized guarantee. |
| 12 | `source/tests/dr808_test_utils.h:54-59` | `renderMonoScript` consumes events only while `script[eventIndex].sample == i` — silently drops events if a future script is not ascending-sorted. Use a sorted view / `lower_bound`. |
| 13 | `source/tests/forensic_engine_sanity_v527_tests.cpp:248-250` | Tautological `require(X == nullptr || X != nullptr)` — author's own comment concedes "trivially true". Assert `== nullptr` or drop the guard. |
| 14 | `scripts/macos/{full,macos_full}_build_install_clear_au_logic_cache.sh` | Byte-identical duplicate wrappers (one `exec`s the other); the pair is pure redundancy (a static-guard test pins the literals, so keep one and repoint the guard). |
| 15 | `scripts/macos/package_complete_release.sh:92-110` | `BUILD-VALIDATION.txt` hardcodes stale counts ("282/282 passed", "17 visible production tabs") — the tree now registers ~441 tests / 490 sources. Capture real numbers or drop them. |
| 16 | `scripts/ci/check_build_warnings.sh:13-18` | Linker-warning regex misses `ld.lld`; `external/resid-fp` warnings (the product's DSP fallback header) are silently out of scope. Add `ld.lld`, decide on `external/`. |
| 17 | `.github/dependabot.yml` + 3× VST3 SDK tag | SDK tag duplicated in CMakeLists:426, fetch_vst3_sdk.sh:11, build.yml:37 with no coherence check (unlike the product version). Add a CI step asserting all three agree. |
| 18 | `scripts/install/install_macos.sh:62-66` vs `scripts/macos/refresh_auv2_component.sh:27-32` | Two shipped scripts disagree on AU cache refresh: one `killall -9 AudioComponentRegistrar` (which the other's comment explicitly documents as a no-op/contraindicated). Have `install_macos.sh` call the canonical script. |
| 19 | `CMakeLists.txt:1082-1086` | `list(REMOVE_ITEM _arpsid_auv2_sign_args --entitlements)` removes only the flag, not its value → dangling entitlements path passed to codesign as a positional arg (errors, or worse) whenever `ARPSID_CODESIGN_ENTITLEMENTS` is set. The similar code at :1806 removes both items correctly. |
| 20 | `scripts/fetch_vst3_sdk.sh:24-27` | `git clone` into an existing empty dir the user named proceeds silently; existing non-empty dir gives a confusing clone error. Refuse non-empty targets. |
| 21 | `tools/capture_logic_auv2_hang.sh:27,44` | 3 s × 10-sample `sample(8)` profiles per AU PID inside a 1 s loop overlap themselves with multiple AUHostingServiceXPC processes; the "8 samples then break" cap is reached after ~1 min of overlapping profiles. Hygiene only. |

---

## 4. Document drift

| Location | Claim | Reality |
|---|---|---|
| `docs/TECHNICAL_SPECIFICATIONS.md:31` | "435 registered CTest tests" | 441 `add_test(` in CMakeLists.txt |
| `README.md:52,566`, `docs/ARCHITECTURE.md:77` | "about 480" tests | 480 is the test-*source* count, not registered tests (441) |
| `docs/ARCHITECTURE.md:220` | Standalone entry point = `source/au3/ArpSIDHostMain.mm` | That is the macOS AUv3 host only; the Windows/Linux app is `source/standalone/*` (RtAudio/RtMidi + VSTGUI), omitted from the Wrappers and Source-tree tables |
| `docs/STANDALONE_APP.md` | (omission) | Does not document the `ARPSID_STANDALONE_CONFIG_DIR` env override honored by `configFolder()` (`settings.h:116`) |

Verified **accurate**: C64_EXACTNESS_BOUNDARIES.md, REALTIME_OWNERSHIP.md,
REALTIME_ROLLBACK_JOURNAL.md (dirty-journal caps 8192/1024, PHI2-only strict RSID, ownership
split all match code + guard tests), STANDALONE_APP.md (RtAudio/RtMidi, paths, 30 s autosave,
2 s hot-plug, CLI flags all match).

## 5. Verified-clean areas (due-diligence record)

Traced in full and found correct — do not re-investigate without new evidence:

- **C64 CPU6510 micro-sequencer** (`c64_cpu6510_micro.h`, 1652 L): JMP(ind) NMOS page-wrap
  bug, branch T3 wrong-page address, JSR/RTS/RTI stack sequencing, decimal ADC/SBC flag
  derivation, `computeAbsIdx_` page-cross detection — all consistent with 6502/6510 hardware.
- **PHI2 machine** (`c64_phi2_machine.h`): tick ordering (open-bus decay → CIA/VIC → IRQ/NMI →
  AEC/BA → CPU → mem → commit), `cpuExecutionSuppressed_` freeze, deterministic kernal vector
  install.
- **CIA6526** (`c64_cia.h`): timer underflow/one-shot/chained/CNT-gated, PB6/PB7 pulse,
  serial I/O, TOD 50/60 Hz, read-to-clear ICR. `kTimerIrqModelIsCycleExact = true` correctly
  keeps the `CiaModelApprox` downgrade bit unset.
- **MPSC ingress rings** (`arpsid_bounded_mpsc_ring.h`, `ingress_fallback_edge_ring.h`,
  `sid_ingress_lane.h`): correct Vyukov sequence-number protocol; CAS claim, consumer re-arm,
  epoch-barrier `clearEnqueuedBeforeNow`; `resetBarrier_` consumer-only as documented.
- **AUv2 render buffer math** (`ArpSIDAUv2Component.mm:3376-3692`): frame cap 65536, channel
  clamp 1–2, interleaved/planar capacity vs needed all verified; zero-frame render still
  balances the use scopes.
- **AUv2 view-factory timeout arbitration** (4103-4153): `stateLock` three-case analysis,
  weak/strong capture, `__block` + semaphore happens-before; `disposeAbandonedAUv2Editor` is
  idempotent (verified: no double-free in the signal-lost race window).
- **AUv2 render-block swap** (1455-1482): `deferredOldBlock` strong local + bounded 50 ms
  drain — documented, accepted residual risk with telemetry counter.
- **PSID parser / patchbank I/O**: `psid_header.h` length-field handling, `sid_patchbank_io.cpp`
  atomic temp+fsync+rename, `count` clamps, 1 MB blob bound, null-termination of name fields.
- **Version coherence**: `VERSION.txt` = `project(... VERSION 0.9.15)` = `version.h` = README =
  CHANGELOG; enforced by `check_version_coherence.py` + ctest + CI.
- **Test registration**: all 490 test sources registered in CMake (zero orphans); the 42
  previously-orphaned sources restored via the `_arpsid_restored_tests` EXISTS-guarded list;
  `test_suite_integrity_v969` pins this.
- **`release.yml` zip integrity**: SHA256SUMS cross-check with correct awk field matching.
- **`sync_public_mirror.sh`**: `git archive | tar -x` overlay + ROM-file excludes + re-verify;
  refuses dirty trees.
- **Standalone audio/MIDI**: X11 `poll` fd mapping, RtAudio fallback chain, ALSA port-name
  cleanup, session-load bounds checks (magic/version/nameLen/stateLen), CAS loops in the test
  harness are bounded Vyukov reserves (no hang).

## 6. Suggested fix order

1. **P0-1** (readback catch-up cap) — one-line cap + telemetry; removes the only RT stall.
2. **P0-3** (pin installer fallback to the release ref + checksum) — supply chain.
3. **P1-2** (`#undef NDEBUG` × 45 or convert to `require()`) — mechanical, restores Release coverage.
4. **P1-1** (poller as `activeUsers` user + bounded join + locked callbacks copy) — teardown hardening.
5. **P0-2** (`if (chip == 0u) regs[r] = value;`) — one line, kills the multi-SID trap before any flavor needs it.
6. **P1-4** (amortized worst-priority in `sid_event_queue`) — RT bounded-work.
7. Build/CI batch: P1-5, P1-6, P1-7, P1-8, P1-10, P1-11, P1-12, P1-13, P1-9 (entitlements).
8. P2 batch per the table; doc drift fixes are trivial.
