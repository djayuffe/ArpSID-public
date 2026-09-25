# ArpSID v924 — Classic mode authority perfect closure

This release is a final source-side cleanup pass over the v919-v923 Classic/Synth reset-authority closure.

## Closed

- DrSID structural mode bits are now staged exactly once in the reset-authority block.
- DrSID live/factory kit replay branches no longer re-stage `kParamSynthModeEnable` / `kParamDrSidEnable` after the structural authority write.
- DrSID explicit reset-authority now clears `BankSlot` and `Program` dirty flags symmetrically with the SynthMode authority branch after it rewrites their canonical mirrors.
- Added source-contract guards preventing structural mode writes from reappearing inside DrSID kit replay branches.

## Validation

- `RELEASE_CONTENTS.sha256`: OK
- `scripts/verify_source_tree.py`: OK
- `scripts/check_audit_closure.py`: OK
- Focused closure suite: PASS

- SID-808 flavor no longer duplicates structural mode staging in its model override special-case.
