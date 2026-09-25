# ArpSID v948 — Dirty Flush Staging Authority Closure

> Audit note (v952): v948 synchronized the fallback value but did not provide
> full adapter side-effect parity or prevent older queued values from winning
> later in the block. V952 adds generation ordering and uses the normal
> execution-owner side-effect path for queue-full fallback.

v948 closes the final AU3 dirty-flush/async-param staging gap left after v947.

## Fixed

- AU3 async/UI parameter ingress now uses `sanitizeNormalizedParamValue()` with the target parameter id and default value before publishing the latest value to `params_`.
- AU3 parameter intent queue now carries the sanitized clean value instead of a generic 0..1 clamped value.
- AU3 `flushDirtyParams_()` no longer raw-copies `params_` into `renderParams_` without updating the canonical runtime model.
- Dirty latest-value fallback, including the path used when the sample-accurate parameter queue overflows, now keeps:
  - `params_`
  - `renderParams_`
  - `runtimeModel_.stateRoot()`
  synchronized before backend reprojection.
- Added `AuthorityDirtyFlushStagingV948Tests` to guard the new staging law.

## Closure law

Every parameter path that can affect render authority must use the same canonical param-specific sanitize/staging law before render or backend projection consumes it.
