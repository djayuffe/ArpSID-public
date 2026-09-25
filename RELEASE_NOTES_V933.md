# ArpSID v933 GUI effective ARP presentation closure

Status: COMPLETE.

This release closes the remaining GUI-side split between raw `ArpEnable` cache and effective note authority. Runtime and Phase2 already gated ARP authority in v932; v933 makes the GUI presentation and ARP step view use the same effective authority law.

Closed:

- Added `_effectiveArpAuthorityEnabledForModeIndex:` in AU3 view controller.
- ARP is presented as active only in CLASSIC/BitPerfect mode when raw `ArpEnable` is actually enabled.
- Pure Instrument, dedicated DrSID/SID808 flavors, SynthMode/SID-register and DrSID modes suppress stale ARP presentation.
- `_selectModePreferredTab:` no longer steers to LFO/ARP from stale raw cache when another mode owns note authority.
- `_syncPatchAndModePresentation` no longer prints ARP in the header while SynthMode/DrSID owns note routing.
- ARP step view no longer lights up from stale raw cache while ARP is not the effective note authority.
- Added source-contract guards in `GuiViewControllerWiringV590Tests` and `ClassicModeAuthorityClosureV909Tests`.

Preserved:

- v927 AU3 internal SynthMode authority.
- v928 SynthMode-before-ARP authority.
- v929 Phase2 SynthMode/ARP authority.
- v930 Instrument GUI/render authority.
- v931 Instrument effective-mode lock.
- v932 effective ARP runtime/Phase2 telemetry authority.
