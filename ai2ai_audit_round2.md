# ArpSID-public — AI-to-AI Ultra Low-Level Audit, Round 2

- **Target**: https://github.com/djayuffe/ArpSID-public
- **Commit audited**: `10a5acc` — "Release 0.9.15" (Thu Oct 1 13:06:06 2026 +0000)
- **Date**: 2026-10-02
- **Method**: round 2 of a multi-round audit. 10 parallel agents covering the files NOT fully read
  in round 1 (see `ai2ai_audit.md`), plus a dedicated fact-check agent that re-verified every
  candidate finding against source before it was admitted. Round 1 findings are NOT repeated here
  except where round 2 deepened them (marked "extends R1").
- **Coverage**: 100% of `include/arpsid/` (core, engines, gui, modulation, patchbank, audio),
  100% of `source/gui/` + `source/gui/vstgui/`, 100% of `source/au2/` + `source/au3/` (including
  the 2632-line `ArpSIDAudioUnit.mm` and 1083-line `ArpSIDHostAppDelegate.mm`), all 4 GitHub
  workflows, all scripts (install/macos/linux/ci), `build.sh`, `CMakeLists.txt` cross-checks,
  docs/ + README + CHANGELOG vs code. Every P0/P1 below was verified by direct source inspection.

## 0. Executive summary

| Severity | Count | Character |
|---|---|---|
| **P0** | 2 | standalone cleanup script **kills `lsd`** (the LaunchServices daemon) during the documented stale-wrapper recovery; `run_release_gate.sh` `rm -rf` on an environment-controlled path with no containment |
| **P1** | 23 | WAV parser OOB read + 1 GB main-thread alloc; AUv2 `activeRenderUsers` taken too late → UAF window on block swap/close; CoreMIDI off-main races + short-packet OOB read; "Reset All Parameters" writes read-only/virtual params; CC65 destroys user portamento time; file-bank `ensureDirectoryExists` wrong error classification; CI gates that swallow failures (unexamined build-retry rc, `always()` stale artifacts, ROM guard, token-in-URL force-push); serial-retry rc unexamined by callers |
| **P2** | 28 | dead/broken convenience APIs, doc drift, latent races, hygiene |

**Net assessment vs round 1**: the C++ core (DSP, C64 emulation, engines, rings, AUv2 render path
lock discipline) held up under a second, harder pass — zero new P0/P1 in the audio DSP itself. The
new findings concentrate in **the macOS shell/verification layer** (3 P0s, several P1s) and in the
**standalone host app's CoreMIDI handling**. Notably, 5 candidate P1s were **refuted** by the
fact-check pass and are listed in §5 so they are not re-audited.

---

## 1. P0 findings

### P0-1 — `cleanup_stale_logic_auv3_wrapper.sh` kills `lsd`, the LaunchServices daemon
`scripts/macos/cleanup_stale_logic_auv3_wrapper.sh:158,183` (verified verbatim)
```sh
/usr/bin/killall AUHostingServiceXPC AUHostingServiceXPC_arrow AUHostingService AudioComponentRegistrar auvaltool pkd lsd 2>/dev/null || true
...
/usr/bin/killall AudioComponentRegistrar pkd lsd 2>/dev/null || true
```
`lsd` is the LaunchServices daemon. The script rebuilds the LS database with `lsregister -gc`
(lines 180-181) in the same block and kills `lsd` immediately after.
- **Failure scenario**: a user following the documented stale-wrapper cleanup ends up with a
  half-rebuilt LaunchServices index — broken Finder icons, degraded Spotlight metadata, possibly
  requiring `lsregister -kill -r /System/Library ...` or a reboot.
- **Fix**: drop `lsd` from both kill lists (keep `pkd`/`AudioComponentRegistrar` if needed).

### P0-2 — `run_release_gate.sh` runs `rm -rf` on an environment-controlled path with no containment
`scripts/macos/run_release_gate.sh:43` (verified)
```sh
BUILD_DIR="${ARPSID_RELEASE_BUILD_DIR:-${TMPDIR:-/tmp}/arpsid-release-gate-build}"
...
rm -rf "$BUILD_DIR"
```
The guard at :38 only refuses paths *inside* `$ROOT`. `ARPSID_RELEASE_BUILD_DIR=$HOME` (or any
path outside the repo) passes the guard and is then `rm -rf`'d.
- **Failure scenario**: a typo'd or malicious env var destroys the user's home directory during
  the "release gate" run. The default is safe; the documented override is not.
- **Fix**: additionally refuse `$HOME`, `/`, and anything not under `$TMPDIR`/`/tmp`/`.build*`.

---

## 2. P1 findings

### P1-1 — WAV parser: 1-byte-off chunk boundary + odd `dataLen` → out-of-bounds read
`source/gui/vstgui/arpsid_wav_reader.h:34-48` (verified)
```cpp
for (std::size_t p = 12; p + 8 <= b.size();) {
    const std::uint32_t len = u32(p + 4);
    const std::size_t body = p + 8;
    if (body + len > b.size() + 1) break;          // ← admits one byte past end
    ...
    dataLen = std::min<std::size_t>(len, b.size() - body);
```
- `body + len > b.size() + 1` allows a chunk whose body ends **one byte past the buffer**; the
  RIFF spec allows at most a 1-byte odd-size padding *before the next chunk header*, and that
  padding belongs to the next chunk, not the body.
- `dataLen` is then clamped to available bytes but **not rounded down to whole frames**; the
  per-sample `std::memcpy(&x, &b[p], 4/8)` at lines 70/74 can read 1-7 bytes past `b`.
