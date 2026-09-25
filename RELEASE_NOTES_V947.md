# RELEASE NOTES V947 — Sanitized Side-Effect Authority Closure

- Backend/policy side effects now consume the staged sanitized value, not the raw incoming automation value.
- AU3 projected parameter body stages raw input, then reads the clean value from renderParams_ before applying backend/policy side effects.
- Phase2 projected parameter body computes sanitized clean values for duplicate-change guard and uses stagedClean for backend/policy side effects.
- Special-parameter handling no longer pre-clamps before staging; branch decisions and side effects use sanitized clean values while staging remains target-specific.
- Added AuthoritySanitizedSideEffectsV947Tests.
