# ArpSID AUv2 release install / validation

Use this from the unpacked release root on macOS:

```bash
setopt interactive_comments 2>/dev/null || true
ARPSID_AUV2_CLEAN=1 ./scripts/macos_build_install_validate_auv2.sh
```

The shipped AUv2 identifiers are music-device Audio Units:

```bash
auval -strict -v aumu ArpS ASID
auval -strict -v aumu ArIn ASID
auval -strict -v aumu DrSD ASID
auval -strict -v aumu S808 ASID
auval -strict -v aumu C64P ASID
```

Do **not** validate with the stale/effect ID `aufx ArpS UlfB`; that does not match this release's Info.plist.

Manual install, if needed:

```bash
cmake -S . -B .build/auv2-release -DCMAKE_BUILD_TYPE=Release -DARPSID_BUILD_AUV2=ON -DARPSID_BUILD_TESTS=ON
cmake --build .build/auv2-release --target arpsid_auv2 --parallel
./scripts/macos/install_auv2_component.sh .build/auv2-release "$HOME/Library/Audio/Plug-Ins/Components" -
ARPSID_AUV2_HARD_REFRESH=1 ./scripts/macos/refresh_auv2_component.sh "$HOME/Library/Audio/Plug-Ins/Components/ArpSID.component"
./scripts/macos/verify_auv2_component.sh "$HOME/Library/Audio/Plug-Ins/Components/ArpSID.component"
```

A compatibility wrapper is also provided at:

```bash
./scripts/install_auv2_component.sh
```
