# ArpSID v920 classic reset authority final closure

Version: `0.0.690-pass380-v920-classic-reset-authority-final-closure`

## Closure

- Closes the remaining reset-authority asymmetry left after v919.
- DrSID structural reset authority now preserves an explicit factory slot when the reset was invoked with an explicit DrSID/SID808 factory root, instead of always falling back to the pre-reset sticky slot.
- Removes duplicated live DrSID authority replay staging inside `resetPreservingHostParameterSnapshot()`.
- Keeps v919 SynthMode/SidRegister authority reassertion and transport-reset structural filtering.

## Validation target

- `ClassicModeAuthorityClosureV909Tests` includes v920 source-contract guards for explicit DrSID slot preservation and duplicate replay removal.
