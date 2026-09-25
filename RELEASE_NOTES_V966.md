# ArpSID v966 — host parameter presentation authority

This release closes handoff items 22.1 (host parameter display/parse split-brain), 22.2 (VST UTF-8 byte-cast) and 22.8 (unconditional VST editor stderr diagnostics) on top of the v965 canonical render-pipeline closure.

Package: `0.0.690-pass380-v966-parameter-presentation-authority-closure`

## Closed defects

- AUv2, AUv3 and VST3 each carried wrapper-local generic unit-string display formulas that disagreed with the canonical DSP laws. Observable examples now fixed: LFO rate norm 0.2 displayed `4000.0 Hz` (DSP renders 0.2885 Hz), limiter attack 0.08 displayed `160.0 ms` (DSP uses 1.6 ms), limiter release 0.35 displayed `700.0 ms` (DSP uses 356.5 ms), portamento 0.5 displayed `2.500 s` (DSP uses 1.25 s via the quadratic law), sequencer tempo 0.4 displayed `136.0 bpm` (DSP uses 132 bpm), and AUv3 displayed raw normalized values with a unit suffix.
- VST3 `getParamValueByString()` ignored parameter identity and clamped the first numeric token into [0,1]; `160 bpm` became normalized 1.0 instead of 0.5. Parsing is now parameter-ID aware and inverts the exact display law, including labels (`ON`/`OFF`, chip revisions, oversampling factors, mod sources) and both `.`/`,` decimal separators.
- VST3 `utf8ToTChar()` cast each UTF-8 byte to one UTF-16 unit, corrupting every multi-byte character (degree sign, accented patch names). Replaced with a bounded shared decoder: surrogate pairs, deterministic U+FFFD replacement of malformed input, always null-terminated, truncation never splits a pair. A UTF-16 → UTF-8 counterpart handles host text entry.
- VST3 `createView()` wrote unconditional diagnostics (including object addresses) to stderr; now compile-time gated behind `ARPSID_VST_EDITOR_DIAGNOSTICS`.
- Unit identity moved from per-wrapper string matching to one typed descriptor; the `cent` vs `ct` and `%` vs `pct` string drift that reported Generic units for master tune and the forensic percent family is closed (handoff 22.9 partially advanced).

## Architecture

- New `include/arpsid/core/sid_parameter_presentation.h`: `SidParameterPresentation::formatNormalized / parseToNormalized / unit` plus shared UTF conversion. All three wrappers delegate; none re-derives a formula.
- The presentation laws are the DSP laws: the LFO exponential curve, limiter attack/release ms laws and sequencer tempo/length laws moved into `math_utils.h` (`ArpSID_normToLfoRateHz`, `ArpSID_normToLimiterAttackMs/ReleaseMs`, `ArpSID_normToSeqTempoBpm/Steps` with inverses), and the runtime call sites (`sid_runtime_parameter_services.h`, `sid_postfx_timeline.h`, AU kernel, Phase2) now call those helpers. Numeric behavior is unchanged; the formulas simply have one home.
- Floor-binned legacy selectors (VCO waveforms, filter mode, arp octaves) present as integer bin indices because a rounded decimal cannot roundtrip through half-open floor bins.

## Regression coverage

- `ParameterPresentationAuthorityV966Tests`: handoff regression values, parse(format(v)) roundtrip across every parameter × value grid, label/boolean/enum text in numeric and label forms, malformed-input non-mutation, typed unit descriptors, UTF-8 ⇄ UTF-16 correctness (ASCII, °C, Greek/Cyrillic/CJK, surrogate pairs, malformed sequences, truncation), and wrapper source contracts pinning the delegation.

## Validation limits

- Focused source/runtime validation in this environment; full macOS AU/Logic host validation, signing and a real Steinberg SDK VST3 build remain external sign-off items (handoff 22.3–22.5 unchanged).
