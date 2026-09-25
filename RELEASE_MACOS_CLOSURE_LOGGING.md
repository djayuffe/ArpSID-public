# ArpSID pass349 — macOS build-green handoff

Use this when closing the release on macOS and handing the result back into ChatGPT.

## Full closure with default log directory

```bash
./build.sh --macos-closure
```

This writes a timestamped transcript under:

```text
./build/release-logs/macos-closure-YYYYmmdd-HHMMSS.log
```

## Full closure with explicit handoff directory

```bash
./build.sh --macos-closure --closure-log-dir ./release-logs
```

Attach or paste the generated `macos-closure-*.log` after the run. That log is the evidence for:

- release-check
- full CTest
- AUv2 build/install
- AU cache refresh
- strict targeted auval

## Truth boundary

This source package does not claim `auval success`, Logic validation success, VST3 validation, or notarization. Those statuses may only be updated after their logs are provided.
