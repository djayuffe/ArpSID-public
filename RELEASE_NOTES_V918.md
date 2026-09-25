# RELEASE NOTES V918 — final source closure complete

Version: `0.0.690-pass380-v918-final-source-closure-complete`

This pass closes the final release-artifact hygiene gap found after v917: the zip payload root directory still used the obsolete v916 source-tree name even though `VERSION.txt` reported v917. The v918 package is emitted from a correctly named root directory so extracted source, version, status, and release notes agree.

Preserved closures:

- v917 release-forward v904-v910 closure lineage guards.
- v916 strict DrSID authored-slot classification with no `>127 -> 127` aliasing.
- v916 schema-aware legacy/canonical factory restore/import helpers.
- v916 clean/off raw forensic defaults.
- v915 source cleanup and dead-file guards.
- v914 Program Change dead metadata queue cleanup.
- v913/v912/v911/v910 ingress-authority and runtime-owner hardening.

Validation in this package:

- `RELEASE_CONTENTS.sha256`: OK.
- `scripts/verify_source_tree.py`: OK.
- `scripts/check_audit_closure.py`: OK.
- Focused source/release/factory/timing closure suite v659/v818/v819/v904/v905/v906/v907/v908/v909: PASS.

Note: the AU3-heavy v687/v910 ingress tests still exceed this sandbox compile timeout when built from cold source here; their source guards remain release-forward and unchanged by v918.
