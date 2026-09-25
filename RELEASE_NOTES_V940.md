# ArpSID v940 render-mode transition authority closure

This closure completes the v939 BitPerfect/Classic authority pass by hardening
mode-transition and DrSID/Synth activation boundaries.

Closed:
- Any real render-mode transition now silences BitPerfect, ARP and Synth voice
  authorities, clears canonical voice tokens, and resets SEQ countdown/step
  state before the new authority takes over.
- DrSID activation through the shared special-parameter path now clears raw
  SynthMode/ARP/SEQ state, matching the deterministic SynthMode activation law.
- SynthMode activation through the shared special-parameter path also clears
  raw ARP/SEQ state and SEQ runtime cursor state.
- AU3 resetRenderModeTransitionRuntime_ resets sequencer phase/countdown and
  the prevSeqEnabled latch, so inactive SEQ cannot resume after a DrSID/Synth
  detour.
- Backend projection, AU3 direct-poly cleanup and Phase2 virtual-gate routing
  now use the shared effective ARP/SEQ helpers instead of local raw formulas.

Validation:
- RELEASE_CONTENTS.sha256: OK after packaging.
- scripts/verify_source_tree.py: OK.
- scripts/check_audit_closure.py: OK.
- ClassicModeAuthorityClosureV909Tests: PASS.
- BitPerfectClassicAuthorityV939Tests: PASS.