- **Failure scenario**: a truncated/corrupt WAV (file cut by 1 byte, or `len` inflated by 1) →
  heap over-read; ASan failure; in release, garbage samples or a crash. **No test exists** for
  `parseWavMono`/`readWavMono` (verified by grep across `source/tests/`).
- **Fix**: `if (body + len > b.size()) break;` and `dataLen = (dataLen / frameBytes) * frameBytes;`

### P1-2 — WAV import: unbounded main-thread allocation (~1 GB) + synchronous full decode in the UI callback
`source/gui/vstgui/arpsid_wav_reader.h:63` + `source/gui/vstgui/arpsid_editor_pages.cpp:1417-1423` (verified)
```cpp
out.samples.assign(frames, 0.f);          // frames derived from file-supplied dataLen
```
`readWavMono` caps the *file* at 256 MB; a 256 MB 8-bit mono WAV yields ~256 M samples →
`assign(256M, 0.f)` ≈ **1 GB** on the main thread, followed by a full mixdown loop, all inside
the VSTGUI file-choose completion callback.
- **Failure scenario**: a large (or malicious) WAV freezes the editor/DAW UI for seconds to
  minutes, or OOMs the host. No sample-count ceiling, no off-main decode.
- **Fix**: cap `frames` (e.g. 10 M) before allocation; decode on a background queue.

### P1-3 — AUv2: `activeRenderUsers` is incremented too late → UAF window on render-block swap and close
`source/au2/ArpSIDAUv2Component.mm` — `componentRender` takes `ScopedInstanceUse` (activeUsers) at
~:3383, then loads the render block pointer, then constructs `ScopedAuv2RenderUse`
(activeRenderUsers) at ~:3393. `publishRenderBlock` (~:1455-1482), `publishKernelPointer`, and
`componentClose` (~:2538-2544) all rely on a **50 ms bounded drain of `activeRenderUsers`**
before releasing the old block / deleting the instance.
- **Failure scenario**: a render that has loaded the block pointer but not yet incremented
  `activeRenderUsers` is invisible to the drain; the 50 ms deadline passes, the old block is
  ARC-released, and the in-flight render dereferences a freed block. Narrow (requires a render
  stalled >50 ms inside the pre-ref window) but a real UAF. This **extends R1 P1-1** (the
  unbounded `join()`) with the swap/close variant.
- **Fix**: move `ScopedAuv2RenderUse` to the first statement of `componentRender`, before
  `loadPublishedRenderBlock`.

### P1-4 — Standalone: CoreMIDI handler runs on the MIDI system thread with racy ivar reads + short-packet OOB
`source/au3/ArpSIDHostAppDelegate.mm:555-662` (verified)
- `_handleMIDIPacketList:` is called directly from the CoreMIDI read proc (line ~1082). It reads
  `_midiChannelFilter` (claimed "snapshot for this thread" — the snapshot is taken at the top,
  correct) but `_preferredDeviceName` is copied strong at entry (correct), while the per-packet
  path reads `self->_audioUnit` (an ivar) **on the MIDI thread** and messages it, while
  `_teardownAudio` nils that ivar on the main thread — a check-then-act race (verified lines
  580-585: `if (!self->_audioUnit) ...; [self->_audioUnit injectMIDIBytes:...]`).
- **Poly Aftertouch (0xA0) / Channel Pressure (0xD0)** (verified lines ~629-634): the guard is
  `if (pkt->length < 2) break;` but poly aftertouch is **2 data bytes** (note + pressure); a
  1-byte aftertouch packet then injects `data[1..2]` — `data[2]` is the **next packet's status
  byte** (OOB read of the packet payload, and wrong-length injection). 0xE0 (pitch bend) is
  correctly guarded `< 3`; 0xA0/0xD0 are not.
- **Failure scenario**: a 1-byte aftertouch from a non-conforming device → garbage injection /
  OOB read; teardown racing a MIDI burst → UAF of the AU.
- **Fix**: guard 0xA0/0xD0 with `pkt->length < 3`; weak-load the AU once at entry and inject via
  the weak reference, or bounce the whole packet list to main.

### P1-5 — Standalone: "Reset All Parameters" writes read-only and virtual params
`source/au3/ArpSIDHostAppDelegate.mm:1001-1006` (verified)
```objc
for (int i=0; i<ArpSID::kNumParams; ++i)
    [_audioUnit setParameterValue:ArpSID::kParamInfos[(size_t)i].defaultNorm forID:i];
```
`kNumParams = 512` (parameter_ids.h:48). The AUv3 tree build deliberately excludes
non-automatable params (`auv3ParamIsAutomatable`, ArpSIDAudioUnit.mm:379) — proof these IDs are
not meant to be written. Writing `kParamVirtualNote`/`kParamVirtualGate` (default 0.0 = gate
off) through `setParameterValue:` can **kill held notes**, and RO readback params get shadowed
with automation-target values.
- **Fix**: iterate only `auv3ParamIsAutomatable(i)` (as `_syncParameterTreeFromStateRoot:` does).

