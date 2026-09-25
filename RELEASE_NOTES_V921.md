# ArpSID v921 — Classic reset authority dedup closure

v921 is a final hardening pass on the v919/v920 Classic/Synth reset-authority closure.

## Fixed

- Removed remaining DrSID reset-authority duplicate mode replay inside live and factory DrSID authority loops.
- `kParamSynthModeEnable` and `kParamDrSidEnable` are now staged exactly by structural authority after DrSID live/factory kit value replay, not also replayed as ordinary DrSID kit parameters.
- This prevents any future stale snapshot/mode-array ordering from briefly or redundantly treating render-mode authority as editable kit data.

## Preserved

- SynthMode structural authority from v919 remains intact.
- DrSID/SID808 structural authority from v920 remains intact.
- Transport-reset structural authority filter remains reset-specific and does not alter normal full-state import/restore semantics.
