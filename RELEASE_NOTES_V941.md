# ArpSID v941 dedicated drum SEQ authority closure

v941 continues the v937-v940 first-class mode-authority cleanup by closing the
remaining dedicated-drum / SID808 SEQ-state hole and the direct automation path
that could re-enable stale ARP/SEQ when a structural mode bit was turned off.

## Fixed

- Dedicated drum flavors now clear `kParamSeqEnable` everywhere they already
  clear Synth/ARP mode state:
  - AU3 state-root flavor policy
  - AUv2 state-root flavor policy
  - AU3 render flavor enforcement
- SID808 flavor now also clears `kParamSeqEnable` in AU3/AUv2 state roots and
  AU3 render enforcement.
- File-bank drum/DrSID canonicalization now clears both `kParamArpEnable` and
  `kParamSeqEnable` when promoting a root to DrSID authority.
- Direct host automation disabling `kParamSynthModeEnable` or
  `kParamDrSidEnable` now creates a pure CLASSIC / BitPerfect authority edge:
  stale ARP/SEQ raw state is cleared instead of becoming effective again.
- Added source-contract coverage for dedicated drum/SID808 SEQ clearing and the
  v941 structural-mode disable path.

## Validation

- `scripts/verify_source_tree.py`
- `scripts/check_audit_closure.py`
- `C64SidPlayerFiveFlavorPolicyV802Tests`
- `ReleaseCleanupClosureV818Tests`
- `ReleaseDeadFileClosureV819Tests`
- `ClassicModeAuthorityClosureV909Tests`
- `BitPerfectClassicAuthorityV939Tests`
