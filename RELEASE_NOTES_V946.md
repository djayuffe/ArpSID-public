# ArpSID v946 — Authority Final Staging Closure

This release closes the remaining staging-canonicalization edge after v945:

- The shared `runtimeStageNormalizedParameter()` helper no longer pre-clamps values before delegating to target-specific staging.
- `CanonicalRuntimeTargetAdapter::applyProjectedNormalizedParameter()` no longer pre-clamps before `runtimeApplyProjectedParameterBody()`.
- AU3 legacy/local `stageRenderParam_()` now routes through `runtimeStageNormalizedParameterOnly()` instead of manually writing `params_` / `renderParams_`.
- Added `AuthorityFinalStagingV946Tests` to guard the final staging law and prove that param-specific sanitize preserves non-zero defaults for NaN/invalid values.

Authority law after v946:

`runtimeStageNormalizedParameter()` delegates raw value -> target param-specific sanitize -> params/render/runtimeModel sync.

This prevents a final split-brain class where all mirrors agree on a value that was generically pre-clamped to `0.0` before the parameter-specific default/sanitize rules could run.
