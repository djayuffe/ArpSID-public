# ArpSID pass350 — macOS closure log command preservation

pass350 fixes a handoff-quality bug in the pass348/pass349 closure logging path.

## Problem

`build.sh` parsed all arguments before enabling closure logging. The log banner then recorded:

```text
[ArpSID] command: ./build.sh
```

even when the user actually invoked a full closure command such as:

```bash
./build.sh --macos-closure --closure-log-dir ./release-logs
```

That made the generated `macos-closure-*.log` less forensic: the log contained the results, but not the exact requested mode.

## Fix

`build.sh` now snapshots the original argv before option parsing:

```bash
ORIGINAL_ARGS=("$@")
```

When log capture starts, it reconstructs a shell-escaped command line using that preserved argv and writes it into the log banner. This preserves flags such as `--macos-closure`, `--closure-log-dir`, `--install-auv2`, `--clear-au-cache`, and `--validate-auv2`.

## Status boundary

This is a release-handoff/logging fix only. It does not claim new `auval`, Logic runtime, VST3 SDK, or notarization success. Those remain pending until their logs are provided.
