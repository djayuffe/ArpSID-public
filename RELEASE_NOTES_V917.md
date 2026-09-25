# RELEASE_NOTES_V917.md

v917 final closure completion pass.

## Closed

- Made v904-v910 closure lineage tests release-forward instead of pinning each guard to a small obsolete VERSION.txt window. The tests now require the current `0.0.690-pass380-v*` closure train identity, so future closure releases do not self-stale the preserved timing/authority contracts.
- Wired the legacy/canonical factory-slot schema into the production compatibility import helper, not only factory/test helpers. Pre-schema flat parameter imports now use `FactorySlotSchema::Legacy128`; normal root/factory paths remain canonical.
- Added production import coverage proving legacy flat slot 127 restores as DrSID while canonical slot 127 restores as SID808.
- Preserved the v916 fixes: strict authored DrSID slot predicate, canonical/legacy schema helpers, clean/off raw forensic defaults, and passing v904-v909 closure guards.

Source-side closure: COMPLETE
