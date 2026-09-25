# ArpSID 0.0.690 pass347 — auval / macOS closure command status

This package carries forward the user-provided macOS result from the pass344/pass345 lineage:

```text
source suite: PASS (313/313 on macOS)
AUv2 install: PASS
AU cache refresh: PASS
```

The remaining AUv2 validation can be run either as a targeted auval-only step:

```bash
./build.sh --validate-auv2
```

or as the full Mac closure step:

```bash
./build.sh --macos-closure
```

For a full rebuild/install/refresh/validation pass without the release-check/full-CTest bundle, use:

```bash
./build.sh --install-auv2 --clear-au-cache --validate-auv2
```

`--validate-auv2` delegates to `scripts/macos/verify_auv2_component.sh`, which performs targeted strict validation for the installed AUv2 component:

```bash
auval -strict -v aumu ArpS ASID
auval -strict -v aumu ArIn ASID
auval -strict -v aumu DrSD ASID
auval -strict -v aumu S808 ASID
auval -strict -v aumu C64P ASID
```

Current truth state:

```text
source suite: PASS (313/313 on macOS)
AUv2 install: PASS
AU cache refresh: PASS
auval: PENDING until the command above is run and its log is captured
Logic runtime: PENDING until Logic is launched/reloaded and exercised
```

Do not claim AU validation success or Logic runtime success from this package alone.


## pass349 note

For final Mac validation, prefer `./build.sh --macos-closure --closure-log-dir ./release-logs` and provide the generated timestamped log.
