# Changelog

All notable changes to ArpSID are documented here. The project uses
[semantic versioning](https://semver.org/); the single source of truth for the
version is `VERSION.txt`, mirrored by `project(VERSION)` in `CMakeLists.txt`
and `include/arpsid/version.h`.

## [0.9.0] — 2026-09-26

First public, cleanly versioned release. This consolidates the entire
pre-release development line (internally tracked as `0.0.690`, audit passes up
to 380 and closure revisions up to v970) into one release.

### Highlights

- **Formats:** AUv2 (five component flavors: ArpSID, ArpSID Instrument, DrSID,
  SID-808, C64 tune player), AUv3, VST3 and a standalone macOS host.
- **Engines:** BitPerfect/classic SID, `$D400` SID-register SynthMode, DrSID drum
  synthesis, SID-808, DIGI sampler with authentic `$D418` playback, and a
  cycle-exact C64 PSID/RSID player.
- **Arranger:** arpeggiator, 32-step sequencer, KIT routing, MIX page and a
  modulation matrix; reverb, limiter and the Hi-Fi post-FX chain.
- **Canonical render pipeline:** one timed-event ordering authority across
  ingress, storage and dispatch; sample-boundary-safe structural changes;
  exact-offset post-FX; render-owned telemetry.
- **Parameter presentation authority:** one shared service formats and parses
  every host-visible parameter across AUv2/AUv3/VST3 with the canonical DSP
  laws; Unicode-safe VST3 strings.
- **DrSID kits:** user-kit save normalizes to DrSID mode for `.arpsid`/`.json`;
  the GUI "Standard" quick-kit matches the factory base voicing.

### Changed

- Version scheme reset to plain semantic versioning. The previous compound
  identifiers (`0.0.690`, `pass380`, `v970`, `…-closure` qualifiers) and the
  `ARPSID_BUILD_PASS` macro are gone.
- Repository cleanup: ~150 historical per-pass release notes, closure
  reports, audit ledgers, hand-off files and a stale committed release manifest
  were removed from the tree. The 9 tests that only asserted the wording of
  those documents were removed, and ~30 source-contract tests had their
  document-wording assertions stripped; all behavioural and source-contract
  assertions are unchanged. The full history remains available in git.
- Reference documentation now lives in `docs/`.

### Fixed

- VST3 now builds off macOS (verified on Linux against VST3 SDK 3.8.1; the
  SDK validator passes 47/47). The Cocoa editor sources are compiled only on
  Apple, with a portable bridge elsewhere; ArpSID's duplicate module entry
  points (`arpsid_module_init.cpp`), which collided with the SDK's own
  `linuxmain.cpp`/`dllmain.cpp` and skipped `InitModule`, were removed; and the
  SDK's example targets (which need GTK on Linux) are disabled.
- AUv2/AUv3 `AudioComponents` version integer now uses Apple's
  `major << 16 | minor << 8 | patch` encoding, so hosts display the real
  version (previously `major*10000 + minor*100 + patch`).
- De-flaked `Auv2RenderNotifyRtSafetyV524Tests`: under a loaded parallel CTest
  run the reader thread could be starved until the publisher finished, failing
  the "snapshot observed" sanity checks. The publisher now runs (bounded) until
  the reader has observed both outcomes; the torn-pair invariant is unchanged.

### Known limitations

Verified on the Linux-buildable core with a green test suite. The following are
external platform sign-off items and are **not** claimed as verified:

- macOS AUv2/AUv3 validation in Logic Pro and other hosts.
- Code signing, notarization and an installer.
- A full build against the real Steinberg VST3 SDK in a host matrix.
- Sanitizer and fuzz coverage.
- Strict physical C64 exactness beyond the documented boundaries
  (`docs/C64_EXACTNESS_BOUNDARIES.md`).
