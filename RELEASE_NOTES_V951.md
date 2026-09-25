# ArpSID v951 — state-root staging parity closure

> Audit note (v952): v951 made AU3/Phase2 call one shared sanitizer, but that
> sanitizer still only performed generic finite/default/clamp repair. V952 is
> the release that adds actual per-parameter boolean/enum/range semantics.

v951 closes the remaining state-root restore staging parity gap after v950:

- AU3 `applyStateRootCanonical()` now sanitizes every restored root parameter with `sanitizeNormalizedParamValue(pid, value, default)`, not generic `std::clamp`.
- Phase2 `applyCanonicalStateRoot_()` now uses the same param-specific sanitize law.
- Phase2 root restore mirrors the clean value back into `runtimeModel_` through `applyAutomationPoint()` so `paramValues`, `lastAppliedParamValues`, and state-root presentation cannot drift after a root apply.
- This keeps serialized/factory/restored roots under the same staging law as projected automation, dirty-flush fallback, and AU3/Phase2 live parameter application.

Validation target added: `StateRootStagingParityV951Tests`.
