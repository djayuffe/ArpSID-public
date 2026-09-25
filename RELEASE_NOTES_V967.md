# ArpSID v967 — DrSID quick-kit base-profile parity closure

This release fixes a DrSID drum-kit split-brain found by audit, on top of the v966 host parameter presentation authority closure.

Package: `0.0.690-pass380-v967-drsid-quick-kit-base-parity-closure`

## Closed defects

- **Standard quick-kit tom-decay drift.** The on-screen DrSID quick-kit popup overlays a GUI tone/motion table (`kArpSIDDrumKitToneProfiles`) on top of the factory patch. Its row 0 "Standard" is authored to mirror the canonical factory DrSID base voicing (`applyFactoryDrSidDefaults`). v909 lengthened the factory base tom decay from `0.30` to `0.44`, but the GUI table was never updated — so selecting "Standard" from the drum tab played a shorter tom than recalling the same default kit from the host program list. The GUI base default and the Standard row are re-synced to `0.44`.
- **"Standard" quick kit was a silent no-op.** The GUI-only "Standard" kit carries slot `-1` (no backing factory patch). `_deferFactoryDrumKitApplyForSlot:` returned early for negative slots, so selecting "Standard" applied nothing to audio while still reporting "Loaded DrSID kit Standard". GUI-only kits now apply their curated realtime tone/motion profile; only the factory-patch load is skipped when no slot backs the kit.

## Scope

- The other 15 curated quick kits (Taiko, Melodic Toms, Cymbal FX, Impact FX, Bell Metal, …) are deliberately distinct voicings layered over their backing factory slots, not accidental drift, and are unchanged. The broader "quick-kit overlay differs from the raw factory slot" behavior is intentional for those curated kits; only the neutral "Standard" base is contracted to equal the factory base.

## Regression coverage

- `DrSidQuickKitBaseProfileParityV967Tests`: pins the Standard-base ↔ factory-base tom-decay invariant (either side changing alone now fails the build — this is the guard that would have caught the v909 drift), forbids the pre-v909 `0.30` base from returning, and pins the slot-(-1) apply path so the "Standard" no-op cannot regress.

## Validation limits

- Focused source/runtime validation; full macOS AU/Logic host validation, signing and a real Steinberg SDK VST3 build remain external sign-off items (unchanged from v966).
