# ArpSID 0.0.690 pass347 — macOS closure command

This package adds one explicit command for the remaining macOS validation closure:

```bash
./build.sh --macos-closure
```

`--macos-closure` is macOS-only. It intentionally performs the whole release validation path rather than only one fragment:

```text
1. configure with AUv2 enabled
2. build the source tree
3. run the release-check guard suite
4. run full CTest
5. build and install ArpSID.component
6. refresh the AU cache
7. run strict targeted auval for all shipped AUv2 flavors
```

It is equivalent to selecting the safe release options together:

```text
--release-check
--install-auv2
--clear-au-cache
--validate-auv2
```

The AUv2 validation is still delegated to the canonical verifier:

```text
scripts/macos/verify_auv2_component.sh
```

which runs:

```bash
auval -strict -v aumu ArpS ASID
auval -strict -v aumu ArIn ASID
auval -strict -v aumu DrSD ASID
auval -strict -v aumu S808 ASID
auval -strict -v aumu C64P ASID
```

Truth state carried by this source package:

```text
source suite: PASS (313/313 on macOS, user log from pass344 lineage)
AUv2 install: PASS (user log from pass344 lineage)
AU cache refresh: PASS (user log from pass344 lineage)
auval: PENDING until --macos-closure or --validate-auv2 log is captured
Logic runtime: PENDING until Logic is launched/reloaded and exercised
```

Do not claim AU validation success, Logic runtime success, notarization success, or VST3 validation success from this package alone.

## pass349 log capture

`./build.sh --macos-closure` now captures a timestamped transcript under `./build/release-logs/` by default. Use `--closure-log-dir ./release-logs` to place the handoff log in an explicit directory.