### P1-6 — Standalone: CC65 (portamento switch) is routed to the portamento *time* parameter
`source/au3/ArpSIDHostAppDelegate.mm:601-607` (verified)
```objc
if (cc2 == 65) {  // CC65 = portamento switch ON/OFF
    const float enable = (val2 >= 64) ? 1.f : 0.f;
    ... [strongSelf->_audioUnit setParameterValue:enable forID:ArpSID::kParamPortamentoTime];
```
`kParamPortamentoTime` is seconds-scaled (parameter_ids.h:803). Every CC65=127 on a MIDI
keyboard hard-sets a **1.0 s glide** (destroying the user's configured time); CC65=0 zeroes it.
CC65 semantically belongs to `kParamPortamentoStyle` (the enable bit).
- **Fix**: route CC65 to `kParamPortamentoStyle`; leave the time param untouched.

### P1-7 — File bank: `ensureDirectoryExists` swallows intermediate `mkdir` failures and accepts EEXIST without stat
`source/arpsid_file_bank.cpp:412-425` (verified verbatim by fact-check)
```cpp
while ((pos = p.find('/', pos + 1)) != std::string::npos) {
    const std::string sub = p.substr(0, pos);
    if (!sub.empty()) mkdir(sub.c_str(), 0755);          // result/errno ignored
}
return mkdir(p.c_str(), 0755) == 0 || errno == EEXIST;  // EEXIST w/o stat (first EEXIST path :414-417 DOES stat)
```
- Intermediate `mkdir` failures (e.g. a sibling component is a regular file) are swallowed; the
  final `errno == EEXIST` accepts an existing **file** as a directory → the subsequent
  `fopen(path, "wb")` fails with a misleading `OpenFailed` instead of `DirectoryCreateFailed`.
  Windows branch (`_mkdir`) is worse (no stat, no intermediate creation).
- **Fix**: check each intermediate `mkdir`; on final EEXIST, `stat` + `S_ISDIR` (as :414-417
  already does).

### P1-8 — `verify_auv2_component.sh`: unquoted, env-sourced subtype loop
`scripts/macos/verify_auv2_component.sh:229-231` (verified)
```sh
SUBTYPES="${ARPSID_AUVAL_SUBTYPES:-ArpS ArIn DrSD S808 C64P}"
for subtype in $SUBTYPES; do
```
An injected `ARPSID_AUVAL_SUBTYPES` (build env, wrapper) can add tokens that become auval
arguments (`-n` etc.). **Fix**: `read -r -a SUBTYPES <<<"$ARPSID_AUVAL_SUBTYPES"` + validate
`^[A-Za-z0-9]{4}$` per token.

### P1-9 — Serial-build retry discards its own exit code (extends R1 P1-7)
`scripts/arpsid_build_helpers.sh:39-51` (verified)
```sh
arpsid_build_with_serial_retry() {
  local build="$1" jobs="$2"; shift 2
  set +e
  cmake --build "${build}" -j"${jobs}" "$@"
  local rc=$?
  set -e
  if [[ ${rc} -ne 0 ]]; then
    echo "WARN: parallel build failed; retrying serially for first deterministic compiler error" >&2
    cmake --build "${build}" -j1 "$@"
  fi
}
```
The retry's rc is the function's return value — and the callers
(`run_full_closure_validation.sh:26`, `macos_build_install_validate_auv2.sh:30`) do **not** check
it, so a build that fails both in parallel and serially is treated as successful; the script then
fails later at install with a confusing "no ArpSID.component found".
- **Fix**: the function should propagate the retry rc AND callers must check it
  (`arpsid_build_with_serial_retry ... || exit 1`).

(Note: the round-2 agent also claimed `log_run` in `run_full_ctest_preflight.sh:13-18` masks
stage exit codes. **Refuted on re-verification**: the script runs `set -euo pipefail` at
script scope, and a failure inside the `"$@" | tee` pipeline propagates as the pipeline's
non-zero status, which `set -e` aborts on — the preflight stages ARE real gates. Only the
`arpsid_build_with_serial_retry` rc (above) is genuinely unexamined.)

### P1-10 — CI: `check_public_tree.sh` ROM guard is defeated by a trimmed real ROM
`scripts/ci/check_public_tree.sh` (verified full)
The release's **sole** supply-chain gate against shipping real C64 ROMs checks only (a) the
literal string `kEmbeddedC64RomsAvailable = false` and (b) file size ≤ 16 384 bytes. A real ROM
trimmed/re-encoded under 16 KB with the flag flipped passes both checks and `release.yml`
publishes it. No structural assertion that the array is the zero-filled placeholder.
- **Fix**: pin the SHA-256 of the known placeholder, or assert the array literal is all-zero.

### P1-11 — CI: `build.yml` artifact uploads use `always()` + `if-no-files-found: ignore` → stale/partial evidence
`.github/workflows/build.yml:160-162, 184-186, 267-269, 276-278`
```yaml
- uses: actions/upload-artifact@v7
  if: always() && matrix.arch == 'x86_64'
  with: { name: editor-snapshots-linux, path: build-vst3/editor-snapshots, if-no-files-found: ignore }
```
A partially-failed render (12 of 15 tabs) uploads 12 PNGs that look like a complete set — no
file-count assertion before upload (unlike the 180-preset checks).
- **Fix**: upload only on success of the render step; assert the expected snapshot count.

### P1-12 — CI: screenshots workflow force-pushes unvalidated content with a token in the URL argv
`.github/workflows/screenshots.yml:58`
```sh
git push -f "https://x-access-token:${{ github.token }}@github.com/${GITHUB_REPOSITORY}.git" HEAD:refs/heads/ci-screenshots
```
The workflow is `workflow_dispatch`-only with `contents: write` — any user with dispatch rights
can trigger a force-push of whatever the render step produced (including anything a compromised
runner writes), with no `concurrency` group and no file-set validation (only `*.png` + logs
asserted). Token in argv is visible in the runner process table.
- **Fix**: `GIT_ASKPASS`/credential helper; `concurrency` group on the branch; validate the
  committed set is only `*.png` + the two logs.

### P1-13 — CI: ccache restore key is branch-agnostic (`restore-keys` prefix)
`.github/workflows/build.yml:51,57-61`
`restore-keys: ccache-linux-${{ matrix.compiler }}-` restores any prior run's `.ccache` from any
branch. ccache's internal keying makes wrong-object reuse unlikely, but the workflow adds no
staleness guard, and the key convention collides across jobs.
- **Fix**: include a hash of `CMakeLists.txt` + compiler version in the key.

### P1-14 — Engines: `forceNoteOnVoice*` bookkeeping asymmetry (downgraded from P1 by fact-check — listed at P1 for the channel-scoped gap)
`include/arpsid/engines/single_sid_three_voice_engine.h:216-238` (verified)
`forceNoteOnVoice`/`forceNoteOnVoiceBookkeepingOnly` set `channelByVoice_[v] = -1;
noteIdByVoice_[v] = -1;` while the 5-arg `noteOn` stores the real values. Fact-check verdict:
the only producer is the internal Sid808 engine (`sid808_engine.h:447`, token = 0x40+drumIndex),
whose NoteOff uses the 1-arg `noteOff` (noteId −1 → matches), so **no host-reachable stuck gate**.
What IS confirmed: `allNotesOffChannel` (:310-324) genuinely cannot clear a forced voice
(channel −1 never matches), and `setSustainPedal`/sostenuto channel-scoped logic treats drum
voices as channel-less. **Severity: P2** (see §3).

### P1-15 — Engines: arpeggiator `process()` convenience API drops all but the last event (dead code)
`include/arpsid/engines/arpeggiator.h:349-358` (verified)
```cpp
ArpNote process(int numSamples) {
    ...
    TimedArpEvent events[kMaxTimedEventsPerProcess]{};   // 512 × ~16B on the RT stack
    const int n = collectTimedEvents(numSamples, events, kMaxTimedEventsPerProcess);
    return (n > 0) ? events[n - 1].note : ArpNote{-1, 0.0f, false};
}
```
Only the last event is returned; an earlier gate-off in the block is never seen by a caller of
this API. Fact-check: **no production caller exists** — the render pipeline uses
`collectTimedEvents` directly (`sid_runtime_engine_ops.h:105,198,217`). So this is a broken
convenience API (would cause stuck voices if ever used) + 8 KB RT stack usage, not a live audio
bug. **Severity: P2** (fix or delete).

### P1-16 — Standalone: "Save User Preset" menu is dead — the AU always refuses
`source/au3/ArpSIDHostAppDelegate.mm:694,905` + `source/au3/ArpSIDAudioUnit.mm:2185-2192` (verified)
```objc
- (BOOL)supportsUserPresets { return NO; }
- (BOOL)saveUserPreset:(AUAudioUnitPreset*)p error:... { (void)p; if (err) *err = ...; return NO; }
```
The menu item prompts the user for a name, then **always fails** with `unimpErr`. The codebase
explicitly chose `supportsUserPresets: NO` + canonical fullState — the menu item should be
deleted (or implemented). **Severity: P2** (user-visible broken feature, no crash).

### P1-17 — AUv2: GM-drum promotion writes are mirror-only and non-atomic (extends R1 P2-2)
`source/au2/ArpSIDAUv2Component.mm:2143-2167` (verified)
`promoteDrSidModeForIncomingGMNote` updates two `parameterCache` atoms (disable-synth,
enable-drum) **without** `activityMutex` and **without** `enqueueParameterIntent` — i.e. the
kernel is never told; only the `GetParameter` readback mirror changes, and the two stores are not
atomic as a pair. A host reading `kParamDrSidEnable` right after a ch-10 NoteOn sees `1` while
the engine is still in synth mode. **Severity: P1** (correctness of the readback contract).
- **Fix**: also `enqueueParameterIntent` for both params (if promotion should be audible), or
  apply the pair under one generation bump and document readback-only.

### P1-18 — AUv2: `hostTransportSnapshotAccess` struct assigned bare while the poller may dereference it
`source/au2/ArpSIDAUv2Component.mm:2026` (verified)
```cpp
impl->hostTransportSnapshotAccess = [audioUnit hostTransportSnapshotAccess];   // 64-byte struct of 8 raw atomic*
```
Written inside `syncRuntimeConfigurationLocked` (holds `activityMutex`, drains renders) but the
**poller thread reads it without any lock** (`startAuv2HostTransportPoller`/poller body,
:1868-1888) and the poller is NOT stopped on the open/initialize/format-change paths that call
`syncRuntimeConfigurationLocked` (it is stopped only on close and the HostCallbacks setter).
Latent today (all pointers point into the same long-lived AU), but a real unsynchronized struct
write observed cross-thread. **Severity: P1** (extends R1 P1-1 poller findings).
- **Fix**: stop the poller before the assignment, or publish the pointer set behind its own
  seqlock/atomic pointer.

### P1-19 — AUv3: transport snapshot seqlock has no writer serialization (torn snapshot passes the check)
`source/au3/ArpSIDAudioUnit.mm:713-756` (writer) + `:99-134` (reader, verified)
The writer does `seq=load; store(seq+1); write fields; store(seq+2)` with **no mutex**. Two
concurrent writers (e.g. host `reset` concurrent with `allocateRenderResources`) can interleave
field writes and both land on the same even generation — the reader's `before == after && even`
check **passes** on the mixed data. The 8-retry loop does not help (same even value).
- **Failure scenario**: one render block with mixed BPM/beat from two lifecycle events →
  transport glitch. `reset` is documented callable from any thread.
- **Fix**: serialize writers with a mutex (cold path); reader stays lock-free.

### P1-20 — AUv3: `selectViewConfiguration:` off-main path uses `dispatch_sync` to main with a strong `self` capture
`source/au3/ArpSIDAUExtensionViewController.mm:63-68` (verified)
```objc
if (![NSThread isMainThread]) {
    __block BOOL accepted = NO;
    dispatch_sync(dispatch_get_main_queue(), ^{
        accepted = [self selectViewConfiguration:viewConfiguration];
    });
    return accepted;
}
```
`dispatch_sync` to main from a background thread is a deadlock candidate in AppKit callback
inversions, and the block captures `self` **strongly** (holds the VC alive through teardown if
main is busy). The sibling `setExtensionAudioUnit:` (:22-31) uses the correct async-weak
pattern. **Note**: a test pins this exact `dispatch_sync` (auv2_view_controller_compile_guard
family), so it is an intentional-but-fragile design choice. **Severity: P1** (deadlock/lifetime).
- **Fix**: async-weak with an out-param/error pattern, or at least `__weak self`.

### P1-21 — AUv3: `setExtensionAudioUnit:` early-return reads ivars off-main
`source/au3/ArpSIDAUExtensionViewController.mm:22-31` (verified)
The `_extensionAudioUnit == extensionAudioUnit && self.arpsidDidAutoConnect` early-return is
evaluated on the calling (background) thread against ivars written on main — a torn
pointer/BOOL read (UB). **Fix**: dispatch the entire setter (including the check) to main
off-main.

### P1-22 — AUv2: `componentScheduleParameters` ramp offset overflow + no clamp (extends R1 P2-9)
`source/au2/ArpSIDAUv2Component.mm:3344-3359` (verified)
```cpp
const UInt32 sampleOffset = ev.eventValues.ramp.startBufferOffset +
    (UInt32)std::llround((double)(duration - 1u) * t);   // can wrap UInt32 / exceed the slice
...
kernel->enqueueParameterIntent((int)ev.parameter, value, (int32_t)sampleOffset, 0);
```
`startBufferOffset + (duration-1)` can overflow `UInt32` (wrap to a small/negative value) and
can exceed `maxFramesPerSlice`; the kernel stores it as `int32_t` and applies it as a
parent-relative offset — an out-of-block offset mis-times the ramp in the next block. The MIDI
path clamps (:3918-3920); the ramp path does not. **Fix**: clamp to `[0, maxFramesPerSlice-1]`
and guard the addition.

### P1-23 — Standalone: settings `configFolder()` inverted ternary (R1 P2-8 confirmed and re-verified)
`source/standalone/arpsid_standalone_settings.h:119-120`
```cpp
const auto appData = Presets::detail::envPath("APPDATA");
return appData.empty() ? appData : appData / "ArpSID";
```
On missing `APPDATA` the function returns an **empty path**; `settingsFile()`/`sessionFile()`
then produce empty paths and every save silently no-ops with no error. (R1 listed this as P2;
the confirmed no-op-without-diagnostic behavior keeps it at P1 for a user-facing app where
settings loss is silent.)

---

## 3. P2 findings (round 2)

| # | Location | Issue |
|---|---|---|
| 1 | `source/au2/ArpSIDAUv2Component.mm:1868,1880` | `hostCallbacks` (plain struct) read by the poller thread without `activityMutex` (writer holds it at :3191-3194) — data race on the function pointers (extends R1 P1-1; distinct from the close-path join). Snapshot under lock at poller start, or make the procs atomics. |
| 2 | `source/au2/ArpSIDAUv2Component.mm:1876-1888` | Poller also reads `impl->outputFormat.mSampleRate` (plain `AudioStreamBasicDescription`) while format setters write it under `activityMutex` without stopping the poller — same class. |
| 3 | `source/au2/ArpSIDAUv2Component.mm:1876` | Poller captures raw `impl`; lifetime guaranteed only by the close-ordering invariant (`isClosing` → join → `delete impl`), not self-contained. Document or add a generation check. |
| 4 | `source/au3/ArpSIDComponentFlavor.h:47-52` | `makeArpSIDFourCC` shifts a signed `OSType`; bytes ≥ 0x80 produce a negative value. Build in `uint32_t`, cast once. Latent (all current FourCCs ASCII). |
| 5 | `source/au3/ArpSIDParityTrace.h:76-114` | `ParityTraceLogBuffer` is a ring with a single `ready` 0/1 gate — two producers (AU + VST3 parity traces simultaneously) can interleave `vsnprintf` on the same wrapped slot → garbled diagnostic output. Diagnostic-only (`ARPSID_PARITY_TRACE` off by default). Add a per-slot generation counter. |
| 6 | `source/au3/ArpSIDAudioUnit.mm:809-811,1610-1615` | `[param setValue:clamped originator:(__bridge void*)self]` uses the AU's own address as a "don't re-notify" sentinel — address-unique but not lifetime-unique; use a file-scope static sentinel. |
| 7 | `source/au3/ArpSIDAudioUnit.mm:2594-2599` | KVC `setValue:forKey:@"extensionAudioUnit"` around a `respondsToSelector` + `@catch` scaffold for a property that exists — dead defensive code; direct typed call is cleaner. |
| 8 | `source/au3/ArpSIDDigiAudioQueueCapture.mm:185-194` | `DeferredFlush` RAII: a message callback that re-enters `start()` during unwind gets its "STARTED" delivered before the outer "BLOCKED" (ordering inversion). Diagnostic-only. |
| 9 | `include/arpsid/engines/sid808_engine.h:383,390` | `sid808DefaultConfig(Sid808Drum::Count)` fallback returns the all-zero "Count" config — verified safe (the `case Count` arm returns `{0,0,0,0,0,0,0.0f}`), but the fallback is only reachable with an out-of-range drum; keep a `std::unreachable`-style guard or an assert. |
| 10 | `include/arpsid/engines/single_sid_three_voice_engine.h:216-238` | Forced-voice `channel/noteId = -1` bookkeeping: `allNotesOffChannel` cannot clear forced voices; sustain/sostenuto channel logic treats them as channel-less. No host-reachable stuck gate (fact-checked). |
| 11 | `include/arpsid/engines/arpeggiator.h:349-358` | Dead convenience API `process()` returns only the last event + 8 KB RT stack array. Fix or delete. |
| 12 | `include/arpsid/engines/arpeggiator.h:58` | `seedFromHostPosition` `uint32_t` cast is well-defined modulo 2³² for finite input (fact-check refuted the "overflow" claim); positions 262 144 beats apart collide. Document the period. |
| 13 | `include/arpsid/engines/lfo.h:101-111` | `retrigger()` re-draws S&H/Random targets even when phase is already 0, and is gated on the same `retriggerEnabled` flag as the callback in `process()` — disabling retrigger silently kills the phase-wrap side channel. Contract ambiguity. |
| 14 | `include/arpsid/engines/sid_register_engine.h:71-77` | `SidRegFile::reset()` verified CORRECT (`r.fill(0)` zeroes everything) — the candidate finding "leaves 0x00-0x14 non-zero" is **refuted**. Residual nit: `SidRegisterEngine::reset()` (:743-753) does not `clear()` the `SidWriteQueue` — pending writes survive the reset (callers clear per block). |
| 15 | `include/arpsid/engines/sid808_engine.h:1308-1339` | Snare micro-stages and auto-release advance by *chunk*, not exact sample — the stage fires up to `nextScheduledEventChunk_` samples late (the `snareMicroStageLateCount_` counter exists precisely to track this). Deterministic but wrong latency for the ~7.5 ms snare body stage. |
| 16 | `include/arpsid/engines/sid_register_engine.h:1446-1452` | Lookahead limiter deque pruned by index-age only; at very low `delay` + long quiet periods, stale peaks can dominate the 50 ms release envelope. |
| 17 | `include/arpsid/gui/mix_panel_model.h:96-118` | Comments say "64 bytes pinned" / "MixChannel = 64 bytes" but the struct is 72 (`static_assert(sizeof == 72)`). Stale comments; also a thinking-out-loud narrative ("wait, that exceeds 64. Let me recount.") shipped in the header. |
| 18 | `include/arpsid/gui/mix_panel_model.h:252-257` | `channelVolumeDb` formula gives **+0.82 dB at volume=200** (comment claims 0 dB) and is not monotonic (dips ~−1.5 dB around volume≈40) — the RT path uses a different curve (`guiMixVolumeGain`: volume/200, 200 → 1.0). Display readout disagrees with the actual gain path. |
| 19 | `include/arpsid/gui/kit_state_blob.h:182-188` | `kitStateBlobMigrateLegacyV1` never reads `old.stepGrid.stepCount` (forces 32) — redundant field ignored; a corrupt legacy grid is sanitized after migration. P3 in practice. |
| 20 | `source/arpsid_file_bank.cpp:275-325` | `count == 0` accepted as `OK` (no explicit rejection) — fact-check verified all call sites guard (factory-default seeding / `!patches.empty()`), so no state corruption; add an explicit reject for hygiene. |
| 21 | `source/arpsid_file_bank.cpp:332` | `exportAllToDirectory` uses `std::min(patches.size(), metas.size())` — silent truncation on mismatch (unreachable with the current single caller, which resizes both to 128). |
| 22 | `source/arpsid_file_bank.cpp:348-351` | `strncpy(m.name, ...)` relies on value-initialization for NUL termination — the file's own `copyBoundedField_` helper exists for exactly this; route through it. |
| 23 | `source/arpsid_file_bank.cpp:215-260` | `loadBankFromFile` (legacy `PatchBankFile` API) returns success after a short read if the loop broke early with a reduced `patchCount` — a truncated file is accepted as a partial bank with no error. |
| 24 | `source/gui/vstgui/arpsid_editor_view.cpp:449-454` | `valueChanged` calls `setValueNormalized` on sibling controls, which re-fires `valueChanged` (VSTGUI notifies) → redundant second `setParamNormalized`/`performEdit` per drag, nested edit-gesture edge case. Suppress notification or re-entrancy guard. |
| 25 | `source/gui/vstgui/arpsid_editor_view.cpp:51-53` | `shortLabel` indexes `kParamInfos[id]` with no range check (safe today: only layout-table ids, verified by `editor_layout_coverage_tests`); add the guard. |
| 26 | `source/gui/vstgui/arpsid_vstgui_plugview.cpp:59` | `HostRunLoop::setTarget` plain-pointer write (UI thread) vs read on the run-loop thread — make it `std::atomic<IRunLoop*>`. |
| 27 | `source/gui/vstgui/arpsid_editor_pages.cpp:95-102` | `readFileBytes` never inspects `failbit`/`badbit` — a mid-file I/O error yields a partial buffer that proceeds to the PSID loader as "invalid" (or, worse, partially parsed). Check `in.bad()` after the iterator copy. |
| 28 | `source/standalone/arpsid_standalone_settings.h:48` | `Settings::sanitize` clamps `tab` to ≥0 but not to `kTabCount-1` (R1 P2-9, re-verified). |

Plus R1 P2s still open (see `ai2ai_audit.md` §3): unclamped MIDI offset (now extended by P1-22),
view-factory `interfaceVersion 0`, AUv2 process-lifetime CF leak, package-release hardcoded
counts, duplicate scripts, `ld.lld` gap, dependabot gap, `killall AudioComponentRegistrar`
contradiction, CMake `REMOVE_ITEM` missing value, fetch-SDK non-empty-dir refusal.

---

## 4. CI / packaging / supply chain (round 2, distinct from R1)

- **P1-10** ROM guard (above) — the load-bearing supply-chain control is a 16 KB size bound.
- **P1-11** `always()` artifact uploads (above).
- **P1-12** screenshots force-push + token-in-argv (above).
- **P1-13** ccache branch-agnostic restore (above).
- **P0-2** `run_release_gate.sh` unbounded `rm -rf` (above).
- **P2-29** `scripts/package_release.sh:22` — `tmp="${PACKAGE_TMP_ROOT%/}/.${base}.package.$$"` is a
  predictable, non-`mktemp` path under a shared `TMPDIR`; `mkdir -p` (not `O_EXCL`) happily
  reuses a pre-planted directory → file injection into the shipped source zip. Use `mktemp -d`.
- **P2-30** `scripts/sync_public_mirror.sh:77` — `tar --exclude` patterns are unanchored (GNU tar
  matches rightmost components); a different-path file ending in the same components could be
  unintentionally excluded from the mirror. Use `--anchored`; assert `git status --porcelain`
  before commit.
- **P2-31** `scripts/ci/check_build_warnings.sh:13-18` — `ld.lld` missing from the linker-warning
  regex (R1, re-verified); `external/resid-fp` (the product's DSP fallback header) warnings are
  silently out of scope.
- **P2-32** `scripts/verify_source_tree.py:39`, `scripts/check_audit_closure.py:34,40` —
  `read_text(errors="ignore")` on the sources the guards exist to protect silently drops
  undecodable bytes; a mid-write-corrupted `ArpSIDDSPKernel.hpp` could pass/fail on a mangled
  string. Use strict decoding.
- **P2-33** `.github/workflows/build.yml:154 vs 398` — preset-count assertion inconsistent
  (`wc -l` vs `wc -l | tr -d ' '`), non-portable for names with newlines.
- **P2-34** `.github/workflows/release.yml:93` — `echo "version=... ref=$ref"` prints the resolved
  SHA unredacted next to a step that carries `GH_TOKEN`; drop the redundant echo.
- **P2-35** `scripts/macos/verify_auv2_component.sh:196-198` — `mktemp` log file is
  world-readable in `/tmp` with potentially identifying content (bundle path, home path);
  `chmod 600`.
- **P2-36** `scripts/macos/notarize_release.sh:44,54,58` — Apple ID app-specific password passed
  on the command line (visible in `ps`); use `--password @-`/stdin or a 0600 file.
- **P2-37** `scripts/macos/verify_p2_static_guards.sh:17` — `env -i PATH=/usr/bin:/bin python3 -S -`
  hard-fails when python3 lives in Homebrew (`/opt/homebrew/bin`) → false gate failure on Apple
  Silicon dev boxes. Resolve `python3` first.
- **P2-38** `build.sh:193` — `exec > >(tee -a "$LOG_FILE")` process substitution is never waited
  on; the last log lines can be missing from the closure-evidence log on `set -e` exit paths.
- **P2-39** `scripts/macos/run_release_gate.sh:43` context — the `rm -rf` (P0-2) also runs before
  the `cmake -S "$ROOT" -B "$BUILD_DIR"` configure, so a path that exists as a *file* (not dir)
  is deleted and recreated — fine once P0-2's containment is fixed.

**Fork-PR attack surface (explicitly verified clean)**: `build.yml:33-34` `permissions:
contents: read` caps all reusable-workflow jobs; the only `pull_request` trigger (`ci.yml:6`)
inherits it; the only `contents: write` workflows (`release.yml` push/dispatch, `screenshots.yml`
dispatch-only) are never PR-triggered; every `run:` step executes scripts from the base checkout
(pinned refs), no `curl | sh` of variable URLs, no `eval` of variable content.

---

## 5. Refuted / downgraded candidates (do not re-audit)

Verified by the fact-check agent against source — these looked like findings but are not:

1. **`verify_auv2_component.sh` "missing `[[` makes verifier vacuous"** — REFUTED: bare
   `check_component_entry ...` lines ARE valid bash command invocations; the checks run.
   (Residual real defects: P1-8 unquoted loop, P2-35 mktemp.)
2. **`verify_auv2_component.sh` "auval reports PASS with zero flavors validated"** — REFUTED:
   the final-attempt fallthrough reaches `fail` (exit 1); the `for` loop is under `set -e`.
3. **`SidRegFile::reset()` leaves 0x00–0x14 non-zero** — REFUTED: `r.fill(0)` zeroes every
   register; `SidRegisterEngine::reset()` additionally resets voices/filter/DC/limiter/scope.
4. **`seedFromHostPosition` uint32 overflow** — REFUTED as UB: the cast is well-defined modulo
   2³² for finite input (the caller clamps to finite); only a seed-collision period exists.
5. **`loadBankFromFile` count==0 corrupts UI state** — REFUTED: all call sites guard (factory
   defaults seeded first / `!patches.empty()` / empty-bank view only).
6. **`SidCoreRegisterWriteRing::lostEventCount_` data race via telemetry** — REFUTED: no
   render/telemetry path reads it (grep-verified; kernel `readTelemetry` reads only its own
   atomics). Residual: `reset()` vs a live `push()` can tear the ring (debug surface only).
7. **forceNoteOnVoice "host-reachable stuck gate"** — REFUTED: the only producer is the internal
   Sid808 path whose NoteOff uses the 1-arg overload (noteId −1 matches). Residual P2: channel-
   scoped ops can't clear forced voices.
8. **arpeggiator `process()` live audio bug** — DOWNGRADED: no production caller (dead API).
9. **AUv2 `cachedFactoryPresetArray` coherence race** — REFUTED: `dispatch_once` serializes;
   the vector is stable after `CFArrayCreate`.
10. **`channelVolumeDb` "not monotonic"** — CONFIRMED as stated in P2-18 (derivative
    36n−12 < 0 for n < 1/3).

## 6. Verified-clean this round (due-diligence record)

- **AUv2 render path lock discipline** (full re-read, 4214 lines): no mutex/lock is taken anywhere
  in `componentRender` or its callees; all four mutexes are non-RT only; the
  `AuditedMutex<NonRealtime>` + `SidRealtimeScope` + slice-audit counters would detect a future
  RT lock.
- **AUv2 seqlocks** (state snapshot, render-notify table, host-transport generation): writers
  fence odd/even with release/acquire; readers are bounded (3 attempts / one-skip / 8 retries)
  with even+stable validation and fail-closed on exhaustion. No unbounded RT retry, no torn
  acceptance. (Exception: P1-19 AUv3 writer-side serialization.)
- **AUv2 atomics**: acquire/release pairing correct on `parameterCache`, `publishedRenderBlock`,
  `publishedKernel`, `activeUsers`/`activeRenderUsers`, `maxFramesPerSlice`, `isClosing`,
  `presetApplyInProgress`; relaxed loads on transport data sit correctly inside the generation
  fence.
- **AUv2 state save/restore**: save delegates to the wrapped AUv3's canonical state (the AUv2
  mirror is not mixed in); restore holds `activityMutex` + `presetApplyInProgress`, and a
  concurrent render detects the flag via the seqlock snapshot and fails closed (silence) — the
  split-brain guard works as designed.
- **AUv2 MIDI**: per-message contract (host expands running status); every byte bounds-checked;
  kernel re-validates via `rawMidiChannelVoiceLengthOk_`; ring push bounded `n<=4`.
- **AU factory**: ownership correct (ARC +1 to host); flavor description matching exhaustive.
- **GUI panel models** (all 23 headers): every file-supplied blob uses an **exact-size gate**
  before `memcpy` (no length-prefixed parse) except `arpsid_file_bank.cpp`, which caps its one
  length field at 64 KiB before allocation and delegates to the checksummed
  `decodeSidStateRootBinary`. `digi_sample_bank_v596.h` PCM loops bounded by 60 000 frames in
  every path; NaN/inf handled; name copies bounded. `kit_state_blob.h` v2 (1676 B) / legacy
  (1388 B) exact-size gates. `settings_panel_model.h` sanitize-clamps every field.
- **VSTGUI editor**: view lifetime (VSTGUI refcounting, `SharedPointer` cleared in `close()`
  before `forget()`); `kNumParams = 512` consistent across layout/control/guard; no GUI-thread
  read of a render-thread scalar found (telemetry is pulled at 30 Hz into an owned copy).
- **Docs/CHANGELOG vs code** (16 doc files + README + full CHANGELOG): zero P1 drift; all
  quantitative claims verified (kNumParams 512, bypass 1024, kTabCount 17, kSidHardRestartCycles
  46, dirty-RAM 8192/1024, subcycle 256, kMaxTimedEventsPerProcess 512, 208 host-MIDI params
  286-493, 32 parameter units, all referenced files exist). One P2: CHANGELOG 0.9.5 Wayland
  packages claim (the backend is compiled OFF, `VSTGUI_ENABLE_WAYLAND_SUPPORT OFF` at
  CMakeLists:521).
- **`bash -n`** clean on all 27 in-scope shell scripts; `py_compile` clean on all Python.
- **Fork-PR surface**: clean (see §4).

## 7. Suggested fix order (round 2)

1. **P0-1** drop `lsd` from the kill lists (one line, prevents system-level damage).
2. **P0-2** contain `ARPSID_RELEASE_BUILD_DIR` (a few lines).
3. **P1-1 + P1-2** WAV parser: boundary fix + frame rounding + frame cap (the untrusted-file
   parser has zero test coverage — add tests).
4. **P1-3** move `ScopedAuv2RenderUse` to the top of `componentRender` (one line, closes the
   UAF window on swap AND close).
5. **P1-4/P1-5/P1-6** standalone CoreMIDI + param-write + CC65 fixes (all in
   `ArpSIDHostAppDelegate.mm`, untested surface).
6. **P1-9/P1-10/P1-11/P1-12** CI gate hardening (propagate build-retry rc, ROM byte-pin,
   `always()` removal, screenshots validation).
7. **P1-7/P1-17/P1-18/P1-19/P1-20/P1-21/P1-22/P1-23** wrapper/threading batch.
8. P2 batch per the tables; R1's §6 list remains.
