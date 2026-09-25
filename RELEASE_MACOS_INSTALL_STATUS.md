
## pass372 / fix-order #50 — DrSID user-kit save weak continuation

- `_drumSaveUserKit:` now weak-loads its delayed main-queue publish continuation before updating `_loadedDrumUserKitURL`, reloading the user-kit library, or writing bank status.
- Added `DrumUserKitSaveWeakContinuationV796Tests`.
- Version: `0.0.690-pass372-rt-safety-audit-drum-kit-save-weak-continuation`.

# ArpSID 0.0.690 pass358 — macOS install status carried forward

macOS build/install status remains carried forward; auval and Logic runtime remain pending.

# ArpSID 0.0.690 pass347 — macOS install status handoff

This file records the Mac-side validation log provided after pass344.

## Confirmed by user-provided macOS log

- Full CTest completed: `313/313` tests passed, `0` failed.
- AUv2 bundle build/install step ran after the tests.
- `ArpSID.component` was installed from the build tree to:

```text
~/Library/Audio/Plug-Ins/Components/ArpSID.component
```

- Existing component signature was replaced.
- AUv2 component cache fast refresh was triggered.
- Build helper finished with `[ArpSID] done`.

## Not yet claimed

The log did not include an `auval` success line and did not include Logic runtime validation. Therefore this package must not claim either of these yet:

```bash
auval -v aumu ArpS ASID
```

For a fuller AUv2 release pass, also validate the other shipped music-device IDs documented in `RELEASE_AUV2_INSTALL_VALIDATE.md`.

## Current release state

```text
source suite: PASS (313/313 on macOS)
AUv2 install: PASS (user-provided macOS log)
AU cache refresh: PASS (user-provided macOS log)
auval: PENDING
Logic runtime: PENDING
```


Pass346 adds an executable validation handoff:

```bash
./build.sh --validate-auv2
```

This still leaves `auval` pending until the command is run and logged.

## pass347 closure command update

Pass347 adds the full Mac closure command:

```bash
./build.sh --macos-closure
```

This is the recommended next command after the already-confirmed full CTest + AUv2 install/cache-refresh result. `auval` and Logic runtime remain pending until logs are captured.


## pass349 note

For final Mac validation, prefer `./build.sh --macos-closure --closure-log-dir ./release-logs` and provide the generated timestamped log.

## pass363 / fix-order #41 — DIGI record auto-stop weak continuations

- Fixed DIGI record auto-stop continuations queued from AudioQueue and AVAudioEngine callbacks so they no longer retain callback-local controller references into the main queue.
- Queued stops now use `stopWeakSelf`, strong-load on main, and compare `_digiRecordGeneration_v184_` against the callback/session generation before stopping.
- Guard: `DigiRecordAutostopWeakContinuationV787Tests`.
